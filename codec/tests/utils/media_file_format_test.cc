// media_file_format_test.cc
#include "utils/media_file_format.h"

#include "gtest/gtest.h"

namespace video {
namespace codec {
namespace utils {
namespace {

TEST(MediaFileFormatTest, ConstantsMatchSuffix) {
  EXPECT_EQ(MediaFileFormat::kMp4, ".mp4");
  EXPECT_EQ(MediaFileFormat::kMp3, ".mp3");
  EXPECT_EQ(MediaFileFormat::kH264, ".h264");
  EXPECT_EQ(MediaFileFormat::kAac, ".aac");
}

TEST(MediaFileFormatTest, HasExtensionExact) {
  EXPECT_TRUE(MediaFileFormat::HasExtension("out.mp4", MediaFileFormat::kMp4));
  EXPECT_TRUE(MediaFileFormat::HasExtension("/tmp/dir/clip.h264", MediaFileFormat::kH264));
  EXPECT_FALSE(MediaFileFormat::HasExtension("out.mp4", MediaFileFormat::kMp3));
  EXPECT_FALSE(MediaFileFormat::HasExtension("video.mp4x", MediaFileFormat::kMp4));
}

TEST(MediaFileFormatTest, HasExtensionIgnoresCase) {
  EXPECT_TRUE(MediaFileFormat::HasExtension("OUT.MP4", MediaFileFormat::kMp4));
  EXPECT_TRUE(MediaFileFormat::HasExtension("Clip.H264", MediaFileFormat::kH264));
}

TEST(MediaFileFormatTest, HasExtensionEdgeCases) {
  EXPECT_FALSE(MediaFileFormat::HasExtension("mp4", MediaFileFormat::kMp4));
  EXPECT_FALSE(MediaFileFormat::HasExtension("", MediaFileFormat::kMp4));
  EXPECT_FALSE(MediaFileFormat::HasExtension("out.m4v", MediaFileFormat::kMp4));
  // Extension must be the suffix, not a substring elsewhere.
  EXPECT_FALSE(MediaFileFormat::HasExtension("x.mp4.old", MediaFileFormat::kMp4));
}

}  // namespace
}  // namespace utils
}  // namespace codec
}  // namespace video
