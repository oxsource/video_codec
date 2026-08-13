// stream_byte_sink.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "io/byte_sink.h"

namespace video {
namespace codec {

// ByteSink that pushes bytes to a remote endpoint (cloud streaming / RTMP /
// SRT / HTTP upload) through caller-supplied write/flush callbacks. There is
// no separate transport interface: a network output is just a ByteSink whose
// Seek/Tell are unsupported, so the source must be a fragmented (fMP4) muxer.
class StreamByteSink : public ByteSink {
 public:
  // `write` pushes bytes to the endpoint; `flush` commits/uploads any buffered
  // data (called at each fragment boundary). Both must return true on success.
  using WriteFn = std::function<bool(const uint8_t*, size_t)>;
  using FlushFn = std::function<bool()>;

  StreamByteSink(WriteFn write, FlushFn flush)
      : write_(std::move(write)), flush_(std::move(flush)) {}

  bool Write(const uint8_t* data, size_t size) override { return write_(data, size); }
  bool Seek(int64_t) override { return false; }  // sequential only
  int64_t Tell() override { return -1; }
  bool Flush() override { return flush_(); }

 private:
  WriteFn write_;
  FlushFn flush_;
};

}  // namespace codec
}  // namespace video
