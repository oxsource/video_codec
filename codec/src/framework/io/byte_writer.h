// byte_writer.h
#pragma once

#include <cstddef>
#include <cstdint>

namespace video {
namespace codec {

// Destination for raw bytes produced by a muxer or other binary writer.
// Implementations own their backing store (file, memory buffer, socket, ...).
// Writing is always sequential (append). Seek/Tell are OPTIONAL capabilities:
// seekable writers (e.g. FileWriter) override them for random access, while
// sequential writers (e.g. StreamWriter) leave them unsupported.
class ByteWriter {
 public:
  virtual ~ByteWriter() = default;

  // Append `size` bytes. Returns false on failure.
  virtual bool Write(const uint8_t* data, size_t size) = 0;

  // Move the write position to an absolute offset. Returns false if the writer
  // does not support seeking (e.g. a network stream).
  virtual bool Seek(int64_t pos) { return false; }

  // Current absolute write position, or -1 if the writer is not seekable.
  virtual int64_t Tell() { return -1; }

  // Flush buffered data to the backing store. Returns false on failure.
  virtual bool Flush() { return true; }
};

}  // namespace codec
}  // namespace video
