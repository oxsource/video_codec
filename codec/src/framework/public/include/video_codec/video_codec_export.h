// video_codec_export.h
#pragma once

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
