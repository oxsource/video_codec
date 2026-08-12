// status.h
#pragma once

#include <cstdint>

namespace video {
namespace codec {

// Unified status code returned by every fallible public API. No exceptions cross
// the public boundary; callers inspect this instead.
enum class StatusCode {
  kOk = 0,
  kInvalidArgument,     // bad config / null handle / invalid argument
  kNotInitialized,      // operation requires Init() first
  kEncodeFailed,        // external encoder rejected a frame / flush
  kUnsupportedFormat,   // pixel/sample format not handled by the backend
  kBackendUnavailable,  // requested backend could not be loaded/resolved
  kPlatformUnsupported, // no backend for this platform/configuration
  kUnsupportedOperation,// e.g. Encode(NativeBuffer) on a software-only path
};

inline const char* StatusCodeToString(StatusCode c) {
  switch (c) {
    case StatusCode::kOk: return "kOk";
    case StatusCode::kInvalidArgument: return "kInvalidArgument";
    case StatusCode::kNotInitialized: return "kNotInitialized";
    case StatusCode::kEncodeFailed: return "kEncodeFailed";
    case StatusCode::kUnsupportedFormat: return "kUnsupportedFormat";
    case StatusCode::kBackendUnavailable: return "kBackendUnavailable";
    case StatusCode::kPlatformUnsupported: return "kPlatformUnsupported";
    case StatusCode::kUnsupportedOperation: return "kUnsupportedOperation";
  }
  return "kUnknown";
}

}  // namespace codec
}  // namespace video
