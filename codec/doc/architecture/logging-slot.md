# Logging Slot

## Goal

Let the framework emit diagnostics without taking a hard dependency on any logging
library. Consumers plug in their own logger; by default logging is a no-op.

## Interface

```cpp
namespace video::codec {

enum class LogLevel { kTrace, kInfo, kWarn, kError };

class LogSlot {
  public:
    virtual ~LogSlot() = default;
    virtual void Log(LogLevel level, const char* msg) = 0;
};

// Process-wide slot; set once at startup. Null = no-op.
void SetLogSlot(LogSlot* slot);

// Framework macro (expands to nothing when slot is null):
VIDEO_CODEC_LOG(LogLevel::kInfo, "encoded frame %d", frame_no);

}  // namespace video::codec
```

## Contract

1. The framework calls `slot->Log(level, msg)` only when a slot is installed.
2. `LogSlot` implementations must be thread-safe (called from encoder threads).
3. The framework never deletes the installed slot; the consumer owns its lifetime.
4. Formatting is done by the framework into a fixed buffer before calling `Log`, so
   implementations need not parse format strings.

## Example plug-ins (consumer side)

- Desktop: forward to `spdlog` / `fprintf`.
- Android: forward to `__android_log_print`.
- Tests: a capturing slot that asserts on expected log lines.

## Rationale

Mirrors native_ui's `LogSlot`: keeps the library dependency-free and lets each consumer
keep its existing logging pipeline. See `research.md` R6.
