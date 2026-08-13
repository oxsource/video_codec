// encoder_lifecycle_test.cc
#include "api/encoder_lifecycle.h"

#include "api/audio_encoder.h"
#include "api/codec_factory.h"
#include "api/video_encoder.h"
#include "gtest/gtest.h"

namespace video {
namespace codec {
namespace {

// --- Lifecycle state machine (data-model.md §5) ------------------------------
TEST(EncoderLifecycleTest, Created) {
  EncoderLifecycle lc;
  ASSERT_EQ(lc.state(), EncoderLifecycle::State::kCreated);
  // Invalid transitions from Created (must Init first).
  EXPECT_EQ(lc.Encode(), Status::kNotInitialized);
  EXPECT_EQ(lc.Flush(), Status::kNotInitialized);
  EXPECT_EQ(lc.Init(), Status::kOk);
  EXPECT_EQ(lc.state(), EncoderLifecycle::State::kInitialized);
  EXPECT_EQ(lc.Release(), Status::kOk);
  EXPECT_EQ(lc.state(), EncoderLifecycle::State::kReleased);
}

TEST(EncoderLifecycleTest, Initialized) {
  EncoderLifecycle lc;
  ASSERT_EQ(lc.Init(), Status::kOk);
  EXPECT_EQ(lc.Init(), Status::kInvalidArgument);  // already initialized
  EXPECT_EQ(lc.Encode(), Status::kOk);
  EXPECT_EQ(lc.state(), EncoderLifecycle::State::kEncoding);
  EXPECT_EQ(lc.Encode(), Status::kOk);  // Encoding -> Encoding
  EXPECT_EQ(lc.Flush(), Status::kOk);
  EXPECT_EQ(lc.state(), EncoderLifecycle::State::kFlushed);
}

TEST(EncoderLifecycleTest, FlushedReuse) {
  EncoderLifecycle lc;
  ASSERT_EQ(lc.Init(), Status::kOk);
  ASSERT_EQ(lc.Encode(), Status::kOk);
  ASSERT_EQ(lc.Flush(), Status::kOk);
  EXPECT_EQ(lc.Encode(), Status::kNotInitialized);  // must Init again
  EXPECT_EQ(lc.Init(), Status::kOk);                // reuse from Flushed
  EXPECT_EQ(lc.state(), EncoderLifecycle::State::kInitialized);
}

TEST(EncoderLifecycleTest, ReleasedIsTerminal) {
  EncoderLifecycle lc;
  ASSERT_EQ(lc.Init(), Status::kOk);
  ASSERT_EQ(lc.Release(), Status::kOk);
  EXPECT_EQ(lc.Init(), Status::kInvalidArgument);
  EXPECT_EQ(lc.Encode(), Status::kNotInitialized);
  EXPECT_EQ(lc.Flush(), Status::kNotInitialized);
  EXPECT_EQ(lc.Release(), Status::kOk);  // idempotent
}

// --- Backend selection model (data-model.md §6) ------------------------------
TEST(CodecFactoryTest, ResolveBackend) {
  // On a non-Android host, kAuto selects FFmpeg (Apple falls back, ADR-004).
  EXPECT_EQ(CodecFactory::ResolveBackend(Backend::kAuto), Backend::kFFmpeg);
  // An explicit force is honored regardless of platform.
  EXPECT_EQ(CodecFactory::ResolveBackend(Backend::kAndroid), Backend::kAndroid);
  EXPECT_EQ(CodecFactory::ResolveBackend(Backend::kFFmpeg), Backend::kFFmpeg);
  EXPECT_EQ(CodecFactory::ResolveBackend(Backend::kDarwin), Backend::kDarwin);
}

TEST(CodecFactoryTest, NoBackendLinkedReturnsNull) {
  // This test does not link any backend, so the registry is empty: Create must
  // fail gracefully (nullptr) instead of throwing.
  VideoConfig vc;
  EXPECT_EQ(CodecFactory::CreateVideo(vc), nullptr);
  AudioConfig ac;
  EXPECT_EQ(CodecFactory::CreateAudio(ac), nullptr);
}

}  // namespace
}  // namespace codec
}  // namespace video
