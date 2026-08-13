// packet_queue.cc
#include "queue/packet_queue.h"

#include <string>

#include "core/log_slot.h"

namespace video {
namespace codec {

namespace {

// Drain one packet of media type `Pkt` from `src` and forward it to `sink`.
// Returns false only when the consumer failed (Finish() already called) — the
// whole await must abort. Sets `done` when that media's stream hit EOS; a done
// stream is a no-op so the loop can call this unconditionally.
template <typename Pkt>
bool DrainOne(PacketSource& src, Pkt& out, bool& done, PacketSink& sink,
              int64_t deadline_us, const char* what) {
  if (done) return true;
  switch (src.Pop(out, deadline_us)) {
    case Status::kOk:
      if (sink.Consume(std::move(out)) != Status::kOk) {
        VC_LOG(LogLevel::kError,
               std::string("PacketQueue::Await: ") + what + " Consume failed");
        sink.Finish();
        return false;
      }
      return true;
    case Status::kEos:
      done = true;
      return true;
    case Status::kEmpty:
      return true;  // try the other media (or retry next iteration)
  }
  return true;  // Pop only yields kOk/kEmpty/kEos; keep draining otherwise
}

}  // namespace

PacketQueue::PacketQueue(size_t capacity, Backpressure policy)
    : video_(capacity, policy), audio_(capacity, policy) {}

Status PacketQueue::Submit(VideoPacket&& pkt) {
  if (video_.Push(std::move(pkt))) return Status::kOk;
  // Rejected: full under kError (kBackendUnavailable), or push-after-EOS
  // (kInvalidArgument). Under kBlock/kDropOldest Push only fails at EOS.
  return video_.policy() == Backpressure::kError ? Status::kBackendUnavailable
                                                 : Status::kInvalidArgument;
}

Status PacketQueue::Submit(AudioPacket&& pkt) {
  if (audio_.Push(std::move(pkt))) return Status::kOk;
  return audio_.policy() == Backpressure::kError ? Status::kBackendUnavailable
                                                 : Status::kInvalidArgument;
}

Status PacketQueue::Pop(VideoPacket& out, int64_t deadline_us) {
  return video_.Pop(out, deadline_us);
}

Status PacketQueue::Pop(AudioPacket& out, int64_t deadline_us) {
  return audio_.Pop(out, deadline_us);
}

Status PacketQueue::Await(PacketSink& sink, int64_t deadline_us) {
  VideoPacket vp;
  AudioPacket ap;
  bool video_done = false;
  bool audio_done = false;

  // DrainOne no-ops on a done stream, so the loop calls both unconditionally;
  // termination is the explicit break once BOTH streams hit EOS.
  while (true) {
    if (!DrainOne(*this, vp, video_done, sink, deadline_us, "video") ||
        !DrainOne(*this, ap, audio_done, sink, deadline_us, "audio")) {
      return Status::kEncodeFailed;
    }
    if (video_done && audio_done) break;  // both streams finished
  }
  return sink.Finish();
}

void PacketQueue::MarkEos() {
  video_.MarkEos();
  audio_.MarkEos();
}

}  // namespace codec
}  // namespace video
