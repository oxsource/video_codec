// file_sink_consumer.cc
#include "consumer/file_sink_consumer.h"

#include <ostream>

namespace video {
namespace codec {

FileSinkConsumer::FileSinkConsumer(std::string video_path,
                                   std::string audio_path)
    : video_path_(std::move(video_path)), audio_path_(std::move(audio_path)) {
  if (!video_path_.empty()) {
    video_file_.open(video_path_, std::ios::binary);
  }
  if (!audio_path_.empty()) {
    audio_file_.open(audio_path_, std::ios::binary);
  }
}

Status FileSinkConsumer::Consume(VideoPacket&& pkt) {
  if (!video_file_.is_open()) return Status::kInvalidArgument;
  if (!pkt.data.empty()) {
    video_file_.write(reinterpret_cast<const char*>(pkt.data.data()),
                      static_cast<std::streamsize>(pkt.data.size()));
  }
  return video_file_.good() ? Status::kOk : Status::kEncodeFailed;
}

Status FileSinkConsumer::Consume(AudioPacket&& pkt) {
  if (audio_path_.empty() || !audio_file_.is_open()) {
    return Status::kOk;  // no audio sink configured: no-op success
  }
  if (!pkt.data.empty()) {
    audio_file_.write(reinterpret_cast<const char*>(pkt.data.data()),
                      static_cast<std::streamsize>(pkt.data.size()));
  }
  return audio_file_.good() ? Status::kOk : Status::kEncodeFailed;
}

Status FileSinkConsumer::Finish() {
  if (video_file_.is_open()) video_file_.close();
  if (audio_file_.is_open()) audio_file_.close();
  return Status::kOk;
}

}  // namespace codec
}  // namespace video
