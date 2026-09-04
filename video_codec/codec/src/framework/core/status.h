// status.h
#pragma once

#include <cstdint>

#include "codec/src/framework/core/export.h"

namespace video {
namespace codec {

// Unified status code returned by every fallible public API. No exceptions
// cross the public boundary; callers inspect this instead. Also covers the
// transport results from PacketSource::Pop: kOk (packet), kEmpty (timeout /
// empty), kEos (end-of-stream and drained).
enum class Status {
  kOk = 0,
  kEmpty,                 // no data available (e.g. Pop timed out / queue empty)
  kEos,                   // end-of-stream reached and drained (e.g. Pop after MarkEos)
  kInvalidArgument,       // bad config / null handle / invalid argument
  kNotInitialized,        // operation requires Init() first
  kEncodeFailed,          // external encoder rejected a frame / flush
  kUnsupportedFormat,     // pixel/sample format not handled by the backend
  kBackendUnavailable,    // requested backend could not be loaded/resolved
  kPlatformUnsupported,   // no backend for this platform/configuration
  kUnsupportedOperation,  // e.g. Encode(NativeBuffer) on a software-only path
  kNetworkError,          // transport/signaling failure (e.g. WHIP HTTP errors)
  kTimeout,               // operation did not complete within its deadline
};

// Human-readable name of a status, e.g. StatusToString(Status::kEncodeFailed)
// == "kEncodeFailed". Returns "kUnknown" for an out-of-range value. A free
// function (not a member): C++ enum classes cannot declare member functions.
inline VIDEO_CODEC_API const char* StatusToString(Status c) {
  switch (c) {
    case Status::kOk:
      return "kOk";
    case Status::kEmpty:
      return "kEmpty";
    case Status::kEos:
      return "kEos";
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
    case Status::kNetworkError:
      return "kNetworkError";
    case Status::kTimeout:
      return "kTimeout";
  }
  return "kUnknown";
}

}  // namespace codec
}  // namespace video
