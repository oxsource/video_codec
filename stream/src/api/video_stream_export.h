#pragma once

#if defined(_WIN32)
#if defined(VIDEO_STREAM_SHARED_LIBRARY)
#define VIDEO_STREAM_API __declspec(dllexport)
#else
#define VIDEO_STREAM_API __declspec(dllimport)
#endif
#else
#if defined(VIDEO_STREAM_SHARED_LIBRARY)
#define VIDEO_STREAM_API __attribute__((visibility("default")))
#else
#define VIDEO_STREAM_API
#endif
#endif