// export.h
#pragma once

// Canonical export/visibility macro for the public C++ API. Framework headers
// (core/api) include this directly so their public types can be tagged for
// shared-library export; the public video_codec_export.h re-exports the same
// macro for consumers who include only <video_codec/video_codec.h>.
//
//   - Shared builds (VIDEO_CODEC_SHARED_LIBRARY defined): expands to default
//     visibility so the symbol is exported from the shared library.
//   - Static builds: expands to nothing.
#if defined(_WIN32)
#if defined(VIDEO_CODEC_SHARED_LIBRARY)
#define VIDEO_CODEC_API __declspec(dllexport)
#else
#define VIDEO_CODEC_API __declspec(dllimport)
#endif
#else
#if defined(VIDEO_CODEC_SHARED_LIBRARY)
#define VIDEO_CODEC_API __attribute__((visibility("default")))
#else
#define VIDEO_CODEC_API
#endif
#endif
