#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <rtc/rtc.hpp>
#include <string>
#include <thread>

#include "codec/src/framework/core/status.h"
#include "stream/src/api/stream_backend.h"
#include "stream/src/api/stream_config.h"
#include "stream/src/backend/webrtc/whip_session.h"

namespace video {
namespace stream {

class WebrtcBackend : public StreamBackend {
 public:
  explicit WebrtcBackend(const StreamConfig& config);
  ~WebrtcBackend() override;

  video::codec::Status Connect(const StreamConfig& config) override;
  video::codec::Status SendVideo(
      const video::codec::VideoPacket& packet) override;
  video::codec::Status SendAudio(
      const video::codec::AudioPacket& packet) override;
  video::codec::Status Disconnect() override;

  StreamStats GetStats() override;
  void SetStatusCallback(StatusCallback callback) override;

  static std::unique_ptr<StreamBackend> Create(const StreamConfig& config);

 private:
  // Every libdatachannel callback carries the session generation it was
  // registered under. Teardown bumps session_gen_, so events that arrive from
  // a closing/old PeerConnection after a Disconnect() or a fresh Connect() are
  // ignored instead of touching the members of the new session.
  bool IsCurrentSession(uint64_t gen) const {
    return gen == session_gen_.load();
  }

  void OnStateChange(uint64_t gen, rtc::PeerConnection::State state);
  void OnLocalDescription(uint64_t gen, const std::string& offer);
  void OnGatheringStateChange(uint64_t gen,
                              rtc::PeerConnection::GatheringState state);
  void OnTrackOpen(uint64_t gen);
  void OnTrackClosed(uint64_t gen);
  void OnWhipReady(uint64_t gen, const std::string& sdp);
  void OnWhipError(uint64_t gen, const std::string& error);

  // Atomically claims the right to send the WHIP offer exactly once per
  // Connect() (P0-5). Returns true for the single winner.
  bool ClaimOfferSend();
  // Launches the WHIP POST on a dedicated worker thread (S3) so the
  // libdatachannel internal thread is never blocked on HTTP.
  void SendOfferToWhip(uint64_t gen, const std::string& offer);
  void WhipCreateWorker(uint64_t gen, std::string endpoint,
                        const std::string& offer);
  // Closes and releases the current PeerConnection and joins the WHIP worker.
  void TeardownConnection();
  void JoinWhipWorker();
  std::shared_ptr<rtc::PeerConnection> CurrentPeerConnection();
  // Resets all per-connection state at the start of Connect() (P0-3).
  void ResetConnectState();
  // Signals a connect failure through answer_ready_ + connect_result_.
  void FailConnect(video::codec::Status status, const std::string& error);
  // Reports a new public stream state to the upper layer (serialised).
  void NotifyStreamState(StreamState state);
  // Promotes kIceConnected -> kStreaming once the track is open and at least
  // one frame has been acknowledged sent (P1-4).
  void TryMarkStreaming();

  StreamConfig config_;
  std::string whip_endpoint_;
  StreamStatus status_;
  std::unique_ptr<WhipSession> whip_session_;
  std::shared_ptr<rtc::PeerConnection> pc_;
  std::shared_ptr<rtc::Track> video_track_;
  // Held so SendVideo() can advance the RTP timestamp per frame and drive the
  // RTCP sender report. The library never increments it automatically; a
  // constant timestamp makes the receiver treat the whole stream as a single
  // access unit (no decodable frames).
  std::shared_ptr<rtc::RtpPacketizationConfig> rtp_config_;
  // Kept so SendVideo() can periodically request an RTCP Sender Report
  // (P1-3). RtcpSrReporter has no internal timer and only reports when
  // setNeedsToReport() has been called.
  std::shared_ptr<rtc::RtcpSrReporter> rtcp_sr_reporter_;
  StreamStats stats_;
  StatusCallback callback_;

  std::mutex mtx_;
  // Recursive so a status callback that re-enters the backend (e.g. Stop() ->
  // Disconnect()) on the same thread does not self-deadlock, while still
  // serialising concurrent deliveries from different threads.
  std::recursive_mutex cb_mtx_;
  std::condition_variable cv_;
  std::thread whip_thread_;
  std::atomic<uint64_t> session_gen_{0};
  std::atomic<bool> offer_ready_{false};
  std::atomic<bool> gathering_complete_{false};
  std::atomic<bool> answer_ready_{false};
  std::atomic<bool> connected_state_{false};
  std::atomic<bool> track_open_{false};
  std::atomic<bool> pc_terminal_{false};
  std::atomic<bool> first_frame_sent_{false};
  std::atomic<bool> streaming_reported_{false};
  std::atomic<bool> audio_unsupported_logged_{false};
  // Only true once a session is fully established (after Connect() succeeds)
  // so SendVideo() from the encoder thread never races a reconnect worker
  // that is re-initialising the media objects.
  std::atomic<bool> send_enabled_{false};
  // Snapshot of config_.debug taken under the lock; avoids a data race on the
  // std::string config_ when a reconnect worker reassigns it while the encoder
  // thread is logging.
  std::atomic<bool> debug_log_{false};
  // connect_result_ is written by the WHIP worker and read by Connect() after
  // waking; guard with mtx_.
  video::codec::Status connect_result_ = video::codec::Status::kOk;

  // frame_index_ is advanced by SendVideo() and reset on (re)connect; atomic
  // so the two never race even across the send_enabled_ boundary.
  std::atomic<uint64_t> frame_index_{0};
  // Timers are stored as microseconds since epoch to stay lock-free on the
  // send hot path while still being reset concurrently by Connect().
  std::atomic<int64_t> last_sr_us_{0};
  std::atomic<int64_t> last_drop_log_us_{0};
  // Only touched under mtx_.
  bool annexb_checked_ = false;
};

}  // namespace stream
}  // namespace video
