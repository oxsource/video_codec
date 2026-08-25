#include "src/core/stream_impl.h"
#include "src/core/backend_registry.h"

#include <algorithm>

#include "src/framework/core/status.h"
#include "src/framework/core/types.h"

namespace video {
namespace stream {

StreamImpl::StreamImpl(const StreamConfig& config) : config_(config) {
  status_.state = StreamState::kCreated;
}

StreamImpl::~StreamImpl() {
  Release();
}

video::codec::Status StreamImpl::ValidateConfig(const StreamConfig& config) const {
  if (config.backend_type.empty()) return video::codec::Status::kInvalidArgument;
  if (config.remote_url.empty()) return video::codec::Status::kInvalidArgument;
  if (config.initial_bitrate_kbps == 0) return video::codec::Status::kInvalidArgument;
  if (config.resolution_width == 0 || config.resolution_height == 0) {
    return video::codec::Status::kInvalidArgument;
  }
  if (config.framerate == 0) return video::codec::Status::kInvalidArgument;
  return video::codec::Status::kOk;
}

video::codec::Status StreamImpl::Init() {
  if (status_.state >= StreamState::kConfigured) {
    return video::codec::Status::kOk;
  }
  if (status_.state >= StreamState::kDestroyed) {
    return video::codec::Status::kUnsupportedOperation;
  }

  auto validation = ValidateConfig(config_);
  if (validation != video::codec::Status::kOk) {
    UpdateStatus(StreamState::kDisconnected);
    status_.last_error = "invalid config";
    return validation;
  }

  backend_ = CreateBackend(config_.backend_type, config_);
  if (!backend_) {
    UpdateStatus(StreamState::kDisconnected);
    status_.last_error = "backend not found: " + config_.backend_type;
    return video::codec::Status::kBackendUnavailable;
  }

  backend_->SetStatusCallback(
      [this](const StreamStatus& s) { OnBackendStatusChange(s); });

  abr_controller_ = std::make_unique<AbrController>(
      config_.initial_bitrate_kbps,
      config_.min_bitrate_kbps,
      config_.max_bitrate_kbps);

  reconnect_handler_ = std::make_unique<ReconnectHandler>(
      config_.reconnect_max_interval_s,
      config_.buffer_duration_s);

  UpdateStatus(StreamState::kConfigured);
  return video::codec::Status::kOk;
}

video::codec::Status StreamImpl::Start() {
  if (status_.state != StreamState::kConfigured) {
    if (status_.state == StreamState::kCreated) {
      status_.last_error = "Init() must be called before Start()";
      return video::codec::Status::kNotInitialized;
    }
    if (status_.state == StreamState::kStreaming) {
      return video::codec::Status::kOk;
    }
    status_.last_error = "invalid state transition";
    return video::codec::Status::kUnsupportedOperation;
  }

  UpdateStatus(StreamState::kConnecting);
  auto st = backend_->Connect(config_);
  if (st != video::codec::Status::kOk) {
    status_.last_error = "Connect failed";
    UpdateStatus(StreamState::kDisconnected);
    return st;
  }

  abr_controller_->Reset();
  UpdateStatus(StreamState::kStreaming);
  return video::codec::Status::kOk;
}

video::codec::Status StreamImpl::Stop() {
  if (status_.state != StreamState::kStreaming &&
      status_.state != StreamState::kReconnecting) {
    return video::codec::Status::kOk;
  }

  if (reconnect_handler_) reconnect_handler_->Reset();
  auto st = backend_->Disconnect();
  UpdateStatus(StreamState::kDisconnected);
  return st;
}

void StreamImpl::Release() {
  if (status_.state == StreamState::kStreaming ||
      status_.state == StreamState::kReconnecting) {
    Stop();
  }
  backend_.reset();
  abr_controller_.reset();
  reconnect_handler_.reset();
  UpdateStatus(StreamState::kDestroyed);
}

video::codec::Status StreamImpl::SendVideo(const video::codec::VideoPacket& packet) {
  if (status_.state < StreamState::kConfigured) {
    return video::codec::Status::kNotInitialized;
  }
  if (!backend_) return video::codec::Status::kNotInitialized;

  auto st = backend_->SendVideo(packet);
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

  return st;
}

video::codec::Status StreamImpl::SendAudio(const video::codec::AudioPacket& packet) {
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

  auto st = backend_->SendAudio(packet);
  if (st == video::codec::Status::kOk) {
    status_.frames_sent++;
    backpressure_buffer_count_ = 0;
  } else {
    backpressure_buffer_count_++;
    status_.frames_dropped++;
    status_.last_error = "SendAudio failed";
  }
  return st;
}

video::codec::Status StreamImpl::UpdateConfig(const StreamConfig& config) {
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
  return video::codec::Status::kOk;
}

StreamStatus StreamImpl::GetStatus() const {
  return status_;
}

void StreamImpl::SetStatusCallback(StatusCallback callback) {
  callback_ = std::move(callback);
}

void StreamImpl::UpdateStatus(StreamState state) {
  status_.state = state;
  if (callback_) callback_(status_);
}

void StreamImpl::OnBackendStatusChange(const StreamStatus& s) {
  status_ = s;
  if (s.state == StreamState::kDisconnected && reconnect_handler_) {
    reconnect_handler_->OnDisconnected([this]() {
      if (backend_) {
        auto st = backend_->Connect(config_);
        if (st == video::codec::Status::kOk) {
          reconnect_handler_->OnConnected();
          UpdateStatus(StreamState::kStreaming);
        } else {
          reconnect_handler_->Tick();
        }
      }
    });
  }
  if (callback_) callback_(status_);
}

void StreamImpl::OnReconnectTick() {
  if (reconnect_handler_) reconnect_handler_->Tick();
}

}  // namespace stream
}  // namespace video