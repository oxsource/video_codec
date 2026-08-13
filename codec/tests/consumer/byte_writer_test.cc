// byte_writer_test.cc
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "io/file_writer.h"
#include "io/stream_writer.h"
#include "io/tee_writer.h"

namespace video {
namespace codec {
namespace {

// In-memory ByteWriter stub: records bytes and flush count.
class MemoryWriter : public ByteWriter {
 public:
  bool Write(const uint8_t* data, size_t size) override {
    buf_.insert(buf_.end(), data, data + size);
    return true;
  }
  bool Seek(int64_t) override { return false; }
  int64_t Tell() override { return -1; }
  bool Flush() override {
    ++flushed_;
    return true;
  }
  std::vector<uint8_t> buf_;
  int flushed_ = 0;
};

TEST(TeeWriterTest, FansWritesAndFlushesToAllWriters) {
  MemoryWriter a, b;
  TeeWriter tee({&a, &b});

  const uint8_t data[] = {1, 2, 3};
  EXPECT_TRUE(tee.Write(data, 3));
  EXPECT_TRUE(tee.Flush());

  EXPECT_EQ(a.buf_, std::vector<uint8_t>({1, 2, 3}));
  EXPECT_EQ(b.buf_, std::vector<uint8_t>({1, 2, 3}));
  EXPECT_EQ(a.flushed_, 1);
  EXPECT_EQ(b.flushed_, 1);
}

TEST(TeeWriterTest, PropagatesWriterFailure) {
  MemoryWriter ok;
  struct FailWriter : ByteWriter {
    bool Write(const uint8_t*, size_t) override { return false; }
    bool Flush() override { return false; }
  } fail;
  TeeWriter tee({&ok, &fail});
  EXPECT_FALSE(tee.Write(nullptr, 0));
  EXPECT_FALSE(tee.Flush());
}

TEST(StreamWriterTest, PushesViaCallbacksAndIsNonSeekable) {
  std::vector<uint8_t> buf;
  int flushed = 0;
  StreamWriter writer(
      [&buf](const uint8_t* data, size_t size) {
        buf.insert(buf.end(), data, data + size);
        return true;
      },
      [&flushed] {
        ++flushed;
        return true;
      });

  const uint8_t data[] = {9, 8};
  EXPECT_TRUE(writer.Write(data, 2));
  EXPECT_FALSE(writer.Seek(0));  // sequential only
  EXPECT_EQ(writer.Tell(), -1);
  EXPECT_TRUE(writer.Flush());

  EXPECT_EQ(buf, std::vector<uint8_t>({9, 8}));
  EXPECT_EQ(flushed, 1);
}

TEST(FileWriterTest, WritesFlushesAndTells) {
  const std::string path = "/tmp/vc_byte_writer_test.bin";
  std::remove(path.c_str());
  {
    FileWriter writer(path);
    ASSERT_TRUE(writer.IsOpen());
    const uint8_t data[] = {5, 6, 7};
    EXPECT_TRUE(writer.Write(data, 3));
    EXPECT_EQ(writer.Tell(), 3);
    EXPECT_TRUE(writer.Flush());
  }
  std::ifstream f(path, std::ios::binary);
  const std::vector<uint8_t> got((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
  EXPECT_EQ(got, std::vector<uint8_t>({5, 6, 7}));
  std::remove(path.c_str());
}

}  // namespace
}  // namespace codec
}  // namespace video
