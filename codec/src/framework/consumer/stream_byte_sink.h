// stream_byte_sink.h
#pragma once

#include <cstddef>
#include <cstdint>

#include "consumer/byte_stream.h"
#include "mux/byte_sink.h"

namespace video {
namespace codec {

// ByteSink that pushes bytes to a remote endpoint through a ByteStream.
// Non-seekable: only usable with sequential output (fragmented MP4).
class StreamByteSink : public ByteSink {
 public:
  // `stream` must outlive this sink.
  explicit StreamByteSink(ByteStream* stream) : stream_(stream) {}

  bool Write(const uint8_t* data, size_t size) override {
    return stream_->Write(data, size);
  }
  bool Seek(int64_t) override { return false; }  // sequential only
  int64_t Tell() override { return -1; }
  bool Flush() override { return stream_->Flush(); }

 private:
  ByteStream* stream_;
};

}  // namespace codec
}  // namespace video
