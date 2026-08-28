// egl_surface.h
//
// Minimal reusable EGL window-surface wrapper: sets up an EGL display/surface/
// context over an opaque native window handle (an ANativeWindow* as void*),
// and exposes make-current / presentation-time / swap-buffers. Used by
// SmpteBars::Surface (input-surface rendering) and reusable by any caller that
// needs to GPU-render into a Surface. This header stays NDK-free: the native
// window is passed as an opaque void*. The platform check lives inside the
// class — on platforms/builds without EGL support (non-Android) Create()
// returns nullptr and the methods return false.
#pragma once

#include <cstdint>
#include <memory>

namespace video {
namespace codec {
namespace utils {

class EglSurface {
 public:
  // Set up an EGL display/surface/context over `native_window` (an
  // ANativeWindow* from MediaCodec createInputSurface or any Surface). The
  // EGL context is current after Create(). Returns nullptr on unsupported
  // platforms, a null handle, invalid dimensions, or EGL setup failure.
  static std::unique_ptr<EglSurface> Create(void* native_window, int width, int height);

  ~EglSurface();
  EglSurface(const EglSurface&) = delete;
  EglSurface& operator=(const EglSurface&) = delete;

  // Make the EGL context current on this surface (idempotent; useful after
  // another EGL context was made current elsewhere).
  bool MakeCurrent();

  // Surface size in pixels (queried from EGL).
  int Width() const;
  int Height() const;

  // Set the timestamp (nanoseconds) of the next buffer presented on the
  // surface via eglPresentationTimeANDROID. Returns false if the EGL
  // extension is unavailable (the presentation then uses the system time).
  bool SetPresentationTimeNs(int64_t timestamp_ns);

  // Present the back buffer (eglSwapBuffers).
  bool SwapBuffers();

 private:
  struct Impl;
  explicit EglSurface(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace utils
}  // namespace codec
}  // namespace video
