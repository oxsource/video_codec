// encoder_lifecycle.cc
#include "api/encoder_lifecycle.h"

namespace video {
namespace codec {

StatusCode EncoderLifecycle::Init() {
  // Allowed from Created (first init) or Flushed (reuse). Anything else is an
  // invalid transition.
  if (state_ == State::kCreated || state_ == State::kFlushed) {
    state_ = State::kInitialized;
    return StatusCode::kOk;
  }
  return StatusCode::kInvalidArgument;
}

StatusCode EncoderLifecycle::Encode() {
  switch (state_) {
    case State::kInitialized:
      state_ = State::kEncoding;
      return StatusCode::kOk;
    case State::kEncoding:
      return StatusCode::kOk;
    default:
      return StatusCode::kNotInitialized;  // Created / Flushed / Released
  }
}

StatusCode EncoderLifecycle::Flush() {
  switch (state_) {
    case State::kInitialized:
    case State::kEncoding:
    case State::kFlushed:
      state_ = State::kFlushed;
      return StatusCode::kOk;
    default:
      return StatusCode::kNotInitialized;  // Created / Released
  }
}

StatusCode EncoderLifecycle::Release() {
  state_ = State::kReleased;  // idempotent
  return StatusCode::kOk;
}

}  // namespace codec
}  // namespace video
