// log_slot.h
#pragma once

#include <cstdint>
#include <string>

namespace video {
namespace codec {

enum class LogLevel { kTrace, kInfo, kWarn, kError };

// Plug-in logging interface. The framework routes all logging through the
// process-wide slot; by default it is a no-op so there is no hard dependency
// on any logging library.
class LogSlot {
 public:
  virtual ~LogSlot() = default;
  virtual void Write(LogLevel level, const char* file, int line,
                     const std::string& msg) = 0;
};

// Returns the current slot. Never null: before SetLogSlot() it returns a
// built-in no-op slot.
LogSlot* GetLogSlot();

// Install a slot. Pass nullptr to restore the no-op default.
void SetLogSlot(LogSlot* slot);

// Framework logging helper. Use the VC_LOG macro at call sites.
void Log(LogLevel level, const char* file, int line, const std::string& msg);

}  // namespace codec
}  // namespace video

#define VC_LOG(level, msg) \
  ::video::codec::Log((level), __FILE__, __LINE__, (msg))
