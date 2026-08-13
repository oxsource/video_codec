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
// (video_codec_export.h); it expands to nothing for static builds. Note: the
// individual API types below are not yet decorated with VIDEO_CODEC_API;
// tagging them for shared-library export is a tracked follow-up. Static builds
// are the default and are unaffected.

#pragma once

#include "video_codec_export.h"

// Core value types & uniform error model
#include "core/log_slot.h"
#include "core/result.h"
#include "core/status.h"
#include "core/types.h"

// Abstract encoder interfaces, surface input, and backend factory
#include "api/audio_encoder.h"
#include "api/encoder_factory.h"
#include "api/input_surface.h"
#include "api/video_encoder.h"
