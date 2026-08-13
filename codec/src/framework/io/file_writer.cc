// file_writer.cc
#include "io/file_writer.h"

namespace video {
namespace codec {

FileWriter::FileWriter(std::string path)
    : path_(std::move(path)), file_(std::fopen(path_.c_str(), "wb")) {}

FileWriter::~FileWriter() {
  if (file_) std::fclose(file_);
}

bool FileWriter::Write(const uint8_t* data, size_t size) {
  return file_ != nullptr && std::fwrite(data, 1, size, file_) == size;
}

bool FileWriter::Seek(int64_t pos) {
  return file_ != nullptr && std::fseek(file_, pos, SEEK_SET) == 0;
}

int64_t FileWriter::Tell() {
  return file_ != nullptr ? static_cast<int64_t>(std::ftell(file_)) : -1;
}

bool FileWriter::Flush() { return file_ != nullptr && std::fflush(file_) == 0; }

}  // namespace codec
}  // namespace video
