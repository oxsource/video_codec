// status.h
#pragma once

#include <cstdint>

namespace video {
namespace codec {

// Unified status code returned by every fallible public API. No exceptions
// cross the public boundary; callers inspect this instead.
enum class Status {
  kOk = 0,
  kInvalidArgument,       // bad config / null handle / invalid argument
  kNotInitialized,        // operation requires Init() first
  kEncodeFailed,          // external encoder rejected a frame / flush
  kUnsupportedFormat,     // pixel/sample format not handled by the backend
  kBackendUnavailable,    // requested backend could not be loaded/resolved
  kPlatformUnsupported,   // no backend for this platform/configuration
  kUnsupportedOperation,  // e.g. Encode(NativeBuffer) on a software-only path
};

// Human-readable name of a status, e.g. StatusToString(Status::kEncodeFailed)
// == "kEncodeFailed". Returns "kUnknown" for an out-of-range value. A free
// function (not a member): C++ enum classes cannot declare member functions.
inline const char* StatusToString(Status c) {
  switch (c) {
    case Status::kOk:
      return "kOk";
    case Status::kInvalidArgument:
      return "kInvalidArgument";
    case Status::kNotInitialized:
      return "kNotInitialized";
    case Status::kEncodeFailed:
      return "kEncodeFailed";
    case Status::kUnsupportedFormat:
      return "kUnsupportedFormat";
    case Status::kBackendUnavailable:
      return "kBackendUnavailable";
    case Status::kPlatformUnsupported:
      return "kPlatformUnsupported";
    case Status::kUnsupportedOperation:
      return "kUnsupportedOperation";
  }
  return "kUnknown";
}

}  // namespace codec
}  // namespace video
