// video_codec.h
//
// Single public include surface for the video_codec library.
//
// Consumers include ONLY this header:
//
//   #include <video_codec/video_codec.h>
//
// It re-exports the frozen public contracts (core media types, error model,
// logging slot, abstract encoder interfaces, and the factory/backend selection)
// and nothing internal. See
// specs/003-core-utils-public-api/contracts/public-api.md.
//
// Symbol visibility for shared-library builds is governed by VIDEO_CODEC_API
// (video_codec_export.h); it expands to nothing for static builds. The public
// classes and free functions (encoders, muxer, factory, status/backend names,
// log slot) are decorated with VIDEO_CODEC_API so the shared library exports
// them. Static builds are the default and are unaffected.

#pragma once

#include "video_codec_export.h"

// Core value types & uniform error model
#include "log_slot.h"
#include "result.h"
#include "status.h"
#include "types.h"

// Abstract encoder interfaces, muxer, and backend factory
#include "audio_encoder.h"
#include "codec_factory.h"
#include "muxer.h"
#include "video_encoder.h"

// Byte-output sinks used by Muxer (SetOutput) to write container bytes
#include "byte_sink.h"
#include "file_byte_sink.h"

// Media file-format constants (common output extensions like .mp4 / .h264)
#include "media_format.h"
