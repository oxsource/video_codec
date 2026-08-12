# Contract: Output Queue Transport (`OutputSink` / `EncodedPacketSource`)

Module: `queue` (depends only on `core`). Decision: `codec/doc/adrs/ADR-005-ringbuffer-transport.md`.
Architecture: `codec/doc/architecture/output-queue.md`.

This contract defines the *interface surface* the implementation phase must honor. No
runtime code yet — it specifies the shapes, ownership rules, and back-pressure semantics.

## 1. Purpose

Provide a bounded SPSC lock-free ring buffer that decouples an encoder (producer) from a
consumer (muxer / network / file) across threads, with no allocation or lock on the hot
`Encode()`→output path. It is **additive** to the existing pull API: `Encode()` returning
`Result<EncodedPacket>` is unchanged; pushing to a queue is an opt-in sink mode.

## 2. Interfaces

```cpp
// Producer endpoint. Implemented by EncodedPacketQueue; consumed by the encoder.
class OutputSink {
  public:
    virtual ~OutputSink() = default;
    virtual StatusCode Submit(EncodedPacket&& pkt) = 0;   // video packet
    virtual StatusCode Submit(AudioPacket&& pkt) = 0;     // audio packet
    virtual StatusCode Flush() { return StatusCode::kOk; }
};

// Consumer endpoint. Implemented by EncodedPacketQueue; consumed by muxer/network/file.
class EncodedPacketSource {
  public:
    virtual ~EncodedPacketSource() = default;
    virtual bool TryPop(EncodedPacket& out) = 0;         // non-blocking; false if empty
    virtual bool TryPop(AudioPacket& out) = 0;
    // Blocking variant (optional but recommended):
    //   virtual StatusCode Pop(EncodedPacket& out, Deadline deadline) = 0;
};
```

### 2.1 `EncodedPacketQueue` dual role

`EncodedPacketQueue` implements **both** interfaces on the same object:

- The encoder holds an `OutputSink&` (producer view).
- The consumer holds an `EncodedPacketSource&` (consumer view).
- Both references point at the same queue instance; the queue owns the slot storage.

## 3. Data layout

| Field | Type | Constraint |
|-------|------|-----------|
| `capacity` | `size_t` | `> 0` and a power of two |
| `slots[]` | `EncodedPacket / AudioPacket[]` | pre-allocated at construction; packets **moved** in/out |
| `head_` | `std::atomic<size_t>` | consumer index, advanced only by consumer |
| `tail_` | `std::atomic<size_t>` | producer index, advanced only by producer |
| `policy` | `Backpressure` | one of `kBlock` / `kDropOldest` / `kError` |

```cpp
enum class Backpressure { kBlock, kDropOldest, kError };
```

Indexing: `(idx & (capacity − 1))`. `empty ⇒ head_ == tail_`; `full ⇒ (tail_ − head_) == capacity`.

## 4. Behavior contract

### 4.1 Producer (`OutputSink`)

- `Submit(EncodedPacket&&)` / `Submit(AudioPacket&&)`:
  - If not full: move the packet into `slots[tail_ & mask]`, advance `tail_` (release), return `kOk`.
  - If full: honor `policy`:
    - `kBlock` → block until a slot frees (consumer advances `head_`), then store.
    - `kDropOldest` → advance `head_` to discard the oldest unconsumed slot, store at `tail_`.
    - `kError` → return a back-pressure status code (e.g. `kBackendUnavailable` or a
      dedicated `kQueueFull`), **do not** store.
- `Flush()`: signal end-of-stream to the consumer (e.g. a flush flag the source observes);
  default no-op returns `kOk`.
- After `Submit()` returns, the producer must **not** reference the packet (ownership moved).

### 4.2 Consumer (`EncodedPacketSource`)

- `TryPop(...)`: if not empty, move `slots[head_ & mask]` out, advance `head_` (release),
  return `true`; if empty, return `false` **without blocking**.
- `Pop(deadline)` (blocking variant): wait until a packet is available or `deadline`
  elapses; returns `kOk` + packet, or a timeout status. Required for consumers that must
  wait (muxer/network).
- Order is FIFO per media type; across video/audio the relative order is preserved as
  submitted (single producer → single consumer preserves total order).

## 5. Ownership & thread-safety rules

- Exactly **one** producer writes `tail_`; exactly **one** consumer writes `head_`
  (SPSC). `std::atomic` with acquire/release ordering is sufficient — no CAS, no mutex.
- Memory order: producer stores packet, then `tail_.store(release)`; consumer loads
  `tail_` (acquire) before reading the slot. Symmetric for `head_`.
- The queue owns `slots[]`; packets' internal `std::vector<uint8_t>` buffers move, so no
  per-packet heap allocation on the hot path.
- `Backpressure::kBlock` may block the producer thread; callers choosing it must accept
  that the encoder thread can wait on a slow consumer (this is the explicit decoupling
  trade-off).

## 6. Validation rules (implementation must satisfy)

- `capacity` is validated `> 0` and power-of-two at construction; reject otherwise.
- `Submit` on a full queue honors the configured `policy` exactly as in §4.1.
- `TryPop` on an empty queue never blocks and returns `false`.
- `Pop(deadline)` returns a timeout status (not `false`) when the deadline passes empty.
- `kDropOldest` must not corrupt in-flight slots the consumer has not yet popped beyond the
  single oldest one.

## 7. Relationship to other contracts

- `encoder-contract.md`: the encoder exposes its existing pull API; the queue is an
  optional `OutputSink` target wired after a successful `Encode()`.
- `public-api.md`: `public` re-exports `queue` so consumers obtain an `EncodedPacketSource`.
- `data-model.md` §8: authoritative entity/field model for `EncodedPacketQueue`.
