// encoder_push_contract_test.cc
//
// Spec 004, US1/A4: the default `SetOutputSink` on the abstract encoders
// returns kUnsupportedOperation (push-incapable backends stay in pull mode).
// The real FFmpeg backends override this and return kOk (covered by
// encode_push_test).

#include "audio_encoder.h"
#include "video_encoder.h"
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
