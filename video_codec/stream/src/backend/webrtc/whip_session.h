#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "stream/src/api/stream_config.h"

namespace cpp_network {
namespace http {
class Client;
}  // namespace http
}  // namespace cpp_network

namespace video {
namespace stream {

struct WhipIceCandidate {
  std::string candidate;
  std::string sdp_mid;
  int sdp_mline_index = 0;
};

class WhipSession {
 public:
  using OnIceCandidateHandler = std::function<void(const WhipIceCandidate&)>;
  using OnReadyHandler = std::function<void(const std::string& sdp)>;
  using OnErrorHandler = std::function<void(const std::string& error)>;

  explicit WhipSession(const NetworkConfig& network);
  ~WhipSession();

  bool Create(const std::string& whip_endpoint, const std::string& offer_sdp);
  bool PatchIce(const std::string& whip_endpoint, const std::string& session_id,
                const std::vector<WhipIceCandidate>& candidates);
  bool Delete();

  void SetOnReady(OnReadyHandler handler) { on_ready_ = std::move(handler); }
  void SetOnError(OnErrorHandler handler) { on_error_ = std::move(handler); }

  const std::string& SessionId() const { return session_id_; }
  const std::string& ResourceUrl() const { return resource_url_; }
  const std::string& AnswerSdp() const { return answer_sdp_; }

  // Clears all per-session state so a later Delete() cannot act on a stale
  // resource URL from a previous session.
  void Reset();

 private:
  // Serialises Create/PatchIce/Delete issued from different threads (backend
  // worker thread vs Disconnect()).
  mutable std::mutex io_mtx_;
  std::unique_ptr<cpp_network::http::Client> client_;
  std::chrono::milliseconds total_timeout_{10000};

  std::string session_id_;
  std::string answer_sdp_;
  std::string resource_url_;
  OnReadyHandler on_ready_;
  OnErrorHandler on_error_;
};

}  // namespace stream
}  // namespace video