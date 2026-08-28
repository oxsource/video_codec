# Encoder Lifecycle

## State machine

```mermaid
stateDiagram-v2
    [*] --> Created: VideoEncoder::Create()
    Created --> Initialized: Init()
    Initialized --> Encoding: Encode() succeeds
    Encoding --> Encoding: Encode() more frames
    Encoding --> Initialized: error → Reset/Release
    Initialized --> Flushed: Flush()
    Flushed --> Initialized: Init() again (reuse)
    Flushed --> [*]: Release()
    Encoding --> [*]: Release()
    Created --> [*]: Release()
    Initialized --> [*]: Release()
```

## Transition table

| State \ Event | `Init()` | `Encode()` | `Flush()` | `Release()` |
|--------------|----------|------------|-----------|-------------|
| Created | →Initialized | `kNotInitialized` | `kNotInitialized` | →Released |
| Initialized | err (already) | →Encoding | →Flushed | →Released |
| Encoding | err | →Encoding | →Flushed | →Released |
| Flushed | →Initialized (reuse) | `kNotInitialized` | →Flushed | →Released |
| Released | err | err | err | err |

## Rules

1. `Create()` only allocates the object; it does **not** open the external encoder.
2. `Init()` opens the external encoder and transitions to `Initialized`. Calling `Init()`
   again from `Initialized` is an error (call `Release()` first).
3. `Encode()` before `Init()` returns `kNotInitialized` and does not change state.
4. `Flush()` drains buffered frames and emits the final packet(s); the encoder remains
   reusable after `Flush()` (transition back to `Initialized` via re-`Init` if needed, or
   keep encoding).
5. `Release()` frees external resources; it is **idempotent** and valid from any state.
6. On an encode error, the backend returns the error and the caller decides whether to
   `Release()` or attempt recovery; the state machine does not auto-advance.

## Rationale

A small, explicit state machine makes the public contract testable and prevents the
class of "used before initialized / double-free" bugs that a free-form API invites.
