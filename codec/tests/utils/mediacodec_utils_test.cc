// mediacodec_utils_test.cc
//
// Host unit tests for the Android MediaCodec backend's pure helpers
// (backend/android/mediacodec_utils). No NDK dependency, so this runs on the
// host build (spec 006, contract C-050).

#include "mediacodec_utils.h"

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"
#include "types.h"

namespace video {
namespace codec {
namespace android {
namespace {

TEST(MediacodecUtilsTest, ColorFormatMapping) {
  EXPECT_EQ(ColorFormatFor(PixelFormat::kI420), 19);  // YUV420Planar
  EXPECT_EQ(ColorFormatFor(PixelFormat::kNV12), 21);  // YUV420SemiPlanar
  EXPECT_EQ(ColorFormatFor(PixelFormat::kRGBA), 0);   // unsupported
}

TEST(MediacodecUtilsTest, MimeMapping) {
  EXPECT_STREQ(MimeFor(VideoCodecType::kH264), "video/avc");
  EXPECT_STREQ(MimeFor(VideoCodecType::kHEVC), "video/hevc");
  EXPECT_STREQ(MimeFor(AudioCodecType::kAAC), "audio/mp4a-latm");
  EXPECT_EQ(MimeFor(AudioCodecType::kNone), nullptr);
  EXPECT_EQ(MimeFor(AudioCodecType::kOpus), nullptr);
}

TEST(MediacodecUtilsTest, BufferSizes) {
  EXPECT_EQ(BufferSizeFor(640, 480, PixelFormat::kI420), 640u * 480 * 3 / 2);
  EXPECT_EQ(BufferSizeFor(640, 480, PixelFormat::kNV12), 640u * 480 * 3 / 2);
  EXPECT_EQ(BufferSizeFor(16, 16, PixelFormat::kRGBA), 16u * 16 * 4);
  EXPECT_EQ(BufferSizeFor(0, 480, PixelFormat::kI420), 0u);
  EXPECT_EQ(BufferSizeFor(641, 480, PixelFormat::kI420), 0u);  // odd width
  EXPECT_EQ(BufferSizeFor(640, 481, PixelFormat::kI420), 0u);  // odd height
  EXPECT_EQ(BufferSizeFor(641, 480, PixelFormat::kNV12), 0u);  // odd width (NV12 too)
  EXPECT_EQ(BufferSizeFor(640, 481, PixelFormat::kNV12), 0u);  // odd height
}

TEST(MediacodecUtilsTest, AppendAnnexB) {
  const std::vector<uint8_t> sps = {0x67, 0x42, 0x00};
  const std::vector<uint8_t> pps = {0x68, 0xCE};
  const std::vector<uint8_t> payload = {0x65, 0x88};

  // Keyframe: SPS + PPS + payload, each start-coded.
  std::vector<uint8_t> out;
  AppendAnnexB(true, sps, pps, payload, &out);
  ASSERT_EQ(out.size(), (4 + sps.size()) + (4 + pps.size()) + (4 + payload.size()));
  EXPECT_EQ(out[0], 0);
  EXPECT_EQ(out[3], 1);
  EXPECT_EQ(out[4], 0x67);  // SPS NAL
  EXPECT_EQ(out[4 + sps.size() + 4], 0x68);  // PPS NAL

  // Non-keyframe: only the payload unit.
  out.clear();
  AppendAnnexB(false, sps, pps, payload, &out);
  ASSERT_EQ(out.size(), 4 + payload.size());
  EXPECT_EQ(out[4], 0x65);
}

TEST(MediacodecUtilsTest, SplitAnnexB) {
  const std::vector<uint8_t> stream = {0, 0, 0, 1, 0x67, 0x42, 0, 0, 0, 1, 0x68, 0xCE};
  std::vector<std::vector<uint8_t>> units;
  ASSERT_TRUE(SplitAnnexB(stream.data(), stream.size(), &units));
  ASSERT_EQ(units.size(), 2u);
  EXPECT_EQ(units[0], std::vector<uint8_t>({0x67, 0x42}));
  EXPECT_EQ(units[1], std::vector<uint8_t>({0x68, 0xCE}));
}

TEST(MediacodecUtilsTest, SplitAnnexBHandlesThreeByteStartCode) {
  const std::vector<uint8_t> stream = {0, 0, 1, 0x65, 0x88};
  std::vector<std::vector<uint8_t>> units;
  ASSERT_TRUE(SplitAnnexB(stream.data(), stream.size(), &units));
  ASSERT_EQ(units.size(), 1u);
  EXPECT_EQ(units[0], std::vector<uint8_t>({0x65, 0x88}));
}

TEST(MediacodecUtilsTest, SplitAnnexBRejectsNullOrEmptyInput) {
  std::vector<std::vector<uint8_t>> units;
  EXPECT_FALSE(SplitAnnexB(nullptr, 4, &units));
  EXPECT_TRUE(units.empty());
  EXPECT_FALSE(SplitAnnexB(nullptr, 0, &units));
  const std::vector<uint8_t> empty;
  EXPECT_FALSE(SplitAnnexB(empty.data(), 0, &units));
}

TEST(MediacodecUtilsTest, StartsWithNal) {
  // 4-byte and 3-byte start codes, then the SPS (type 7).
  const std::vector<uint8_t> sps4 = {0, 0, 0, 1, 0x67, 0x42};
  const std::vector<uint8_t> sps3 = {0, 0, 1, 0x67, 0x42};
  EXPECT_TRUE(StartsWithNal(sps4.data(), sps4.size(), 7));
  EXPECT_TRUE(StartsWithNal(sps3.data(), sps3.size(), 7));
  EXPECT_FALSE(StartsWithNal(sps4.data(), sps4.size(), 5));   // not an IDR
  EXPECT_FALSE(StartsWithNal(nullptr, 0, 7));                 // null/empty
  const std::vector<uint8_t> no_sc = {0x67, 0x42};            // no start code
  EXPECT_FALSE(StartsWithNal(no_sc.data(), no_sc.size(), 7));
}

TEST(MediacodecUtilsTest, BuildAudioSpecificConfig) {
  std::vector<uint8_t> asc;
  ASSERT_TRUE(BuildAudioSpecificConfig(48000, 2, &asc));
  // AAC-LC (AOT 2), freq index 3 (48 kHz), channel config 2 (stereo):
  // byte0 = (2<<3)|(3>>1) = 0x11, byte1 = ((3&1)<<7)|(2<<3) = 0x90.
  EXPECT_EQ(asc, std::vector<uint8_t>({0x11, 0x90}));

  // Unrepresentable rates / layouts are rejected.
  EXPECT_FALSE(BuildAudioSpecificConfig(47999, 2, &asc));
  EXPECT_FALSE(BuildAudioSpecificConfig(48000, 0, &asc));
  EXPECT_FALSE(BuildAudioSpecificConfig(48000, 8, &asc));
}

}  // namespace
}  // namespace android
}  // namespace codec
}  // namespace video
