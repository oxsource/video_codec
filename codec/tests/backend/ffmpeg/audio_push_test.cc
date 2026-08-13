// audio_push_test.cc
//
// Spec 004, US1/A5: the audio encoder's push mode delivers audio packets
// audio packets to the output sink in order (single destination).

#include <cstdint>
#include <vector>

#include "api/audio_encoder.h"
#include "api/encoder_factory.h"
#include "gtest/gtest.h"
#include "queue/packet_queue.h"

namespace video {
namespace codec {
namespace {

AudioFrame MakePcmFrame(int samples) {
  AudioFrame f;
  f.format = SampleFormat::kS16;
  f.sample_rate = 48000;
  f.channels = 2;
  f.data.resize(static_cast<size_t>(samples) * 2 * 2);  // S16 interleaved
  auto* p = reinterpret_cast<int16_t*>(f.data.data());
  for (int i = 0; i < samples * 2; ++i)
    p[i] = static_cast<int16_t>((i % 1000) - 500);
  return f;
}

TEST(AudioPushTest, PushDeliversAudioPacketsInOrder) {
  PacketQueue q(64, Backpressure::kBlock);
  AudioEncoderConfig cfg;
  cfg.codec = AudioCodecType::kAAC;
  cfg.sample_rate = 48000;
  cfg.channels = 2;
  cfg.bitrate = 128'000;
  cfg.force_backend = Backend::kFFmpeg;

  std::unique_ptr<AudioEncoder> encoder = CreateAudioEncoder(cfg);
  ASSERT_NE(encoder, nullptr);
  ASSERT_EQ(encoder->Init(), Status::kOk);
  ASSERT_EQ(encoder->SetOutputSink(&q), Status::kOk);

  for (int i = 0; i < 10; ++i) {
    Result<AudioPacket> r = encoder->Encode(MakePcmFrame(1024));
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(
        r.value().data.empty());  // push mode: single destination = sink
  }
  ASSERT_TRUE(encoder->Flush().ok());
  q.MarkEos();  // caller marks EOS after all producers are done

  std::vector<AudioPacket> packets;
  AudioPacket pkt;
  for (;;) {
    const Status pr = q.Next(pkt, 0);
    if (pr == Status::kEos || pr == Status::kEmpty) break;
    ASSERT_EQ(pr, Status::kOk);
    packets.push_back(std::move(pkt));
  }
  EXPECT_GT(packets.size(), 0u);
  for (size_t i = 1; i < packets.size(); ++i) {
    EXPECT_FALSE(packets[i].data.empty());
    EXPECT_GT(packets[i].pts_us, packets[i - 1].pts_us)
        << "packets out of order";
  }
}

}  // namespace
}  // namespace codec
}  // namespace video
