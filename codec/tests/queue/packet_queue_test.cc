// packet_queue_test.cc
#include "src/framework/queue/packet_queue.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

namespace video {
namespace codec {
namespace {

VideoPacket MakePkt(uint8_t tag) {
  VideoPacket p;
  p.data = {tag};
  p.keyframe = (tag % 7 == 0);
  return p;
}

// --- SPSC correctness: producer pushes N, consumer drains N in order
// ----------
TEST(PacketQueueTest, SpscInOrderNoLoss) {
  constexpr int kN = 200;
  PacketQueue q(16, Backpressure::kBlock);

  std::thread producer([&] {
    for (int i = 0; i < kN; ++i) {
      ASSERT_EQ(q.Push(MakePkt(static_cast<uint8_t>('a' + (i % 26)))), Status::kOk);
    }
  });

  int got = 0;
  std::vector<uint8_t> seen;
  std::thread consumer([&] {
    while (got < kN) {
      VideoPacket p;
      Status r = q.Pull(p, 2'000'000);
      if (r == Status::kOk) {
        seen.push_back(p.data[0]);
        ++got;
      } else if (r == Status::kEos) {
        break;
      }
    }
  });

  producer.join();
  consumer.join();
  ASSERT_EQ(got, kN);
  ASSERT_EQ(seen.size(), static_cast<size_t>(kN));
  for (int i = 0; i < kN; ++i) {
    ASSERT_EQ(seen[i], static_cast<uint8_t>('a' + (i % 26)));
  }
}

// --- kBlock back-pressure: producer blocks when the ring is full
// --------------
TEST(PacketQueueTest, BlockWaitsForConsumer) {
  PacketQueue q(2, Backpressure::kBlock);
  // Fill the ring (capacity 2).
  ASSERT_EQ(q.Push(MakePkt(1)), Status::kOk);
  ASSERT_EQ(q.Push(MakePkt(2)), Status::kOk);

  std::atomic<bool> producer_blocked{true};
  std::atomic<int> produced{0};
  std::thread producer([&] {
    for (int i = 0; i < 5; ++i) {
      ASSERT_EQ(q.Push(MakePkt(static_cast<uint8_t>(3 + i))), Status::kOk);
      produced.fetch_add(1);
    }
    producer_blocked = false;
  });

  // Give the producer a moment; it must be blocked after filling (2 already in,
  // +1 more makes 3 > capacity 2, so it blocks on the 3rd submit).
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_TRUE(producer_blocked.load()) << "producer should block on full ring";

  // Consumer drains; producer should then complete.
  int got = 0;
  while (got < 2 + 5) {
    VideoPacket p;
    Status r = q.Pull(p, 2'000'000);
    if (r == Status::kOk)
      ++got;
    else if (r == Status::kEos)
      break;
  }
  producer.join();
  ASSERT_EQ(produced.load(), 5);
  ASSERT_EQ(got, 2 + 5);
}

// --- kLatest: full ring overwrites the oldest slot
// ------------------------
TEST(PacketQueueTest, DropOldestKeepsNewest) {
  PacketQueue q(4, Backpressure::kLatest);
  for (int i = 0; i < 10; ++i) {
    ASSERT_EQ(q.Push(MakePkt(static_cast<uint8_t>(i))), Status::kOk);
  }
  ASSERT_EQ(q.size(), 4u);
  // Remaining packets are the last 4 (6,7,8,9).
  for (int i = 6; i < 10; ++i) {
    VideoPacket p;
    ASSERT_EQ(q.Pull(p, 0), Status::kOk);
    ASSERT_EQ(p.data[0], static_cast<uint8_t>(i));
  }
}

// --- kError: full ring rejects with kBackendUnavailable
// -----------------------
TEST(PacketQueueTest, ErrorReturnsBackpressureCode) {
  PacketQueue q(2, Backpressure::kError);
  ASSERT_EQ(q.Push(MakePkt(1)), Status::kOk);
  ASSERT_EQ(q.Push(MakePkt(2)), Status::kOk);
  ASSERT_EQ(q.Push(MakePkt(3)), Status::kBackendUnavailable);
}

// --- EOS: Pop returns kEos once drained --------------------------------------
TEST(PacketQueueTest, EosReportedAfterDrain) {
  PacketQueue q(4, Backpressure::kBlock);
  ASSERT_EQ(q.Push(MakePkt(1)), Status::kOk);
  q.MarkEos();

  VideoPacket p;
  ASSERT_EQ(q.Pull(p, 0), Status::kOk);
  ASSERT_EQ(p.data[0], 1);
  ASSERT_EQ(q.Pull(p, 0), Status::kEos);
}

}  // namespace
}  // namespace codec
}  // namespace video
