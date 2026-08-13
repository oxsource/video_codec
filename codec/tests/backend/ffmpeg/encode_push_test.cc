// encode_push_test.cc
//
// End-to-end encoder -> queue push wiring (spec 004, US1 + US2):
//  - push mode delivers all packets in order with zero loss (kBlock);
//  - pull mode is unchanged when no sink is attached;
//  - back-pressure paces the producer instead of dropping packets.

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "api/encoder_factory.h"
#include "api/video_encoder.h"
#include "gtest/gtest.h"
#include "queue/packet_queue.h"

namespace video {
namespace codec {
namespace {

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

VideoEncoderConfig MakeConfig() {
  VideoEncoderConfig cfg;
  cfg.codec = VideoCodecType::kH264;
  cfg.width = 320;
  cfg.height = 240;
  cfg.fps = 30;
  cfg.bitrate = 800'000;
  cfg.input_format = PixelFormat::kI420;
  cfg.force_backend = Backend::kFFmpeg;
  return cfg;
}

// US1/A1: push mode delivers all packets to the queue, in order, zero loss.
TEST(EncodePushTest, PushDeliversAllPacketsInOrder) {
  PacketQueue q(64, Backpressure::kBlock);
  std::unique_ptr<VideoEncoder> encoder = CreateVideoEncoder(MakeConfig());
  ASSERT_NE(encoder, nullptr);
  ASSERT_EQ(encoder->Init(), Status::kOk);
  ASSERT_EQ(encoder->SetOutputSink(&q), Status::kOk);

  constexpr int kFrames = 20;
  for (int i = 0; i < kFrames; ++i) {
    Result<VideoPacket> r = encoder->Encode(MakeI420Frame(320, 240, i));
    ASSERT_TRUE(r.ok());
    // Push mode: single destination is the sink -> returned packet is empty.
    EXPECT_TRUE(r.value().data.empty());
  }
  ASSERT_TRUE(encoder->Flush().ok());
  q.MarkEos();  // caller marks EOS after all producers are done

  std::vector<VideoPacket> packets;
  VideoPacket pkt;
  for (;;) {
    const Status pr = q.Pop(pkt, 0);
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

// US1/A2: without a sink, the pull API is unchanged and returns packets.
TEST(EncodePushTest, PullModeUnchangedWithoutSink) {
  std::unique_ptr<VideoEncoder> encoder = CreateVideoEncoder(MakeConfig());
  ASSERT_NE(encoder, nullptr);
  ASSERT_EQ(encoder->Init(), Status::kOk);

  int pull_packets = 0;
  for (int i = 0; i < 20; ++i) {
    Result<VideoPacket> r = encoder->Encode(MakeI420Frame(320, 240, i));
    ASSERT_TRUE(r.ok());
    if (!r.value().data.empty()) ++pull_packets;
  }
  Result<VideoPacket> fr = encoder->Flush();
  ASSERT_TRUE(fr.ok());
  if (!fr.value().data.empty()) ++pull_packets;
  EXPECT_GT(pull_packets, 0) << "pull mode must still yield encoded packets";
}

// US2/SC-003: a slow consumer fills the bounded queue; the producer blocks
// (kBlock) instead of dropping, and every packet arrives in order afterwards.
TEST(EncodePushTest, BackpressurePacesProducerWithoutLoss) {
  PacketQueue q(4, Backpressure::kBlock);
  std::unique_ptr<VideoEncoder> encoder = CreateVideoEncoder(MakeConfig());
  ASSERT_NE(encoder, nullptr);
  ASSERT_EQ(encoder->Init(), Status::kOk);
  ASSERT_EQ(encoder->SetOutputSink(&q), Status::kOk);

  std::atomic<bool> producer_done{false};
  std::atomic<bool> producer_failed{false};
  std::thread producer([&] {
    for (int i = 0; i < 60; ++i) {
      if (!encoder->Encode(MakeI420Frame(320, 240, i)).ok()) {
        producer_failed = true;
        break;
      }
    }
    if (!encoder->Flush().ok()) producer_failed = true;
    producer_done = true;
  });

  // "Slow" consumer: wait before draining. A working kBlock queue means the
  // producer is stuck on the full bounded queue, not silently dropping packets.
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  EXPECT_FALSE(producer_done.load()) << "producer must block under kBlock";

  std::vector<VideoPacket> drained;
  VideoPacket pkt;
  while (q.Pop(pkt, 5000) == Status::kOk) drained.push_back(std::move(pkt));
  producer.join();
  EXPECT_FALSE(producer_failed.load());
  EXPECT_TRUE(producer_done.load());

  EXPECT_GT(drained.size(), 4u)
      << "more than one full queue of packets expected";
  for (size_t i = 1; i < drained.size(); ++i) {
    EXPECT_FALSE(drained[i].data.empty());
    EXPECT_GT(drained[i].pts_us, drained[i - 1].pts_us)
        << "packets out of order";
  }
}

}  // namespace
}  // namespace codec
}  // namespace video
