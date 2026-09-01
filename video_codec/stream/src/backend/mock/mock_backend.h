#pragma once

#include "stream/src/api/stream_backend.h"
#include "stream/src/api/stream_config.h"
#include "stream/src/api/stream_status.h"

namespace video {
namespace stream {

class MockBackend : public StreamBackend {
 public:
  explicit MockBackend(const StreamConfig& config);

  video::codec::Status Connect(const StreamConfig& config) override;
  video::codec::Status SendVideo(const video::codec::VideoPacket& packet) override;
  video::codec::Status SendAudio(const video::codec::AudioPacket& packet) override;
  video::codec::Status Disconnect() override;

  StreamStats GetStats() override;
  void SetStatusCallback(StatusCallback callback) override;

  bool IsConnected() const { return connected_; }
  int VideoPacketsSent() const { return video_packets_sent_; }
  int AudioPacketsSent() const { return audio_packets_sent_; }

 private:
  bool connected_ = false;
  int video_packets_sent_ = 0;
  int audio_packets_sent_ = 0;
  StreamConfig config_;
  StatusCallback callback_;
};

}  // namespace stream
}  // namespace video