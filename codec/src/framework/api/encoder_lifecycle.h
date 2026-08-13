// encoder_lifecycle.h
#pragma once

#include "core/status.h"

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

  StatusCode Init();
  StatusCode Encode();   // call before producing a packet
  StatusCode Flush();    // -> Flushed
  StatusCode Release();  // -> Released (idempotent)

  State state() const { return state_; }
  bool ready() const {  // Init() has succeeded and not yet Released
    return state_ == State::kInitialized || state_ == State::kEncoding ||
           state_ == State::kFlushed;
  }

 private:
  State state_ = State::kCreated;
};

}  // namespace codec
}  // namespace video
