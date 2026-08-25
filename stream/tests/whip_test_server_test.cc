#include <gtest/gtest.h>

#include <thread>

#include "src/test_server/whip_test_server.h"

namespace video {
namespace stream {
namespace {

class WhipTestServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_ = std::make_unique<WhipTestServer>(0);
    server_->Start();
  }

  void TearDown() override {
    server_->Stop();
    server_.reset();
  }

  std::unique_ptr<WhipTestServer> server_;
};

TEST_F(WhipTestServerTest, ServerStarts) {
  EXPECT_GT(server_->Port(), 0);
}

TEST_F(WhipTestServerTest, SessionCreatedCallback) {
  bool called = false;
  server_->SetOnSessionCreated([&](const std::string&) {
    called = true;
  });
  EXPECT_FALSE(called);
}

TEST_F(WhipTestServerTest, StartStopRestart) {
  server_->Stop();
  EXPECT_TRUE(server_->Start());
  server_->Stop();
  EXPECT_TRUE(server_->Start());
}

}  // namespace
}  // namespace stream
}  // namespace video