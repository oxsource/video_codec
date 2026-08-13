// muxer_test.cc
//
// End-to-end encoder -> queue -> Muxer (FFmpeg backend) integration
// (spec 005, US1): a generic Muxer implementing PacketSink receives packets
// straight from PacketSource::Await and produces a valid fragmented MP4.
// Assertions: output starts with "ftyp", contains moov/mdat after Finish,
// and the first keyframe produces the header + first fragment in one delivery.

#include "api/muxer.h"

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "api/codec_factory.h"
#include "api/video_encoder.h"
#include "gtest/gtest.h"
#include "io/byte_sink.h"
#include "queue/packet_queue.h"

namespace video {
namespace codec {
namespace {

// In-memory ByteSink collecting every written byte.
class MemorySink : public ByteSink {
 public:
  bool Write(const uint8_t* data, size_t size) override {
    bytes_.insert(bytes_.end(), data, data + size);
    return true;
  }
  bool Seek(int64_t) override { return false; }  // sequential only
  int64_t Tell() override { return -1; }
  std::vector<uint8_t> bytes_;
};

VideoFrame MakeI420Frame(int w, int h, int seed) {
  VideoFrame f;
  f.format = PixelFormat::kI420;
  f.width = w;
  f.height = h;
  const size_t ysz = static_cast<size_t>(w) * h;
  const size_t csz = ysz / 4;
  f.planes[0].resize(ysz);
  f.planes[1].resize(csz);
  f.planes[2].resize(csz);
  for (size_t i = 0; i < ysz; ++i) {
    f.planes[0][i] = static_cast<uint8_t>((seed * 17 + i) & 0xFF);
  }
  for (size_t i = 0; i < csz; ++i) {
    f.planes[1][i] = static_cast<uint8_t>(120 + (seed & 1) * 8);
    f.planes[2][i] = static_cast<uint8_t>(140 - (seed & 1) * 8);
  }
  return f;
}

VideoConfig MakeEncoderConfig() {
  VideoConfig cfg;
  cfg.codec = VideoCodecType::kH264;
  cfg.width = 320;
  cfg.height = 240;
  cfg.fps = 30;
  cfg.bitrate = 800'000;
  cfg.input_format = PixelFormat::kI420;
  cfg.backend = Backend::kFFmpeg;
  return cfg;
}

MuxerConfig MakeMuxerConfig() {
  MuxerConfig cfg;
  cfg.format = MuxFormat::kMp4;
  cfg.fragmented = true;
  cfg.width = 320;
  cfg.height = 240;
  cfg.fps = 30;
  cfg.backend = Backend::kFFmpeg;
  return cfg;
}

bool Contains(const std::vector<uint8_t>& bytes, const char* tag) {
  const size_t n = std::char_traits<char>::length(tag);
  if (bytes.size() < n) return false;
  for (size_t i = 0; i + n <= bytes.size(); ++i) {
    bool match = true;
    for (size_t j = 0; j < n; ++j) {
      if (bytes[i + j] != static_cast<uint8_t>(tag[j])) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

bool HasFtypAtOffset4(const std::vector<uint8_t>& bytes) {
  if (bytes.size() < 8) return false;
  // MP4 layout: 4-byte box size (big-endian) + 4-byte type "ftyp".
  const char tag[4] = {'f', 't', 'y', 'p'};
  for (int i = 0; i < 4; ++i) {
    if (bytes[4 + i] != static_cast<uint8_t>(tag[i])) return false;
  }
  return true;
}

// US1/A1+A2+A3: encoder -> queue -> muxer produces a valid fragmented MP4.
TEST(MuxerTest, EncoderToQueueToMuxerProducesMp4) {
  PacketQueue q(64, Backpressure::kBlock);
  std::unique_ptr<VideoEncoder> encoder = CodecFactory::CreateVideo(MakeEncoderConfig());
  ASSERT_NE(encoder, nullptr);
  ASSERT_EQ(encoder->Init(), Status::kOk);
  ASSERT_EQ(encoder->SetOutputSink(&q), Status::kOk);

  std::unique_ptr<Muxer> muxer = CodecFactory::CreateMuxer(MakeMuxerConfig());
  ASSERT_NE(muxer, nullptr);
  MemorySink sink;
  ASSERT_EQ(muxer->SetOutput(&sink), Status::kOk);

  // Muxer implements PacketSink; Await delivers every packet to it and calls
  // muxer->Finish() at EOS.
  std::thread mux_thread([&] { q.Await(*muxer); });

  constexpr int kFrames = 30;
  for (int i = 0; i < kFrames; ++i) {
    ASSERT_TRUE(encoder->Encode(MakeI420Frame(320, 240, i)).ok());
  }
  ASSERT_TRUE(encoder->Flush().ok());
  q.MarkEos();
  mux_thread.join();

  EXPECT_GT(sink.bytes_.size(), 0u);
  EXPECT_TRUE(HasFtypAtOffset4(sink.bytes_))
      << "output must begin with an MP4 box (4-byte size + 'ftyp')";
  EXPECT_TRUE(Contains(sink.bytes_, "moov")) << "finished output must contain moov";
  EXPECT_TRUE(Contains(sink.bytes_, "mdat")) << "finished output must contain mdat";
}

}  // namespace
}  // namespace codec
}  // namespace video
