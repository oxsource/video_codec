// log_slot.h
#pragma once

#include <cstdint>
#include <string>

#include "codec/src/framework/core/export.h"

namespace video {
namespace codec {

enum class LogLevel { kInfo, kDebug, kWarn, kError };

// Plug-in logging interface. The framework routes all logging through the
// process-wide slot; by default it is a no-op so there is no hard dependency
// on any logging library.
class VIDEO_CODEC_API LogSlot {
 public:
  virtual ~LogSlot() = default;
  virtual void Write(LogLevel level, const char* tag, int line, const std::string& msg) = 0;
};

// Returns the current slot. Never null: before SetLogSlot() it returns a
// built-in no-op slot.
VIDEO_CODEC_API LogSlot* GetLogSlot();

// Install a slot. Pass nullptr to restore the no-op default.
VIDEO_CODEC_API void SetLogSlot(LogSlot* slot);

// Framework logging helper. Use the VC_LOG macro at call sites.
VIDEO_CODEC_API void Log(LogLevel level, const char* tag, int line, const std::string& msg);

}  // namespace codec
}  // namespace video

// Per-file log tag mechanism: a compilation unit may define LOG_TAG (typically
// just before including the first header) to identify its log output:
//
//   #define LOG_TAG "webrtc_backend"
//   #include "codec/src/framework/core/log_slot.h"
//
// When LOG_TAG is not defined, it defaults to __FILE__ so existing call sites
// keep the previous source-path behavior without any changes.
#ifndef LOG_TAG
#define LOG_TAG __FILE__
#endif

#define VC_LOG(level, msg) ::video::codec::Log((level), LOG_TAG, __LINE__, (msg))
