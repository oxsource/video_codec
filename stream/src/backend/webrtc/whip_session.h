#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/api/stream_config.h"

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

  WhipSession();
  ~WhipSession();

  bool Create(const std::string& whip_endpoint, const std::string& offer_sdp);
  bool PatchIce(const std::string& whip_endpoint, const std::string& session_id,
                const std::vector<WhipIceCandidate>& candidates);
  bool Delete(const std::string& whip_endpoint, const std::string& session_id);

  void SetOnReady(OnReadyHandler handler) { on_ready_ = std::move(handler); }
  void SetOnError(OnErrorHandler handler) { on_error_ = std::move(handler); }

  const std::string& SessionId() const { return session_id_; }
  const std::string& AnswerSdp() const { return answer_sdp_; }

 private:
  std::string HttpPost(const std::string& url, const std::string& body,
                       const std::string& content_type,
                       std::string* response_content_type = nullptr);
  std::string HttpPatch(const std::string& url, const std::string& body,
                        const std::string& content_type);
  std::string HttpDelete(const std::string& url);

  std::string session_id_;
  std::string answer_sdp_;
  std::string resource_url_;
  OnReadyHandler on_ready_;
  OnErrorHandler on_error_;
};

}  // namespace stream
}  // namespace video