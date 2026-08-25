#pragma once

// Self-registration macro for stream backends.
// Usage: in webrtc_backend.cc:
//   VIDEO_STREAM_REGISTER("webrtc", WebrtcBackend::Create);

#include "src/core/backend_registry.h"

#define VIDEO_STREAM_REGISTER(name, factory) \
  namespace { \
    [[maybe_unused]] auto VIDEO_STREAM_REGISTER_IMPL = [] { \
      ::video::stream::RegisterBackend(name, factory); \
      return true; \
    }(); \
  }