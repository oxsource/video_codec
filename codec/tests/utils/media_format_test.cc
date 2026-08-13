// media_format_test.cc
#include "media_format.h"

#include "gtest/gtest.h"

namespace video {
namespace codec {
namespace utils {
namespace {

TEST(MediaFormatTest, ConstantsMatchSuffix) {
  EXPECT_EQ(MediaFormat::kMp4, ".mp4");
  EXPECT_EQ(MediaFormat::kMp3, ".mp3");
  EXPECT_EQ(MediaFormat::kH264, ".h264");
  EXPECT_EQ(MediaFormat::kAac, ".aac");
}

TEST(MediaFormatTest, HasExtensionExact) {
  EXPECT_TRUE(MediaFormat::HasExtension("out.mp4", MediaFormat::kMp4));
  EXPECT_TRUE(MediaFormat::HasExtension("/tmp/dir/clip.h264", MediaFormat::kH264));
  EXPECT_FALSE(MediaFormat::HasExtension("out.mp4", MediaFormat::kMp3));
  EXPECT_FALSE(MediaFormat::HasExtension("video.mp4x", MediaFormat::kMp4));
}

TEST(MediaFormatTest, HasExtensionIgnoresCase) {
  EXPECT_TRUE(MediaFormat::HasExtension("OUT.MP4", MediaFormat::kMp4));
  EXPECT_TRUE(MediaFormat::HasExtension("Clip.H264", MediaFormat::kH264));
}

TEST(MediaFormatTest, HasExtensionEdgeCases) {
  EXPECT_FALSE(MediaFormat::HasExtension("mp4", MediaFormat::kMp4));
  EXPECT_FALSE(MediaFormat::HasExtension("", MediaFormat::kMp4));
  EXPECT_FALSE(MediaFormat::HasExtension("out.m4v", MediaFormat::kMp4));
  // Extension must be the suffix, not a substring elsewhere.
  EXPECT_FALSE(MediaFormat::HasExtension("x.mp4.old", MediaFormat::kMp4));
}

}  // namespace
}  // namespace utils
}  // namespace codec
}  // namespace video
