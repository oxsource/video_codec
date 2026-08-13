// muxer_contract_test.cc
#include "api/encoder_factory.h"
#include "api/muxer.h"
#include "gtest/gtest.h"
#include "io/byte_sink.h"

namespace video {
namespace codec {
namespace {

// Minimal Muxer implementation exercising the api contract: requires a
// ByteSink (via SetOutput) before the first Push; forwards Push data to the
// sink; Finish/Release are idempotent.
class StubMuxer : public Muxer {
 public:
  using Muxer::Push;  // keep both Push overloads visible (name hiding)

  Status SetOutput(ByteSink* sink) override {
    sink_ = sink;
    return Status::kOk;
  }

  Status Push(VideoPacket&& pkt) override {
    if (!sink_) return Status::kInvalidArgument;
    pushed_ += pkt.data.size();
    return sink_->Write(pkt.data.data(), pkt.data.size())
               ? Status::kOk
               : Status::kEncodeFailed;
  }

  Status Flush() override { return Status::kOk; }

  Status Finish() override {
    finished_ = true;
    return Status::kOk;
  }

  void Release() override { released_ = true; }

  ByteSink* sink_ = nullptr;
  size_t pushed_ = 0;
  bool finished_ = false;
  bool released_ = false;
};

// A trivial in-memory ByteSink for the contract test.
class MemorySink : public ByteSink {
 public:
  bool Write(const uint8_t* data, size_t size) override {
    bytes_.insert(bytes_.end(), data, data + size);
    return true;
  }
  std::vector<uint8_t> bytes_;
};

TEST(MuxerContractTest, AudioPushIsUnsupportedByDefault) {
  StubMuxer muxer;
  AudioPacket ap;
  EXPECT_EQ(muxer.Push(std::move(ap)), Status::kUnsupportedOperation);
}

TEST(MuxerContractTest, PushRequiresSetOutputFirst) {
  StubMuxer muxer;
  VideoPacket vp;
  vp.data = {1, 2, 3};
  EXPECT_EQ(muxer.Push(std::move(vp)), Status::kInvalidArgument);
}

TEST(MuxerContractTest, PushAfterSetOutputWritesToSink) {
  StubMuxer muxer;
  MemorySink sink;
  ASSERT_EQ(muxer.SetOutput(&sink), Status::kOk);

  VideoPacket vp;
  vp.data = {0x66, 0x74, 0x79, 0x70};  // "ftyp"
  ASSERT_EQ(muxer.Push(std::move(vp)), Status::kOk);
  EXPECT_EQ(muxer.pushed_, 4u);
  ASSERT_EQ(sink.bytes_, std::vector<uint8_t>({0x66, 0x74, 0x79, 0x70}));
}

TEST(MuxerContractTest, FinishAndReleaseAreIdempotent) {
  StubMuxer muxer;
  MemorySink sink;
  ASSERT_EQ(muxer.SetOutput(&sink), Status::kOk);

  EXPECT_EQ(muxer.Finish(), Status::kOk);
  EXPECT_EQ(muxer.Finish(), Status::kOk);
  EXPECT_TRUE(muxer.finished_);

  muxer.Release();
  muxer.Release();
  EXPECT_TRUE(muxer.released_);
}

// US2: the factory resolves the backend from the config. A backend that
// registered a muxer returns an instance; an unregistered backend returns
// nullptr (no fabricated/damaged output).
TEST(MuxerContractTest, CreateMuxerResolvesRegisteredBackend) {
  // Register a stub muxer under a specific backend key, then ask for it.
  // This validates the registration/selection mechanism itself without
  // depending on the real FFmpeg backend (contract test links api only).
  RegisterMuxer(Backend::kDarwin, [](const MuxerConfig&) {
    return std::make_unique<StubMuxer>();
  });

  MuxerConfig cfg;
  cfg.format = MuxFormat::kMp4;
  cfg.fragmented = true;
  cfg.width = 320;
  cfg.height = 240;
  cfg.fps = 30;
  cfg.backend = Backend::kDarwin;
  std::unique_ptr<Muxer> muxer = CreateMuxer(cfg);
  EXPECT_NE(muxer, nullptr);
}

TEST(MuxerContractTest, CreateMuxerReturnsNullptrForUnregisteredBackend) {
  MuxerConfig cfg;
  cfg.format = MuxFormat::kMp4;
  cfg.fragmented = true;
  cfg.width = 320;
  cfg.height = 240;
  cfg.fps = 30;
  cfg.backend = Backend::kAndroid;  // not registered on this host
  std::unique_ptr<Muxer> muxer = CreateMuxer(cfg);
  EXPECT_EQ(muxer, nullptr);
}

}  // namespace
}  // namespace codec
}  // namespace video
