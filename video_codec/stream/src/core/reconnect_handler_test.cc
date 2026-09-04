#include "stream/src/core/reconnect_handler.h"

#include <cstdint>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace video {
namespace stream {
namespace {

using ::testing::ElementsAre;

// Simulates the reconnect worker driving Tick() once per "second".
std::vector<uint32_t> FireTicks(ReconnectHandler* handler, uint32_t ticks) {
  std::vector<uint32_t> fired;
  uint32_t tick = 0;
  handler->OnDisconnected([&]() { fired.push_back(tick); });
  for (tick = 1; tick <= ticks; ++tick) {
    handler->Tick();
  }
  return fired;
}

TEST(ReconnectHandlerTest, FiresOnExponentialBackoff) {
  ReconnectHandler handler(/*max_interval_s=*/30);
  // 1s then 2s then 4s then 8s: fire on simulated ticks 1, 3, 7, 15.
  EXPECT_THAT(FireTicks(&handler, 16), ElementsAre(1, 3, 7, 15));
}

TEST(ReconnectHandlerTest, IntervalIsCappedAtMax) {
  ReconnectHandler handler(/*max_interval_s=*/3);
  auto fired = FireTicks(&handler, 60);
  ASSERT_GT(fired.size(), 5u);
  // Once capped, consecutive fires must not drift further apart than the cap.
  for (size_t i = 1; i < fired.size(); ++i) {
    EXPECT_LE(fired[i] - fired[i - 1], 3u + 1u);
  }
  EXPECT_LE(fired.back() - fired[fired.size() - 2], 3u + 1u);
}

TEST(ReconnectHandlerTest, OnConnectedStopsFiring) {
  ReconnectHandler handler;
  auto fired = FireTicks(&handler, 3);
  EXPECT_THAT(fired, ElementsAre(1, 3));

  handler.OnConnected();
  uint32_t tick = 4;
  uint32_t count_before = fired.size();
  for (; tick <= 12; ++tick) handler.Tick();
  EXPECT_EQ(fired.size(), count_before);
  EXPECT_FALSE(handler.IsReconnecting());
}

TEST(ReconnectHandlerTest, ResetStopsFiring) {
  ReconnectHandler handler;
  auto fired = FireTicks(&handler, 2);
  EXPECT_EQ(fired.size(), 1u);

  handler.Reset();
  uint32_t count_before = fired.size();
  for (uint32_t tick = 3; tick <= 20; ++tick) handler.Tick();
  EXPECT_EQ(fired.size(), count_before);
  EXPECT_FALSE(handler.IsReconnecting());
}

TEST(ReconnectHandlerTest, AttemptCounterGrowsPerFire) {
  ReconnectHandler handler;
  FireTicks(&handler, 8);
  // Fires at ticks 1, 3, 7 -> three attempts made.
  EXPECT_EQ(handler.CurrentAttempt(), 3u);
}

}  // namespace
}  // namespace stream
}  // namespace video
