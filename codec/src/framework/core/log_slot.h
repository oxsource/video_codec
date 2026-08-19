// log_slot.h
#pragma once

#include <cstdint>
#include <string>

#include "src/framework/core/export.h"

namespace video {
namespace codec {

enum class LogLevel { kInfo, kDebug, kWarn, kError };

// Plug-in logging interface. The framework routes all logging through the
// process-wide slot; by default it is a no-op so there is no hard dependency
// on any logging library.
class VIDEO_CODEC_API LogSlot {
 public:
  virtual ~LogSlot() = default;
  virtual void Write(LogLevel level, const char* file, int line, const std::string& msg) = 0;
};

// Returns the current slot. Never null: before SetLogSlot() it returns a
// built-in no-op slot.
VIDEO_CODEC_API LogSlot* GetLogSlot();

// Install a slot. Pass nullptr to restore the no-op default.
VIDEO_CODEC_API void SetLogSlot(LogSlot* slot);

// Framework logging helper. Use the VC_LOG macro at call sites.
VIDEO_CODEC_API void Log(LogLevel level, const char* file, int line, const std::string& msg);

}  // namespace codec
}  // namespace video

#define VC_LOG(level, msg) ::video::codec::Log((level), __FILE__, __LINE__, (msg))
