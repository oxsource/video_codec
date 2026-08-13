// umbrella_compile_test.cc
//
// Header-only compile/link test for the public surface (contract A1/A2):
// the translation unit includes ONLY the umbrella header and references the
// public contracts without touching any internal module.

#include <video_codec/video_codec.h>

#include "gtest/gtest.h"

namespace {

TEST(UmbrellaCompileTest, PublicSurfaceReachable) {
  // Core config structs are part of the public surface.
  video::codec::VideoEncoderConfig vcfg;
  vcfg.codec = video::codec::VideoCodecType::kH264;
  vcfg.width = 1280;
  vcfg.height = 720;
  vcfg.input_format = video::codec::PixelFormat::kI420;

  video::codec::AudioEncoderConfig acfg;
  acfg.codec = video::codec::AudioCodecType::kAAC;
  acfg.sample_rate = 48000;

  // LogSlot (pluggable logging) is exposed via the umbrella.
  video::codec::LogSlot* slot = nullptr;  // no-op default exists; null here
  (void)slot;

  // Factory entry points are declared through the umbrella.
  video::codec::Backend b =
      video::codec::ResolveBackend(video::codec::Backend::kFFmpeg);
  EXPECT_EQ(b, video::codec::Backend::kFFmpeg);

  EXPECT_EQ(vcfg.width, 1280);
  EXPECT_EQ(acfg.sample_rate, 48000);
}

}  // namespace
