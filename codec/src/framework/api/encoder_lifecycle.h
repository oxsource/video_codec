// encoder_lifecycle.h
#pragma once

#include "src/framework/core/status.h"

namespace video {
namespace codec {

// Lifecycle state machine shared by all encoder backends. Enforces the valid
// transition table (data-model.md §5) so backends cannot reach an illegal
// state.
//   Created -> Initialized -> Encoding -> Flushed -> Released
// `Init()` may be called again from `Flushed` to reuse the encoder.
class EncoderLifecycle {
 public:
  enum class State { kCreated, kInitialized, kEncoding, kFlushed, kReleased };

  Status Init();
  Status Encode();   // call before producing a packet
  Status Flush();    // -> Flushed
  Status Release();  // -> Released (idempotent)

  State state() const { return state_; }
  bool ready() const {  // Init() has succeeded and not yet Released
    return state_ == State::kInitialized || state_ == State::kEncoding || state_ == State::kFlushed;
  }
  // True if Init() is a legal transition from the current state (Created or
  // Flushed). Backends use this to GUARD entry before doing real init work,
  // then call Init() only after the work succeeds to COMMIT the transition —
  // a failed init leaves the encoder in its previous state and reusable.
  bool CanInit() const { return state_ == State::kCreated || state_ == State::kFlushed; }

 private:
  State state_ = State::kCreated;
};

}  // namespace codec
}  // namespace video
