#include "stream/src/core/stream_impl.h"

#include <algorithm>
#include <chrono>

#include "codec/src/framework/core/log_slot.h"
#include "codec/src/framework/core/status.h"
#include "codec/src/framework/core/types.h"
#include "stream/src/core/backend_registry.h"

namespace video {
namespace stream {

StreamImpl::StreamImpl(const StreamConfig& config) : config_(config) {
  status_.state = StreamState::kCreated;
}

StreamImpl::~StreamImpl() { Release(); }

video::codec::Status StreamImpl::ValidateConfig(
    const StreamConfig& config) const {
  if (config.backend_type.empty())
    return video::codec::Status::kInvalidArgument;
  if (config.remote_url.empty()) return video::codec::Status::kInvalidArgument;
  if (config.initial_bitrate_kbps == 0)
    return video::codec::Status::kInvalidArgument;
  if (config.resolution_width == 0 || config.resolution_height == 0) {
    return video::codec::Status::kInvalidArgument;
  }
  if (config.framerate == 0) return video::codec::Status::kInvalidArgument;
  return video::codec::Status::kOk;
}

video::codec::Status StreamImpl::Init() {
  {
    std::lock_guard<std::mutex> lock(status_mtx_);
    if (status_.state >= StreamState::kConfigured) {
      return video::codec::Status::kOk;
    }
    if (status_.state >= StreamState::kDestroyed) {
      return video::codec::Status::kUnsupportedOperation;
    }

    auto validation = ValidateConfig(config_);
    if (validation != video::codec::Status::kOk) {
      status_.last_error = "invalid config";
      status_.state = StreamState::kDisconnected;
      return validation;
    }
  }

  backend_ = CreateBackend(config_.backend_type, config_);
  if (!backend_) {
    std::lock_guard<std::mutex> lock(status_mtx_);
    status_.last_error = "backend not found: " + config_.backend_type;
    status_.state = StreamState::kDisconnected;
    return video::codec::Status::kBackendUnavailable;
  }

  backend_->SetStatusCallback(
      [this](const StreamStatus& s) { OnBackendStatusChange(s); });

  abr_controller_ = std::make_unique<AbrController>(
      config_.initial_bitrate_kbps, config_.min_bitrate_kbps,
      config_.max_bitrate_kbps);

  reconnect_handler_ = std::make_unique<ReconnectHandler>(
      config_.reconnect_max_interval_s, config_.buffer_duration_s);

  StartReconnectWorker();

  UpdateStatus(StreamState::kConfigured);
  return video::codec::Status::kOk;
}

video::codec::Status StreamImpl::Start() {
  {
    std::lock_guard<std::mutex> lock(status_mtx_);
    if (status_.state != StreamState::kConfigured) {
      if (status_.state == StreamState::kCreated) {
        status_.last_error = "Init() must be called before Start()";
        return video::codec::Status::kNotInitialized;
      }
      if (status_.state == StreamState::kStreaming) {
        return video::codec::Status::kOk;
      }
      if (status_.state == StreamState::kReconnecting) {
        // An initial connection attempt is already being retried internally.
        return video::codec::Status::kOk;
      }
      status_.last_error = "invalid state transition";
      return video::codec::Status::kUnsupportedOperation;
    }
  }

  UpdateStatus(StreamState::kConnecting);
  auto st = backend_->Connect(config_);
  if (st != video::codec::Status::kOk) {
    std::lock_guard<std::mutex> lock(status_mtx_);
    status_.last_error =
        std::string("Connect failed: ") + video::codec::StatusToString(st);
  }
  if (st != video::codec::Status::kOk) {
    // Initial connection failed: keep retrying in the background instead of
    // leaving the stream stuck in kDisconnected.
    ScheduleReconnect();
    UpdateStatus(StreamState::kReconnecting);
    return st;
  }

  if (abr_controller_) abr_controller_->Reset();
  {
    std::lock_guard<std::mutex> lock(status_mtx_);
    status_.last_error.clear();
  }
  UpdateStatus(StreamState::kStreaming);
  return video::codec::Status::kOk;
}

video::codec::Status StreamImpl::Stop() {
  {
    std::lock_guard<std::mutex> lock(status_mtx_);
    if (status_.state != StreamState::kStreaming &&
        status_.state != StreamState::kReconnecting) {
      return video::codec::Status::kOk;
    }
  }

  // Stop the reconnect machinery first so no retry can race the teardown and
  // so a Disconnect()-triggered kDisconnected callback below is not re-armed.
  StopReconnectWorker();
  if (reconnect_handler_) reconnect_handler_->Reset();

  auto st = backend_ ? backend_->Disconnect() : video::codec::Status::kOk;
  UpdateStatus(StreamState::kDisconnected);
  return st;
}

void StreamImpl::Release() {
  bool active = false;
  {
    std::lock_guard<std::mutex> lock(status_mtx_);
    active = status_.state == StreamState::kStreaming ||
             status_.state == StreamState::kReconnecting;
  }
  if (active) Stop();
  StopReconnectWorker();
  reconnect_handler_.reset();
  backend_.reset();
  abr_controller_.reset();
  UpdateStatus(StreamState::kDestroyed);
}

video::codec::Status StreamImpl::SendVideo(
    const video::codec::VideoPacket& packet) {
  {
    std::lock_guard<std::mutex> lock(status_mtx_);
    if (status_.state < StreamState::kConfigured) {
      return video::codec::Status::kNotInitialized;
    }
  }
  if (!backend_) return video::codec::Status::kNotInitialized;

  auto st = backend_->SendVideo(packet);
  {
    std::lock_guard<std::mutex> lock(status_mtx_);
    if (st == video::codec::Status::kOk) {
      status_.frames_sent++;
      backpressure_buffer_count_ = 0;
    } else {
      backpressure_buffer_count_++;
      status_.frames_dropped++;
      status_.last_error = "SendVideo failed";
    }

    if (abr_controller_ && status_.state == StreamState::kStreaming) {
      auto stats = backend_->GetStats();
      abr_controller_->Update(stats.rtt_ms, stats.packet_loss_pct);
      status_.bitrate_kbps = abr_controller_->CurrentBitrateKbps();
    }
  }
  return st;
}

video::codec::Status StreamImpl::SendAudio(
    const video::codec::AudioPacket& packet) {
  {
    std::lock_guard<std::mutex> lock(status_mtx_);
    if (status_.state != StreamState::kStreaming) {
      if (status_.state < StreamState::kConfigured) {
        return video::codec::Status::kNotInitialized;
      }
      status_.last_error = "stream not started";
      return video::codec::Status::kUnsupportedOperation;
    }

    if (backpressure_buffer_count_ >= kBackpressureMaxPackets) {
      status_.frames_dropped++;
      return video::codec::Status::kOk;
    }
  }

  auto st = backend_->SendAudio(packet);
  {
    std::lock_guard<std::mutex> lock(status_mtx_);
    if (st == video::codec::Status::kOk) {
      status_.frames_sent++;
      backpressure_buffer_count_ = 0;
    } else {
      backpressure_buffer_count_++;
      status_.frames_dropped++;
      status_.last_error = "SendAudio failed";
    }
  }
  return st;
}

video::codec::Status StreamImpl::UpdateConfig(const StreamConfig& config) {
  {
    std::lock_guard<std::mutex> lock(status_mtx_);
    if (status_.state == StreamState::kStreaming ||
        status_.state == StreamState::kReconnecting) {
      status_.last_error = "cannot update config while streaming";
      return video::codec::Status::kUnsupportedOperation;
    }
    auto validation = ValidateConfig(config);
    if (validation != video::codec::Status::kOk) {
      status_.last_error = "invalid config";
      return validation;
    }
    config_ = config;
  }
  return video::codec::Status::kOk;
}

StreamStatus StreamImpl::GetStatus() const {
  std::lock_guard<std::mutex> lock(status_mtx_);
  return status_;
}

void StreamImpl::SetStatusCallback(StatusCallback callback) {
  std::lock_guard<std::mutex> lock(status_mtx_);
  callback_ = std::move(callback);
}

void StreamImpl::UpdateStatus(StreamState state) {
  StreamStatus snapshot;
  StatusCallback cb;
  {
    std::lock_guard<std::mutex> lock(status_mtx_);
    status_.state = state;
    snapshot = status_;
    cb = callback_;
  }
  if (cb) cb(snapshot);
}

void StreamImpl::OnBackendStatusChange(const StreamStatus& s) {
  StreamStatus snapshot;
  StatusCallback cb;
  bool schedule = false;
  {
    std::lock_guard<std::mutex> lock(status_mtx_);
    bool was_active = status_.state == StreamState::kStreaming ||
                      status_.state == StreamState::kReconnecting;
    status_ = s;
    // A genuine runtime drop (only when we were actually active, and never
    // while the user is stopping) arms the internal reconnect worker. Keep
    // reporting kReconnecting across repeated kDisconnected events so the
    // upper layer sees a stable state while retries are in flight.
    if (s.state == StreamState::kDisconnected && was_active &&
        reconnect_handler_ && !re_stop_.load()) {
      if (!reconnect_armed_.exchange(true)) {
        schedule = true;
      }
      status_.state = StreamState::kReconnecting;
    }
    snapshot = status_;
    cb = callback_;
  }
  if (schedule) re_cv_.notify_all();
  if (cb) cb(snapshot);
}

void StreamImpl::StartReconnectWorker() {
  std::lock_guard<std::mutex> lock(re_mtx_);
  if (reconnect_thread_.joinable()) return;
  re_stop_.store(false);
  reconnect_armed_.store(false);
  reconnect_thread_ = std::thread([this] { RunReconnectWorker(); });
}

void StreamImpl::StopReconnectWorker() {
  re_stop_.store(true);
  reconnect_armed_.store(false);
  re_cv_.notify_all();
  if (reconnect_thread_.joinable() &&
      reconnect_thread_.get_id() != std::this_thread::get_id()) {
    reconnect_thread_.join();
  }
}

void StreamImpl::ScheduleReconnect() {
  if (re_stop_.load()) return;
  reconnect_armed_.store(true);
  re_cv_.notify_all();
}

void StreamImpl::AttemptReconnect() {
  if (!backend_) return;
  auto st = backend_->Connect(config_);
  if (st == video::codec::Status::kOk) {
    VC_LOG(video::codec::LogLevel::kDebug, "reconnect succeeded");
    if (reconnect_handler_) reconnect_handler_->OnConnected();
    reconnect_armed_.store(false);
    if (abr_controller_) abr_controller_->Reset();
    {
      std::lock_guard<std::mutex> lock(status_mtx_);
      status_.last_error.clear();
    }
    UpdateStatus(StreamState::kStreaming);
  } else {
    VC_LOG(video::codec::LogLevel::kWarn,
           std::string("reconnect attempt failed: ") +
               video::codec::StatusToString(st));
    {
      std::lock_guard<std::mutex> lock(status_mtx_);
      status_.last_error =
          std::string("reconnect failed: ") + video::codec::StatusToString(st);
    }
  }
}

void StreamImpl::RunReconnectWorker() {
  while (true) {
    {
      std::unique_lock<std::mutex> lock(re_mtx_);
      re_cv_.wait_for(lock, std::chrono::seconds(1),
                      [this] { return re_stop_.load(); });
      if (re_stop_.load()) break;
    }
    if (!reconnect_armed_.load() || !reconnect_handler_) continue;
    if (!reconnect_handler_->IsReconnecting()) {
      reconnect_handler_->OnDisconnected([this] { AttemptReconnect(); });
    }
    reconnect_handler_->Tick();
  }
}

}  // namespace stream
}  // namespace video
