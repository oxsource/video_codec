// file_byte_sink.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "src/framework/io/byte_sink.h"

namespace video {
namespace codec {

// ByteSink backed by a FILE* opened for binary writing. Seekable, so it can
// back formats that need random access (e.g. MP4's moov-at-end layout).
class FileByteSink : public ByteSink {
 public:
  explicit FileByteSink(std::string path);
  ~FileByteSink() override;

  FileByteSink(const FileByteSink&) = delete;
  FileByteSink& operator=(const FileByteSink&) = delete;

  // True if the underlying file was opened successfully.
  bool IsOpen() const { return file_ != nullptr; }

  bool Write(const uint8_t* data, size_t size) override;
  bool Seek(int64_t pos) override;
  int64_t Tell() override;
  bool Flush() override;

 private:
  std::string path_;
  FILE* file_ = nullptr;
};

}  // namespace codec
}  // namespace video
