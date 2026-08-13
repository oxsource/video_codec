// result.h
#pragma once

#include <utility>

#include "core/status.h"

namespace video {
namespace codec {

// A value-or-error transport used by fallible operations that produce a value
// (e.g. Encode() -> Result<EncodedPacket>). Never throws across the boundary.
template <typename T>
class Result {
 public:
  Result() : status_(StatusCode::kOk), has_value_(false) {}

  static Result Ok(T v) {
    Result r;
    r.value_ = std::move(v);
    r.status_ = StatusCode::kOk;
    r.has_value_ = true;
    return r;
  }

  static Result Error(StatusCode c) {
    Result r;
    r.status_ = c;
    r.has_value_ = false;
    return r;
  }

  bool ok() const { return status_ == StatusCode::kOk; }
  StatusCode status() const { return status_; }
  bool has_value() const { return has_value_; }

  const T& value() const { return value_; }
  T& value() { return value_; }
  T Release() { return std::move(value_); }

 private:
  T value_{};
  StatusCode status_;
  bool has_value_;
};

template <typename T>
Result<T> Ok(T v) {
  return Result<T>::Ok(std::move(v));
}

template <typename T>
Result<T> Err(StatusCode c) {
  return Result<T>::Error(c);
}

}  // namespace codec
}  // namespace video
