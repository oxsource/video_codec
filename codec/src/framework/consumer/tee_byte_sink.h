// tee_byte_sink.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mux/byte_sink.h"

namespace video {
namespace codec {

// ByteSink that fans every write/flush to N underlying sinks (e.g. file +
// cloud simultaneously). The sinks are non-owning and must outlive the tee.
// Used with sequential (fragmented) output so seeking is avoided; Seek/Tell
// succeed only when ALL sinks support them.
class TeeByteSink : public ByteSink {
 public:
  explicit TeeByteSink(std::vector<ByteSink*> sinks)
      : sinks_(std::move(sinks)) {}

  bool Write(const uint8_t* data, size_t size) override {
    for (ByteSink* s : sinks_) {
      if (!s->Write(data, size)) return false;
    }
    return true;
  }

  bool Seek(int64_t pos) override {
    for (ByteSink* s : sinks_) {
      if (!s->Seek(pos)) return false;
    }
    return true;
  }

  int64_t Tell() override {
    int64_t pos = -1;
    for (ByteSink* s : sinks_) {
      const int64_t p = s->Tell();
      if (p < 0) return -1;
      if (pos < 0) pos = p;
    }
    return pos;
  }

  bool Flush() override {
    for (ByteSink* s : sinks_) {
      if (!s->Flush()) return false;
    }
    return true;
  }

 private:
  std::vector<ByteSink*> sinks_;
};

}  // namespace codec
}  // namespace video
