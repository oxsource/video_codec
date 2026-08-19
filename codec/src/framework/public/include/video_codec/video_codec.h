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
#include "src/framework/core/log_slot.h"
#include "src/framework/core/result.h"
#include "src/framework/core/status.h"
#include "src/framework/core/types.h"

// Abstract encoder interfaces, muxer, and backend factory
#include "src/framework/api/audio_encoder.h"
#include "src/framework/api/codec_factory.h"
#include "src/framework/api/muxer.h"
#include "src/framework/api/video_encoder.h"

// Byte-output sinks used by Muxer (SetOutput) to write container bytes
#include "src/framework/io/byte_sink.h"
#include "src/framework/io/file_byte_sink.h"

// Media file-format constants (common output extensions like .mp4 / .h264)
#include "src/framework/utils/media_format.h"
