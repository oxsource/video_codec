// video_codec_export.h
#pragma once

// The canonical definition lives in core/export.h (framework headers tag their
// public types from there). Re-export it here so consumers who include only
// <video_codec/video_codec.h> still see the VIDEO_CODEC_API macro.
#include "export.h"
