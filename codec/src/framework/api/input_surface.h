// input_surface.h
#pragma once

#include <cstdint>
#include <memory>

namespace video {
namespace codec {

// A drawable surface a caller renders into; the backend serializes against its
// own encode thread. Only the Android backend returns a real surface; the
// FFmpeg (software) backend returns nullptr.
class InputSurface {
 public:
  virtual ~InputSurface() = default;
  virtual void* GetNativeSurface() = 0;  // e.g. ANativeWindow*
  virtual bool QueueFrame(int64_t timestamp_us) = 0;
};

}  // namespace codec
}  // namespace video
