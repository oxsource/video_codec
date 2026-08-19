// tee_byte_sink.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "src/framework/io/byte_sink.h"

namespace video {
namespace codec {

// ByteSink that fans every write/flush to N underlying writers (e.g. file +
// cloud simultaneously). The writers are non-owning and must outlive the tee.
// Used with sequential (fragmented) output so seeking is avoided; Seek/Tell
// succeed only when ALL writers support them.
class TeeByteSink : public ByteSink {
 public:
  explicit TeeByteSink(std::vector<ByteSink*> writers) : writers_(std::move(writers)) {}

  bool Write(const uint8_t* data, size_t size) override {
    for (ByteSink* w : writers_) {
      if (!w->Write(data, size)) return false;
    }
    return true;
  }

  bool Seek(int64_t pos) override {
    for (ByteSink* w : writers_) {
      if (!w->Seek(pos)) return false;
    }
    return true;
  }

  int64_t Tell() override {
    int64_t pos = -1;
    for (ByteSink* w : writers_) {
      const int64_t p = w->Tell();
      if (p < 0) return -1;
      if (pos < 0) pos = p;
    }
    return pos;
  }

  bool Flush() override {
    for (ByteSink* w : writers_) {
      if (!w->Flush()) return false;
    }
    return true;
  }

 private:
  std::vector<ByteSink*> writers_;
};

}  // namespace codec
}  // namespace video
