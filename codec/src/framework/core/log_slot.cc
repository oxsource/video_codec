// log_slot.cc
#include "log_slot.h"

#include <mutex>

namespace video {
namespace codec {
namespace {

// Built-in no-op slot used until a consumer installs a real one.
class NoOpLogSlot : public LogSlot {
 public:
  void Write(LogLevel, const char*, int, const std::string&) override {}
};

NoOpLogSlot g_noop;
LogSlot* g_slot = &g_noop;
std::mutex g_slot_mu;

}  // namespace

LogSlot* GetLogSlot() {
  std::lock_guard<std::mutex> lk(g_slot_mu);
  return g_slot;
}

void SetLogSlot(LogSlot* slot) {
  std::lock_guard<std::mutex> lk(g_slot_mu);
  g_slot = slot ? slot : &g_noop;
}

void Log(LogLevel level, const char* file, int line, const std::string& msg) {
  // Read the slot pointer once under the lock, then call outside the lock to
  // avoid re-entrant deadlocks in a consumer's logger.
  LogSlot* slot = GetLogSlot();
  slot->Write(level, file, line, msg);
}

}  // namespace codec
}  // namespace video
