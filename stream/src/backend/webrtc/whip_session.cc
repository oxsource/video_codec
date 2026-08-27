#include "src/backend/webrtc/whip_session.h"

#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "http/http_umbrella.h"

#include "src/framework/core/log_slot.h"

namespace video {
namespace stream {

using cpp_network::http::Client;
using cpp_network::http::ErrorCodeToString;
using cpp_network::http::Headers;
using cpp_network::http::Method;
using cpp_network::http::Options;
using cpp_network::http::Request;
using cpp_network::http::Response;
using cpp_network::http::Result;

namespace {

// MIME type constants
static constexpr const char* kMimeSdp = "application/sdp";
static constexpr const char* kMimeTrickleIce = "application/trickle-ice-sdpfrag";

// HTTP header constants
static constexpr const char* kHeaderExpect = "Expect";
static constexpr const char* kHeaderContentType = "Content-Type";
static constexpr const char* kHeaderLocation = "Location";

// Default timeouts (ms), matching the previous libcurl-based implementation.
static constexpr std::chrono::milliseconds kConnectTimeout{5000};
static constexpr std::chrono::milliseconds kTotalTimeout{10000};

Options MakeOptions() {
  Options options;
  options.SetConnectTimeout(kConnectTimeout);
  options.SetTotalTimeout(kTotalTimeout);
  options.SetFollowRedirects(false);
  return options;
}

Result<Request> MakeRequest(Method method, const std::string& url,
                            const std::string& body,
                            const std::string& content_type) {
  Request::Builder builder;
  builder.SetMethod(method).Url(url);
  // Empty Expect header suppresses libcurl's 100-continue handshake for the
  // SDP POST body (same behavior as the previous implementation).
  builder.Header(kHeaderExpect, "");
  if (!content_type.empty()) {
    builder.Header(kHeaderContentType, content_type);
  }
  builder.Timeout(kTotalTimeout);
  if (!body.empty()) {
    builder.Body(body);
  }
  return builder.Build();
}

}  // namespace

WhipSession::WhipSession() {
  Result<Client> client = Client::Create(MakeOptions());
  if (client.ok()) {
    client_ = std::make_unique<Client>(std::move(client.value()));
  } else {
    VC_LOG(video::codec::LogLevel::kError,
           std::string("cpp_network client create failed: ") +
               client.error().message());
  }
}

WhipSession::~WhipSession() = default;

bool WhipSession::Create(const std::string& whip_endpoint,
                          const std::string& offer_sdp) {
  VC_LOG(video::codec::LogLevel::kDebug,
         std::string("Create: POST ") + whip_endpoint + " (" + std::to_string(offer_sdp.size()) + " bytes)");

  if (!client_) return false;

  Result<Request> req = MakeRequest(Method::kPost, whip_endpoint, offer_sdp, kMimeSdp);
  if (!req.ok()) {
    VC_LOG(video::codec::LogLevel::kError,
           std::string("POST request build failed: ") + req.error().message());
    if (on_error_) on_error_("WHIP POST failed");
    return false;
  }

  Result<Response> resp = client_->Send(req.value());
  if (!resp.ok()) {
    VC_LOG(video::codec::LogLevel::kError,
           std::string("POST failed: [") + ErrorCodeToString(resp.error().code()) +
               "] " + resp.error().message());
    if (on_error_) on_error_("WHIP POST failed");
    return false;
  }

  const Response& response = resp.value();
  VC_LOG(video::codec::LogLevel::kDebug,
         std::string("POST -> ") + std::to_string(response.status()) + " (" +
             std::to_string(response.body().size()) + " bytes)");

  if (!response.ok() || response.body().empty()) {
    VC_LOG(video::codec::LogLevel::kError,
           std::string("unexpected status, body=") + response.body().substr(0, 200));
    if (on_error_) on_error_("WHIP POST failed");
    return false;
  }

  answer_sdp_ = response.body();

  // Extract session ID from the Location header (WHIP resource URL).
  if (auto location = response.GetHeader(kHeaderLocation)) {
    resource_url_ = *location;
    auto last_slash = resource_url_.rfind('/');
    if (last_slash != std::string::npos) {
      session_id_ = resource_url_.substr(last_slash + 1);
    }
  }

  if (on_ready_) on_ready_(answer_sdp_);
  return true;
}

bool WhipSession::PatchIce(const std::string& whip_endpoint,
                            const std::string& session_id,
                            const std::vector<WhipIceCandidate>& candidates) {
  std::string url = resource_url_.empty() ? whip_endpoint + "/" + session_id : resource_url_;
  if (!client_) return false;

  std::ostringstream body;
  for (const auto& c : candidates) {
    body << "a=candidate:" << c.candidate << "\r\n";
  }

  Result<Request> req = MakeRequest(Method::kPatch, url, body.str(), kMimeTrickleIce);
  if (!req.ok()) {
    VC_LOG(video::codec::LogLevel::kError,
           std::string("PATCH request build failed: ") + req.error().message());
    return false;
  }

  Result<Response> resp = client_->Send(req.value());
  if (!resp.ok()) {
    VC_LOG(video::codec::LogLevel::kError,
           std::string("PATCH failed: [") + ErrorCodeToString(resp.error().code()) +
               "] " + resp.error().message());
    return false;
  }
  return resp.value().ok();
}

bool WhipSession::Delete(const std::string& whip_endpoint,
                          const std::string& session_id) {
  std::string url = resource_url_.empty() ? whip_endpoint + "/" + session_id : resource_url_;
  if (!client_) return false;

  Result<Request> req = MakeRequest(Method::kDelete, url, "", "");
  if (!req.ok()) {
    VC_LOG(video::codec::LogLevel::kError,
           std::string("DELETE request build failed: ") + req.error().message());
    return false;
  }

  Result<Response> resp = client_->Send(req.value());
  if (!resp.ok()) {
    VC_LOG(video::codec::LogLevel::kError,
           std::string("DELETE failed: [") + ErrorCodeToString(resp.error().code()) +
               "] " + resp.error().message());
    return false;
  }
  return resp.value().ok();
}

}  // namespace stream
}  // namespace video
