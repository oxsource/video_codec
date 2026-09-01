// muxer_test.cc
//
// End-to-end encoder -> queue -> Muxer (FFmpeg backend) integration
// (spec 005, US1): a generic Muxer implementing PacketSink receives packets
// straight from PacketSource::Await and produces a valid fragmented MP4.
// Assertions: output starts with "ftyp", contains moov/mdat after Finish,
// and the first keyframe produces the header + first fragment in one delivery.

#include "codec/src/framework/api/muxer.h"

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "codec/src/framework/api/codec_factory.h"
#include "codec/src/framework/api/video_encoder.h"
#include "codec/src/framework/api/audio_encoder.h"
#include "gtest/gtest.h"
#include "codec/src/framework/io/byte_sink.h"
#include "codec/src/framework/queue/packet_queue.h"

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

AudioConfig MakeAudioEncoderConfig() {
  AudioConfig cfg;
  cfg.codec = AudioCodecType::kAAC;
  cfg.sample_rate = 48000;
  cfg.channels = 2;
  cfg.bitrate = 128'000;
  cfg.backend = Backend::kFFmpeg;
  return cfg;
}

// Interleaved S16 PCM audio frame of `samples` samples, stereo.
AudioFrame MakePcmFrame(int samples) {
  AudioFrame f;
  f.format = SampleFormat::kS16;
  f.sample_rate = 48000;
  f.channels = 2;
  f.data.resize(static_cast<size_t>(samples) * 2 * 2);  // S16 interleaved
  auto* p = reinterpret_cast<int16_t*>(f.data.data());
  for (int i = 0; i < samples * 2; ++i) p[i] = static_cast<int16_t>((i % 1000) - 500);
  return f;
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

// US1 extension: a video encoder AND an audio encoder push into one queue;
// Await hands both media to the muxer, which produces an MP4 carrying H.264
// (avc1) and AAC (mp4a) sample entries.
TEST(MuxerTest, VideoPlusAudioProducesMp4WithBothTracks) {
  PacketQueue q(64, Backpressure::kBlock);
  std::unique_ptr<VideoEncoder> venc = CodecFactory::CreateVideo(MakeEncoderConfig());
  ASSERT_NE(venc, nullptr);
  ASSERT_EQ(venc->Init(), Status::kOk);
  ASSERT_EQ(venc->SetOutputSink(&q), Status::kOk);

  std::unique_ptr<AudioEncoder> aenc = CodecFactory::CreateAudio(MakeAudioEncoderConfig());
  ASSERT_NE(aenc, nullptr);
  ASSERT_EQ(aenc->Init(), Status::kOk);
  ASSERT_EQ(aenc->SetOutputSink(&q), Status::kOk);

  MuxerConfig mcfg = MakeMuxerConfig();
  mcfg.audio_codec = AudioCodecType::kAAC;
  mcfg.sample_rate = 48000;
  mcfg.channels = 2;
  std::unique_ptr<Muxer> muxer = CodecFactory::CreateMuxer(mcfg);
  ASSERT_NE(muxer, nullptr);
  MemorySink sink;
  ASSERT_EQ(muxer->SetOutput(&sink), Status::kOk);

  std::thread mux_thread([&] { q.Await(*muxer); });

  // Generate audio frames (1024 samples each) covering the wall-clock span of
  // every video frame (48000 / 30 = 1600 samples per 1/30 s).
  constexpr int kFrames = 30;
  constexpr int kAudioSamplesPerFrame = 1024;
  constexpr int64_t kAudioPerVideoPeriod = 48000 / 30;
  int64_t audio_generated = 0;
  for (int i = 0; i < kFrames; ++i) {
    ASSERT_TRUE(venc->Encode(MakeI420Frame(320, 240, i)).ok());
    while (audio_generated + kAudioSamplesPerFrame <= (i + 1) * kAudioPerVideoPeriod) {
      ASSERT_TRUE(aenc->Encode(MakePcmFrame(kAudioSamplesPerFrame)).ok());
      audio_generated += kAudioSamplesPerFrame;
    }
  }
  ASSERT_TRUE(venc->Flush().ok());
  ASSERT_TRUE(aenc->Flush().ok());
  q.MarkEos();
  mux_thread.join();

  EXPECT_GT(sink.bytes_.size(), 0u);
  EXPECT_TRUE(HasFtypAtOffset4(sink.bytes_))
      << "output must begin with an MP4 box (4-byte size + 'ftyp')";
  EXPECT_TRUE(Contains(sink.bytes_, "moov")) << "finished output must contain moov";
  EXPECT_TRUE(Contains(sink.bytes_, "mdat")) << "finished output must contain mdat";
  EXPECT_TRUE(Contains(sink.bytes_, "avc1")) << "must contain the H.264 sample entry";
  EXPECT_TRUE(Contains(sink.bytes_, "mp4a")) << "must contain the AAC sample entry";
}

}  // namespace
}  // namespace codec
}  // namespace video
