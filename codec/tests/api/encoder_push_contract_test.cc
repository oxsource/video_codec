// encoder_push_contract_test.cc
//
// Spec 004, US1/A4: the default `SetOutputSink` on the abstract encoders
// returns kUnsupportedOperation (push-incapable backends stay in pull mode).
// The real FFmpeg backends override this and return kOk (covered by
// encode_push_test).

#include "api/audio_encoder.h"
#include "api/video_encoder.h"
#include "gtest/gtest.h"

namespace video {
namespace codec {
namespace {

class StubVideoEncoder : public VideoEncoder {
 public:
  Status Init() override { return Status::kOk; }
  Result<Packet> Encode(const VideoFrame&) override { return Ok(Packet{}); }
  Result<Packet> Encode(const NativeBuffer&) override {
    return Err<Packet>(Status::kUnsupportedOperation);
  }
  Result<Packet> Flush() override { return Ok(Packet{}); }
  void Release() override {}
};

class StubAudioEncoder : public AudioEncoder {
 public:
  Status Init() override { return Status::kOk; }
  Result<Packet> Encode(const AudioFrame&) override {
    Packet p;
    p.type = PacketType::kAudio;
    return Ok(std::move(p));
  }
  Result<Packet> Flush() override {
    Packet p;
    p.type = PacketType::kAudio;
    return Ok(std::move(p));
  }
  void Release() override {}
};

TEST(EncoderPushContractTest, DefaultVideoSetOutputSinkIsUnsupported) {
  StubVideoEncoder enc;
  EXPECT_EQ(enc.SetOutputSink(nullptr), Status::kUnsupportedOperation);
}

TEST(EncoderPushContractTest, DefaultAudioSetOutputSinkIsUnsupported) {
  StubAudioEncoder enc;
  EXPECT_EQ(enc.SetOutputSink(nullptr), Status::kUnsupportedOperation);
}

}  // namespace
}  // namespace codec
}  // namespace video
