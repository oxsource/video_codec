// encoder_lifecycle_test.cc
#include "api/audio_encoder.h"
#include "api/encoder_factory.h"
#include "api/encoder_lifecycle.h"
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
  EXPECT_EQ(lc.Encode(), StatusCode::kNotInitialized);
  EXPECT_EQ(lc.Flush(), StatusCode::kNotInitialized);
  EXPECT_EQ(lc.Init(), StatusCode::kOk);
  EXPECT_EQ(lc.state(), EncoderLifecycle::State::kInitialized);
  EXPECT_EQ(lc.Release(), StatusCode::kOk);
  EXPECT_EQ(lc.state(), EncoderLifecycle::State::kReleased);
}

TEST(EncoderLifecycleTest, Initialized) {
  EncoderLifecycle lc;
  ASSERT_EQ(lc.Init(), StatusCode::kOk);
  EXPECT_EQ(lc.Init(), StatusCode::kInvalidArgument);  // already initialized
  EXPECT_EQ(lc.Encode(), StatusCode::kOk);
  EXPECT_EQ(lc.state(), EncoderLifecycle::State::kEncoding);
  EXPECT_EQ(lc.Encode(), StatusCode::kOk);  // Encoding -> Encoding
  EXPECT_EQ(lc.Flush(), StatusCode::kOk);
  EXPECT_EQ(lc.state(), EncoderLifecycle::State::kFlushed);
}

TEST(EncoderLifecycleTest, FlushedReuse) {
  EncoderLifecycle lc;
  ASSERT_EQ(lc.Init(), StatusCode::kOk);
  ASSERT_EQ(lc.Encode(), StatusCode::kOk);
  ASSERT_EQ(lc.Flush(), StatusCode::kOk);
  EXPECT_EQ(lc.Encode(), StatusCode::kNotInitialized);  // must Init again
  EXPECT_EQ(lc.Init(), StatusCode::kOk);               // reuse from Flushed
  EXPECT_EQ(lc.state(), EncoderLifecycle::State::kInitialized);
}

TEST(EncoderLifecycleTest, ReleasedIsTerminal) {
  EncoderLifecycle lc;
  ASSERT_EQ(lc.Init(), StatusCode::kOk);
  ASSERT_EQ(lc.Release(), StatusCode::kOk);
  EXPECT_EQ(lc.Init(), StatusCode::kInvalidArgument);
  EXPECT_EQ(lc.Encode(), StatusCode::kNotInitialized);
  EXPECT_EQ(lc.Flush(), StatusCode::kNotInitialized);
  EXPECT_EQ(lc.Release(), StatusCode::kOk);  // idempotent
}

// --- Backend selection model (data-model.md §6) ------------------------------
TEST(EncoderFactoryTest, ResolveBackend) {
  // On a non-Android host, kAuto selects FFmpeg (Apple falls back, ADR-004).
  EXPECT_EQ(ResolveBackend(Backend::kAuto), Backend::kFFmpeg);
  // An explicit force is honored regardless of platform.
  EXPECT_EQ(ResolveBackend(Backend::kAndroid), Backend::kAndroid);
  EXPECT_EQ(ResolveBackend(Backend::kFFmpeg), Backend::kFFmpeg);
  EXPECT_EQ(ResolveBackend(Backend::kDarwin), Backend::kDarwin);
}

TEST(EncoderFactoryTest, NoBackendLinkedReturnsNull) {
  // This test does not link any backend, so the registry is empty: Create must
  // fail gracefully (nullptr) instead of throwing.
  VideoEncoderConfig vc;
  EXPECT_EQ(CreateVideoEncoder(vc), nullptr);
  AudioEncoderConfig ac;
  EXPECT_EQ(CreateAudioEncoder(ac), nullptr);
}

}  // namespace
}  // namespace codec
}  // namespace video
