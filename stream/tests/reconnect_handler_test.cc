#include <gtest/gtest.h>

#include "src/core/reconnect_handler.h"

namespace video {
namespace stream {
namespace {

TEST(ReconnectHandlerTest, StartsNotReconnecting) {
  ReconnectHandler handler;
  EXPECT_FALSE(handler.IsReconnecting());
  EXPECT_EQ(handler.CurrentAttempt(), 0);
}

TEST(ReconnectHandlerTest, OnDisconnectedStartsReconnecting) {
  ReconnectHandler handler;
  handler.OnDisconnected([](){});
  EXPECT_TRUE(handler.IsReconnecting());
}

TEST(ReconnectHandlerTest, OnConnectedStopsReconnecting) {
  ReconnectHandler handler;
  handler.OnDisconnected([](){});
  handler.OnConnected();
  EXPECT_FALSE(handler.IsReconnecting());
}

TEST(ReconnectHandlerTest, ExponentialBackoff) {
  ReconnectHandler handler(30, 30);
  handler.OnDisconnected([](){});
  EXPECT_EQ(handler.NextIntervalMs(), 1000);
  handler.Tick();
  EXPECT_EQ(handler.NextIntervalMs(), 2000);
  handler.Tick();
  EXPECT_EQ(handler.NextIntervalMs(), 4000);
  handler.Tick();
  EXPECT_EQ(handler.NextIntervalMs(), 8000);
}

TEST(ReconnectHandlerTest, BackoffCappedAtMax) {
  ReconnectHandler handler(5, 30);
  handler.OnDisconnected([](){});
  for (int i = 0; i < 10; i++) {
    handler.Tick();
  }
  EXPECT_LE(handler.NextIntervalMs(), 5000);
}

TEST(ReconnectHandlerTest, ResetClearsState) {
  ReconnectHandler handler;
  handler.OnDisconnected([](){});
  EXPECT_TRUE(handler.IsReconnecting());
  handler.Reset();
  EXPECT_FALSE(handler.IsReconnecting());
  EXPECT_EQ(handler.CurrentAttempt(), 0);
}

TEST(ReconnectHandlerTest, CallsCallbackOnTick) {
  ReconnectHandler handler(1, 30);
  bool called = false;
  handler.OnDisconnected([&]() { called = true; });
  EXPECT_FALSE(called);

  for (int i = 0; i < 10; i++) {
    handler.Tick();
    if (called) break;
  }
  EXPECT_TRUE(called);
}

}  // namespace
}  // namespace stream
}  // namespace video