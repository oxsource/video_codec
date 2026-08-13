// byte_stream.h
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/status.h"

namespace video {
namespace codec {

// Transport for pushing bytes to a remote endpoint (cloud streaming / RTMP /
// SRT / HTTP upload). Implementations own the connection; the muxer's output
// is written here fragment by fragment. The stream is sequential — no seeking
// — so the source must be a fragmented (fMP4) muxer.
class ByteStream {
 public:
  virtual ~ByteStream() = default;

  // Push `size` bytes to the remote endpoint. Returns false on failure.
  virtual bool Write(const uint8_t* data, size_t size) = 0;

  // Commit/upload any buffered data (called at each fragment boundary).
  // Returns false on failure.
  virtual bool Flush() = 0;
};

}  // namespace codec
}  // namespace video
