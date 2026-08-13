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

StatusCode FileSinkConsumer::Consume(EncodedPacket&& pkt) {
  if (!video_file_.is_open()) return StatusCode::kInvalidArgument;
  if (!pkt.data.empty()) {
    video_file_.write(reinterpret_cast<const char*>(pkt.data.data()),
                      static_cast<std::streamsize>(pkt.data.size()));
  }
  return video_file_.good() ? StatusCode::kOk : StatusCode::kEncodeFailed;
}

StatusCode FileSinkConsumer::Consume(AudioPacket&& pkt) {
  if (audio_path_.empty() || !audio_file_.is_open()) {
    // No audio sink configured; treat as a no-op success.
    return StatusCode::kOk;
  }
  if (!pkt.data.empty()) {
    audio_file_.write(reinterpret_cast<const char*>(pkt.data.data()),
                      static_cast<std::streamsize>(pkt.data.size()));
  }
  return audio_file_.good() ? StatusCode::kOk : StatusCode::kEncodeFailed;
}

StatusCode FileSinkConsumer::Finish() {
  if (video_file_.is_open()) video_file_.close();
  if (audio_file_.is_open()) audio_file_.close();
  return StatusCode::kOk;
}

}  // namespace codec
}  // namespace video
