// file_sink_consumer_test.cc
#include "consumer/file_sink_consumer.h"
#include "consumer/packet_consumer.h"
#include "consumer/packet_pump.h"
#include "queue/encoded_packet_queue.h"

#include <cstdio>
#include <fstream>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

namespace video {
namespace codec {
namespace {

std::string TempPath(const std::string& name) {
  return std::string("/tmp/") + name;
}

// A stand-in for a future StreamConsumer, proving the encoder is transport
// agnostic: swapping FileSinkConsumer for this requires no encoder change.
class StubConsumer : public PacketConsumer {
 public:
  StatusCode Consume(EncodedPacket&& pkt) override {
    received_.insert(received_.end(), pkt.data.begin(), pkt.data.end());
    return StatusCode::kOk;
  }
  StatusCode Consume(AudioPacket&&) override { return StatusCode::kOk; }
  StatusCode Finish() override {
    finished_ = true;
    return StatusCode::kOk;
  }
  std::vector<uint8_t> received_;
  bool finished_ = false;
};

// Producer runs on its OWN thread (SPSC: one producer, one consumer). Pushes
// `n` packets then marks EOS. Returns the packets it pushed (for comparison).
std::vector<std::vector<uint8_t>> RunProducer(EncodedPacketQueue& q, int n,
                                              std::thread& producer) {
  std::vector<std::vector<uint8_t>> expected;
  producer = std::thread([&q, n, &expected] {
    for (int i = 0; i < n; ++i) {
      EncodedPacket p;
      p.data = {static_cast<uint8_t>('A' + (i % 26)), static_cast<uint8_t>(i)};
      p.keyframe = (i % 10 == 0);
      expected.push_back(p.data);
      EXPECT_EQ(q.Submit(std::move(p)), StatusCode::kOk);
    }
    q.MarkEos();
  });
  return expected;
}

std::vector<uint8_t> ReadFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
}

// --- End-to-end: encoder thread -> ring -> PacketPump -> FileSinkConsumer -----
TEST(FileSinkConsumerTest, EncoderToRingToFile) {
  const std::string path = TempPath("vc_filesink_test.h264");
  std::remove(path.c_str());

  EncodedPacketQueue q(16, Backpressure::kBlock);
  std::thread producer;
  auto expected = RunProducer(q, 50, producer);

  FileSinkConsumer sink(path);
  PacketPump::Run(q, sink);  // consumer thread (this thread) drains until EOS
  producer.join();

  std::vector<uint8_t> file = ReadFile(path);
  std::vector<uint8_t> concatenated;
  for (const auto& pkt : expected) {
    concatenated.insert(concatenated.end(), pkt.begin(), pkt.end());
  }
  ASSERT_EQ(file, concatenated) << "file content must equal concatenated packets";
  std::remove(path.c_str());
}

// --- Transport-agnostic: swap FileSinkConsumer for a stub StreamConsumer ------
TEST(FileSinkConsumerTest, SwapConsumerNeedsNoEncoderChange) {
  EncodedPacketQueue q(16, Backpressure::kBlock);
  std::thread producer;
  auto expected = RunProducer(q, 30, producer);

  StubConsumer stub;
  PacketPump::Run(q, stub);  // identical wiring; different consumer
  producer.join();

  std::vector<uint8_t> concatenated;
  for (const auto& pkt : expected) {
    concatenated.insert(concatenated.end(), pkt.begin(), pkt.end());
  }
  ASSERT_EQ(stub.received_, concatenated);
  ASSERT_TRUE(stub.finished_);
}

}  // namespace
}  // namespace codec
}  // namespace video
