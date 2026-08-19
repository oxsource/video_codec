// encoder_push_contract_test.cc
//
// Spec 004, US1/A4: the default `SetOutputSink` on the abstract encoders
// returns kUnsupportedOperation (push-incapable backends stay in pull mode).
// The real FFmpeg backends override this and return kOk (covered by
// encode_push_test). Also covers the CodecFactory::Create{Video,Audio}
// push-mode overloads: create + Init + SetOutputSink in one Result-wrapped
// call.

#include "src/framework/api/audio_encoder.h"
#include "src/framework/api/video_encoder.h"
#include "src/framework/api/codec_factory.h"
#include "src/framework/core/packet_sink.h"
#include "gtest/gtest.h"

namespace video {
namespace codec {
namespace {

class StubVideoEncoder : public VideoEncoder {
 public:
  Status Init() override { return Status::kOk; }
  Result<VideoPacket> Encode(const VideoFrame&) override { return Ok(VideoPacket{}); }
  Result<VideoPacket> Encode(const NativeBuffer&) override {
    return Err<VideoPacket>(Status::kUnsupportedOperation);
  }
  Result<VideoPacket> Flush() override { return Ok(VideoPacket{}); }
  void Release() override {}
};

class StubAudioEncoder : public AudioEncoder {
 public:
  Status Init() override { return Status::kOk; }
  Result<AudioPacket> Encode(const AudioFrame&) override { return Ok(AudioPacket{}); }
  Result<AudioPacket> Flush() override { return Ok(AudioPacket{}); }
  void Release() override {}
};

// Push-capable stubs: the abstract default SetOutputSink is unsupported, so a
// backend that wants push mode must override it (as the FFmpeg backend does).
class PushStubVideoEncoder : public StubVideoEncoder {
 public:
  Status SetOutputSink(PacketSink*) override { return Status::kOk; }
};

class PushStubAudioEncoder : public StubAudioEncoder {
 public:
  Status SetOutputSink(PacketSink*) override { return Status::kOk; }
};

// A sink that accepts (and drops) every packet — the queue implements
// PacketSink the same way at the call site.
class NullSink : public PacketSink {
 public:
  Status Push(VideoPacket&&) override { return Status::kOk; }
  Status Push(AudioPacket&&) override { return Status::kOk; }
};

TEST(EncoderPushContractTest, DefaultVideoSetOutputSinkIsUnsupported) {
  StubVideoEncoder enc;
  EXPECT_EQ(enc.SetOutputSink(nullptr), Status::kUnsupportedOperation);
}

TEST(EncoderPushContractTest, DefaultAudioSetOutputSinkIsUnsupported) {
  StubAudioEncoder enc;
  EXPECT_EQ(enc.SetOutputSink(nullptr), Status::kUnsupportedOperation);
}

TEST(EncoderPushContractTest, CreateVideoWithSinkInitsAndWires) {
  CodecFactory::RegisterVideo(
      Backend::kAndroid, [](const VideoConfig&) { return std::make_unique<PushStubVideoEncoder>(); });
  NullSink sink;
  VideoConfig cfg;
  cfg.backend = Backend::kAndroid;
  auto r = CodecFactory::CreateVideo(cfg, &sink);
  ASSERT_TRUE(r.ok()) << "status: " << static_cast<int>(r.status());
  EXPECT_NE(r.value(), nullptr);
}

TEST(EncoderPushContractTest, CreateAudioWithSinkInitsAndWires) {
  CodecFactory::RegisterAudio(
      Backend::kAndroid, [](const AudioConfig&) { return std::make_unique<PushStubAudioEncoder>(); });
  NullSink sink;
  AudioConfig cfg;
  cfg.backend = Backend::kAndroid;
  auto r = CodecFactory::CreateAudio(cfg, &sink);
  ASSERT_TRUE(r.ok()) << "status: " << static_cast<int>(r.status());
  EXPECT_NE(r.value(), nullptr);
}

TEST(EncoderPushContractTest, CreateVideoWithSinkFailsForUnregisteredBackend) {
  VideoConfig cfg;
  cfg.backend = Backend::kFFmpeg;  // not linked in this api-only test
  auto r = CodecFactory::CreateVideo(cfg, nullptr);
  ASSERT_FALSE(r.ok());
  EXPECT_EQ(r.status(), Status::kPlatformUnsupported);
}

TEST(EncoderPushContractTest, CreateAudioWithSinkFailsForUnregisteredBackend) {
  AudioConfig cfg;
  cfg.backend = Backend::kFFmpeg;  // not linked in this api-only test
  auto r = CodecFactory::CreateAudio(cfg, nullptr);
  ASSERT_FALSE(r.ok());
  EXPECT_EQ(r.status(), Status::kPlatformUnsupported);
}

TEST(EncoderPushContractTest, CreateVideoWithSinkPropagatesSetOutputSinkStatus) {
  // A push-incapable backend (default SetOutputSink) must surface its
  // kUnsupportedOperation through the Result rather than pretending to wire.
  CodecFactory::RegisterVideo(
      Backend::kAndroid, [](const VideoConfig&) { return std::make_unique<StubVideoEncoder>(); });
  NullSink sink;
  VideoConfig cfg;
  cfg.backend = Backend::kAndroid;
  auto r = CodecFactory::CreateVideo(cfg, &sink);
  ASSERT_FALSE(r.ok());
  EXPECT_EQ(r.status(), Status::kUnsupportedOperation);
}

}  // namespace
}  // namespace codec
}  // namespace video
