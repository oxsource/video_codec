// file_consumer_test.cc
#include "src/framework/consumer/file_consumer.h"

#include <cstdio>
#include <fstream>
#include <thread>
#include <vector>

#include "src/framework/consumer/packet_consumer.h"
#include "gtest/gtest.h"
#include "src/framework/queue/packet_queue.h"

namespace video {
namespace codec {
namespace {

std::string TempPath(const std::string& name) { return std::string("/tmp/") + name; }

// A stand-in for a future StreamConsumer, proving the encoder is transport
// agnostic: swapping FileConsumer for this requires no encoder change.
class StubConsumer : public PacketConsumer {
 public:
  Status Push(VideoPacket&& pkt) override {
    received_.insert(received_.end(), pkt.data.begin(), pkt.data.end());
    return Status::kOk;
  }
  Status Push(AudioPacket&&) override { return Status::kOk; }
  Status Finish() override {
    finished_ = true;
    return Status::kOk;
  }
  std::vector<uint8_t> received_;
  bool finished_ = false;
};

// Producer runs on its OWN thread (SPSC: one producer, one consumer). Pushes
// `n` packets then marks EOS. Returns the packets it pushed (for comparison).
std::vector<std::vector<uint8_t>> RunProducer(PacketQueue& q, int n, std::thread& producer) {
  std::vector<std::vector<uint8_t>> expected;
  producer = std::thread([&q, n, &expected] {
    for (int i = 0; i < n; ++i) {
      VideoPacket p;
      p.data = {static_cast<uint8_t>('A' + (i % 26)), static_cast<uint8_t>(i)};
      p.keyframe = (i % 10 == 0);
      expected.push_back(p.data);
      EXPECT_EQ(q.Push(std::move(p)), Status::kOk);
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

// --- End-to-end: encoder thread -> ring -> PacketPump -> FileConsumer
// -----
TEST(FileConsumerTest, EncoderToRingToFile) {
  const std::string path = TempPath("vc_filesink_test.h264");
  std::remove(path.c_str());

  PacketQueue q(16, Backpressure::kBlock);
  std::thread producer;
  auto expected = RunProducer(q, 50, producer);

  FileConsumer sink(path);
  q.Await(sink);  // consumer thread (this thread) drains until EOS
  producer.join();

  std::vector<uint8_t> file = ReadFile(path);
  std::vector<uint8_t> concatenated;
  for (const auto& pkt : expected) {
    concatenated.insert(concatenated.end(), pkt.begin(), pkt.end());
  }
  ASSERT_EQ(file, concatenated) << "file content must equal concatenated packets";
  std::remove(path.c_str());
}

// --- Transport-agnostic: swap FileConsumer for a stub StreamConsumer
// ------
TEST(FileConsumerTest, SwapConsumerNeedsNoEncoderChange) {
  PacketQueue q(16, Backpressure::kBlock);
  std::thread producer;
  auto expected = RunProducer(q, 30, producer);

  StubConsumer stub;
  q.Await(stub);  // identical wiring; different consumer
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
