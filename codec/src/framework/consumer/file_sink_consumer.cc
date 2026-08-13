// file_sink_consumer.cc
#include "consumer/file_sink_consumer.h"

namespace video {
namespace codec {

FileSinkConsumer::FileSinkConsumer(std::string video_path, std::string audio_path) {
  if (!video_path.empty()) {
    video_sink_ = std::make_unique<FileByteSink>(std::move(video_path));
  }
  if (!audio_path.empty()) {
    audio_sink_ = std::make_unique<FileByteSink>(std::move(audio_path));
  }
}

Status FileSinkConsumer::Push(VideoPacket&& pkt) {
  if (!video_sink_ || !video_sink_->IsOpen()) return Status::kInvalidArgument;
  if (!pkt.data.empty() && !video_sink_->Write(pkt.data.data(), pkt.data.size())) {
    return Status::kEncodeFailed;
  }
  return Status::kOk;
}

Status FileSinkConsumer::Push(AudioPacket&& pkt) {
  if (!audio_sink_ || !audio_sink_->IsOpen()) {
    return Status::kOk;  // no audio sink configured: no-op success
  }
  if (!pkt.data.empty() && !audio_sink_->Write(pkt.data.data(), pkt.data.size())) {
    return Status::kEncodeFailed;
  }
  return Status::kOk;
}

Status FileSinkConsumer::Finish() {
  if (video_sink_) video_sink_.reset();
  if (audio_sink_) audio_sink_.reset();
  return Status::kOk;
}

}  // namespace codec
}  // namespace video
