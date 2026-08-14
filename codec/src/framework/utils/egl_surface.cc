// egl_surface.cc
#include "egl_surface.h"

#if defined(__ANDROID__)
#include <android/native_window.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

namespace video {
namespace codec {
namespace utils {

#if defined(__ANDROID__)
struct EglSurface::Impl {
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLSurface surface = EGL_NO_SURFACE;
  EGLContext context = EGL_NO_CONTEXT;
  int width = 0;
  int height = 0;
};

namespace {

// eglPresentationTimeANDROID — per-buffer timestamp for an input surface (EGL
// extension; resolved at runtime, research R7).
using PresentationTimeFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLnsecsANDROID);
PresentationTimeFn ResolvePresentationTime() {
  static PresentationTimeFn fn =
      reinterpret_cast<PresentationTimeFn>(eglGetProcAddress("eglPresentationTimeANDROID"));
  return fn;
}

}  // namespace

std::unique_ptr<EglSurface> EglSurface::Create(void* native_window, int width, int height) {
  ANativeWindow* window = static_cast<ANativeWindow*>(native_window);
  if (!window || width <= 0 || height <= 0) return nullptr;

  auto impl = std::make_unique<Impl>();
  impl->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (impl->display == EGL_NO_DISPLAY ||
      eglInitialize(impl->display, nullptr, nullptr) != EGL_TRUE) {
    return nullptr;
  }
  static const EGLint kWindowAttribs[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_NONE,
  };
  EGLConfig config = nullptr;
  EGLint num_configs = 0;
  if (eglChooseConfig(impl->display, kWindowAttribs, &config, 1, &num_configs) != EGL_TRUE ||
      num_configs < 1) {
    eglTerminate(impl->display);
    return nullptr;
  }
  impl->surface = eglCreateWindowSurface(impl->display, config, window, nullptr);
  if (impl->surface == EGL_NO_SURFACE) {
    eglTerminate(impl->display);
    return nullptr;
  }
  static const EGLint kContextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  impl->context = eglCreateContext(impl->display, config, EGL_NO_CONTEXT, kContextAttribs);
  if (impl->context == EGL_NO_CONTEXT) {
    eglDestroySurface(impl->display, impl->surface);
    eglTerminate(impl->display);
    return nullptr;
  }
  if (eglMakeCurrent(impl->display, impl->surface, impl->surface, impl->context) != EGL_TRUE) {
    eglDestroyContext(impl->display, impl->context);
    eglDestroySurface(impl->display, impl->surface);
    eglTerminate(impl->display);
    return nullptr;
  }
  // Query the actual surface size (the window may have negotiated a different
  // geometry than requested).
  EGLint w = 0, h = 0;
  if (eglQuerySurface(impl->display, impl->surface, EGL_WIDTH, &w) != EGL_TRUE ||
      eglQuerySurface(impl->display, impl->surface, EGL_HEIGHT, &h) != EGL_TRUE) {
    eglDestroyContext(impl->display, impl->context);
    eglDestroySurface(impl->display, impl->surface);
    eglTerminate(impl->display);
    return nullptr;
  }
  impl->width = w;
  impl->height = h;
  return std::unique_ptr<EglSurface>(new EglSurface(std::move(impl)));
}

bool EglSurface::MakeCurrent() {
  if (!impl_) return false;
  return eglMakeCurrent(impl_->display, impl_->surface, impl_->surface, impl_->context) ==
         EGL_TRUE;
}

int EglSurface::Width() const { return impl_ ? impl_->width : 0; }
int EglSurface::Height() const { return impl_ ? impl_->height : 0; }

bool EglSurface::SetPresentationTimeNs(int64_t timestamp_ns) {
  if (!impl_) return false;
  if (auto pt = ResolvePresentationTime()) {
    pt(impl_->display, impl_->surface, static_cast<EGLnsecsANDROID>(timestamp_ns));
    return true;
  }
  return false;
}

bool EglSurface::SwapBuffers() {
  if (!impl_) return false;
  return eglSwapBuffers(impl_->display, impl_->surface) == EGL_TRUE;
}

EglSurface::~EglSurface() {
  if (!impl_) return;
  if (impl_->context != EGL_NO_CONTEXT) eglDestroyContext(impl_->display, impl_->context);
  if (impl_->surface != EGL_NO_SURFACE) eglDestroySurface(impl_->display, impl_->surface);
  if (impl_->display != EGL_NO_DISPLAY) eglTerminate(impl_->display);
}

#else  // !defined(__ANDROID__)

struct EglSurface::Impl {};

std::unique_ptr<EglSurface> EglSurface::Create(void*, int, int) { return nullptr; }
bool EglSurface::MakeCurrent() { return false; }
int EglSurface::Width() const { return 0; }
int EglSurface::Height() const { return 0; }
bool EglSurface::SetPresentationTimeNs(int64_t) { return false; }
bool EglSurface::SwapBuffers() { return false; }
EglSurface::~EglSurface() = default;

#endif  // defined(__ANDROID__)

EglSurface::EglSurface(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

}  // namespace utils
}  // namespace codec
}  // namespace video
