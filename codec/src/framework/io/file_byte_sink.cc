// file_byte_sink.cc
#include "file_byte_sink.h"

namespace video {
namespace codec {

FileByteSink::FileByteSink(std::string path)
    : path_(std::move(path)), file_(std::fopen(path_.c_str(), "wb")) {}

FileByteSink::~FileByteSink() {
  if (file_) std::fclose(file_);
}

bool FileByteSink::Write(const uint8_t* data, size_t size) {
  return file_ != nullptr && std::fwrite(data, 1, size, file_) == size;
}

bool FileByteSink::Seek(int64_t pos) {
  return file_ != nullptr && std::fseek(file_, pos, SEEK_SET) == 0;
}

int64_t FileByteSink::Tell() {
  return file_ != nullptr ? static_cast<int64_t>(std::ftell(file_)) : -1;
}

bool FileByteSink::Flush() { return file_ != nullptr && std::fflush(file_) == 0; }

}  // namespace codec
}  // namespace video
