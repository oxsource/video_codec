#include <gtest/gtest.h>

#include "src/api/stream.h"
#include "src/api/stream_config.h"
#include "src/api/stream_status.h"
#include "src/core/backend_registry.h"
#include "src/core/video_stream_register.h"
#include "src/backend/mock/mock_backend.h"

namespace video {
namespace stream {
namespace {

class BackendSelectionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    RegisterBackend("mock", [](const StreamConfig& config) {
      return std::make_unique<MockBackend>(config);
    });
  }
};

TEST_F(BackendSelectionTest, ValidBackendInstantiates) {
  StreamConfig config;
  config.backend_type = "mock";
  config.remote_url = "http://localhost:8080/whip";

  auto stream = Stream::Create(config);
  ASSERT_NE(stream, nullptr);
  EXPECT_EQ(stream->Init(), video::codec::Status::kOk);
  EXPECT_EQ(stream->GetStatus().state, StreamState::kConfigured);
}

TEST_F(BackendSelectionTest, UnsupportedBackendReturnsError) {
  StreamConfig config;
  config.backend_type = "nonexistent";

  auto stream = Stream::Create(config);
  ASSERT_NE(stream, nullptr);
  EXPECT_EQ(stream->Init(), video::codec::Status::kBackendUnavailable);
  EXPECT_EQ(stream->GetStatus().state, StreamState::kDisconnected);
}

TEST_F(BackendSelectionTest, EmptyBackendTypeReturnsInvalidArgument) {
  StreamConfig config;

  auto stream = Stream::Create(config);
  ASSERT_NE(stream, nullptr);
  EXPECT_EQ(stream->Init(), video::codec::Status::kInvalidArgument);
}

TEST_F(BackendSelectionTest, InvalidTransitionStartBeforeInit) {
  StreamConfig config;
  config.backend_type = "mock";

  auto stream = Stream::Create(config);
  ASSERT_NE(stream, nullptr);
  EXPECT_EQ(stream->Start(), video::codec::Status::kNotInitialized);
}

TEST_F(BackendSelectionTest, SendVideoBeforeStartReturnsError) {
  StreamConfig config;
  config.backend_type = "mock";

  auto stream = Stream::Create(config);
  ASSERT_NE(stream, nullptr);
  stream->Init();

  video::codec::VideoPacket packet;
  EXPECT_NE(stream->SendVideo(packet), video::codec::Status::kOk);
}

TEST_F(BackendSelectionTest, FullLifecycleSucceeds) {
  StreamConfig config;
  config.backend_type = "mock";

  auto stream = Stream::Create(config);
  ASSERT_NE(stream, nullptr);

  EXPECT_EQ(stream->Init(), video::codec::Status::kOk);
  EXPECT_EQ(stream->GetStatus().state, StreamState::kConfigured);

  EXPECT_EQ(stream->Start(), video::codec::Status::kOk);
  EXPECT_EQ(stream->GetStatus().state, StreamState::kStreaming);

  video::codec::VideoPacket vpkt;
  EXPECT_EQ(stream->SendVideo(vpkt), video::codec::Status::kOk);

  video::codec::AudioPacket apkt;
  EXPECT_EQ(stream->SendAudio(apkt), video::codec::Status::kOk);

  EXPECT_EQ(stream->Stop(), video::codec::Status::kOk);
  EXPECT_EQ(stream->GetStatus().state, StreamState::kDisconnected);

  stream->Release();
  EXPECT_EQ(stream->GetStatus().state, StreamState::kDestroyed);
}

TEST_F(BackendSelectionTest, DoubleInitIsIdempotent) {
  StreamConfig config;
  config.backend_type = "mock";

  auto stream = Stream::Create(config);
  ASSERT_NE(stream, nullptr);

  EXPECT_EQ(stream->Init(), video::codec::Status::kOk);
  EXPECT_EQ(stream->Init(), video::codec::Status::kOk);
}

TEST_F(BackendSelectionTest, ErrorMessageOnBackendFailure) {
  StreamConfig config;
  config.backend_type = "nonexistent";

  auto stream = Stream::Create(config);
  ASSERT_NE(stream, nullptr);
  stream->Init();

  auto status = stream->GetStatus();
  EXPECT_FALSE(status.last_error.empty());
}

TEST_F(BackendSelectionTest, RegisterMacroCompiles) {
  VIDEO_STREAM_REGISTER("test_macro", [](const StreamConfig&) {
    return std::unique_ptr<StreamBackend>(nullptr);
  });
  SUCCEED();
}

}  // namespace
}  // namespace stream
}  // namespace video