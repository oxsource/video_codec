// byte_sink_test.cc
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "consumer/byte_stream.h"
#include "consumer/file_byte_sink.h"
#include "consumer/stream_byte_sink.h"
#include "consumer/tee_byte_sink.h"
#include "gtest/gtest.h"

namespace video {
namespace codec {
namespace {

// In-memory ByteStream stub: records bytes and flush count.
class MemoryStream : public ByteStream {
 public:
  bool Write(const uint8_t* data, size_t size) override {
    buf_.insert(buf_.end(), data, data + size);
    return true;
  }
  bool Flush() override {
    ++flushed_;
    return true;
  }
  std::vector<uint8_t> buf_;
  int flushed_ = 0;
};

// In-memory ByteSink stub: records bytes and flush count.
class MemorySink : public ByteSink {
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

TEST(TeeByteSinkTest, FansWritesAndFlushesToAllSinks) {
  MemorySink a, b;
  TeeByteSink tee({&a, &b});

  const uint8_t data[] = {1, 2, 3};
  EXPECT_TRUE(tee.Write(data, 3));
  EXPECT_TRUE(tee.Flush());

  EXPECT_EQ(a.buf_, std::vector<uint8_t>({1, 2, 3}));
  EXPECT_EQ(b.buf_, std::vector<uint8_t>({1, 2, 3}));
  EXPECT_EQ(a.flushed_, 1);
  EXPECT_EQ(b.flushed_, 1);
}

TEST(TeeByteSinkTest, PropagatesSinkFailure) {
  MemorySink ok;
  struct FailSink : ByteSink {
    bool Write(const uint8_t*, size_t) override { return false; }
    bool Flush() override { return false; }
  } fail;
  TeeByteSink tee({&ok, &fail});
  EXPECT_FALSE(tee.Write(nullptr, 0));
  EXPECT_FALSE(tee.Flush());
}

TEST(StreamByteSinkTest, DelegatesAndIsNonSeekable) {
  MemoryStream stream;
  StreamByteSink sink(&stream);

  const uint8_t data[] = {9, 8};
  EXPECT_TRUE(sink.Write(data, 2));
  EXPECT_FALSE(sink.Seek(0));  // sequential only
  EXPECT_EQ(sink.Tell(), -1);
  EXPECT_TRUE(sink.Flush());

  EXPECT_EQ(stream.buf_, std::vector<uint8_t>({9, 8}));
  EXPECT_EQ(stream.flushed_, 1);
}

TEST(FileByteSinkTest, WritesFlushesAndTells) {
  const std::string path = "/tmp/vc_byte_sink_test.bin";
  std::remove(path.c_str());
  {
    FileByteSink sink(path);
    ASSERT_TRUE(sink.IsOpen());
    const uint8_t data[] = {5, 6, 7};
    EXPECT_TRUE(sink.Write(data, 3));
    EXPECT_EQ(sink.Tell(), 3);
    EXPECT_TRUE(sink.Flush());
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
