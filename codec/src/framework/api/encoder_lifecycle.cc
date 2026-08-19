// encoder_lifecycle.cc
#include "src/framework/api/encoder_lifecycle.h"

namespace video {
namespace codec {

Status EncoderLifecycle::Init() {
  // Allowed from Created (first init) or Flushed (reuse). Anything else is an
  // invalid transition.
  if (state_ == State::kCreated || state_ == State::kFlushed) {
    state_ = State::kInitialized;
    return Status::kOk;
  }
  return Status::kInvalidArgument;
}

Status EncoderLifecycle::Encode() {
  switch (state_) {
    case State::kInitialized:
      state_ = State::kEncoding;
      return Status::kOk;
    case State::kEncoding:
      return Status::kOk;
    default:
      return Status::kNotInitialized;  // Created / Flushed / Released
  }
}

Status EncoderLifecycle::Flush() {
  switch (state_) {
    case State::kInitialized:
    case State::kEncoding:
    case State::kFlushed:
      state_ = State::kFlushed;
      return Status::kOk;
    default:
      return Status::kNotInitialized;  // Created / Released
  }
}

Status EncoderLifecycle::Release() {
  state_ = State::kReleased;  // idempotent
  return Status::kOk;
}

}  // namespace codec
}  // namespace video
