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
  video::codec::VideoConfig vcfg;
  vcfg.codec = video::codec::VideoCodecType::kH264;
  vcfg.width = 1280;
  vcfg.height = 720;
  vcfg.input_format = video::codec::PixelFormat::kI420;

  video::codec::AudioConfig acfg;
  acfg.codec = video::codec::AudioCodecType::kAAC;
  acfg.sample_rate = 48000;

  // LogSlot (pluggable logging) is exposed via the umbrella.
  video::codec::LogSlot* slot = nullptr;  // no-op default exists; null here
  (void)slot;

  // Factory entry points are declared through the umbrella.
  video::codec::Backend b =
      video::codec::CodecFactory::ResolveBackend(video::codec::Backend::kFFmpeg);
  EXPECT_EQ(b, video::codec::Backend::kFFmpeg);

  EXPECT_EQ(vcfg.width, 1280);
  EXPECT_EQ(acfg.sample_rate, 48000);
}

TEST(UmbrellaCompileTest, IoByteSinksReachableThroughUmbrella) {
  // ByteSink / FileByteSink (the Muxer output target) are re-exported by the
  // umbrella so consumers can wire a file output without internal headers
  // (dependency-contract D-1). Construction + base-class dispatch must compile
  // and link from the single umbrella include.
  video::codec::FileByteSink sink("/nonexistent_media_record_dir/out.bin");
  EXPECT_FALSE(sink.IsOpen());  // bogus path -> open fails

  video::codec::ByteSink* base = &sink;
  EXPECT_FALSE(base->Write(reinterpret_cast<const uint8_t*>("x"), 1));
  EXPECT_EQ(base->Tell(), -1);
}

}  // namespace
