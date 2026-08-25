#include <gtest/gtest.h>

#include "src/api/stream.h"
#include "src/api/stream_config.h"
#include "src/api/stream_status.h"
#include "src/core/backend_registry.h"
#include "src/backend/mock/mock_backend.h"

namespace video {
namespace stream {
namespace {

class StreamInterfaceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    RegisterBackend("mock", [](const StreamConfig& config) {
      return std::make_unique<MockBackend>(config);
    });
  }
};

TEST_F(StreamInterfaceTest, CreateAndDestroy) {
  StreamConfig config;
  config.backend_type = "mock";
  config.remote_url = "http://localhost:8080/whip";

  auto stream = Stream::Create(config);
  EXPECT_NE(stream, nullptr);
}

TEST_F(StreamInterfaceTest, InitSuccess) {
  StreamConfig config;
  config.backend_type = "mock";
  config.remote_url = "http://localhost:8080/whip";

  auto stream = Stream::Create(config);
  ASSERT_NE(stream, nullptr);

  auto st = stream->Init();
  EXPECT_EQ(st, video::codec::Status::kOk);
}

TEST_F(StreamInterfaceTest, StartStop) {
  StreamConfig config;
  config.backend_type = "mock";
  config.remote_url = "http://localhost:8080/whip";

  auto stream = Stream::Create(config);
  ASSERT_NE(stream, nullptr);

  EXPECT_EQ(stream->Init(), video::codec::Status::kOk);
  EXPECT_EQ(stream->Start(), video::codec::Status::kOk);
  EXPECT_EQ(stream->Stop(), video::codec::Status::kOk);
}

TEST_F(StreamInterfaceTest, StartWithoutInitReturnsError) {
  StreamConfig config;
  config.backend_type = "mock";

  auto stream = Stream::Create(config);
  ASSERT_NE(stream, nullptr);

  EXPECT_EQ(stream->Start(), video::codec::Status::kNotInitialized);
}

TEST_F(StreamInterfaceTest, UnsupportedBackendReturnsError) {
  StreamConfig config;
  config.backend_type = "nonexistent";

  auto stream = Stream::Create(config);
  ASSERT_NE(stream, nullptr);

  EXPECT_EQ(stream->Init(), video::codec::Status::kBackendUnavailable);
}

TEST_F(StreamInterfaceTest, GetStatusReturnsCurrentState) {
  StreamConfig config;
  config.backend_type = "mock";

  auto stream = Stream::Create(config);
  ASSERT_NE(stream, nullptr);

  auto status = stream->GetStatus();
  EXPECT_EQ(status.state, StreamState::kCreated);
}

TEST_F(StreamInterfaceTest, StatusCallbackInvokedOnStateChange) {
  StreamConfig config;
  config.backend_type = "mock";

  auto stream = Stream::Create(config);
  ASSERT_NE(stream, nullptr);

  StreamStatus received;
  bool called = false;
  stream->SetStatusCallback([&](const StreamStatus& s) {
    received = s;
    called = true;
  });

  stream->Init();
  EXPECT_TRUE(called);
  EXPECT_EQ(received.state, StreamState::kConfigured);
}

TEST_F(StreamInterfaceTest, ReleaseTransitionsToDestroyed) {
  StreamConfig config;
  config.backend_type = "mock";

  auto stream = Stream::Create(config);
  ASSERT_NE(stream, nullptr);

  stream->Init();
  stream->Release();
  EXPECT_EQ(stream->GetStatus().state, StreamState::kDestroyed);
}

}  // namespace
}  // namespace stream
}  // namespace video