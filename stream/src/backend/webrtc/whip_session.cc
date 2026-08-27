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
using cpp_network::http::Tls;
using cpp_network::http::VerifyMode;

namespace {

// MIME type constants
static constexpr const char* kMimeSdp = "application/sdp";
static constexpr const char* kMimeTrickleIce = "application/trickle-ice-sdpfrag";

// HTTP header constants
static constexpr const char* kHeaderExpect = "Expect";
static constexpr const char* kHeaderContentType = "Content-Type";
static constexpr const char* kHeaderLocation = "Location";

Options MakeOptions(const NetworkConfig& network) {
  Options options;
  options.SetConnectTimeout(std::chrono::milliseconds(network.connect_timeout_ms));
  options.SetTotalTimeout(std::chrono::milliseconds(network.total_timeout_ms));
  if (network.read_timeout_ms > 0) {
    options.SetReadTimeout(std::chrono::milliseconds(network.read_timeout_ms));
  }
  if (network.write_timeout_ms > 0) {
    options.SetWriteTimeout(std::chrono::milliseconds(network.write_timeout_ms));
  }
  options.SetFollowRedirects(network.follow_redirects);
  options.SetMaxRedirects(network.max_redirects);

  if (!network.local_address.empty()) {
    options.SetLocalAddress(network.local_address);
    if (network.local_port != 0) {
      options.SetLocalPort(network.local_port);
    }
  }
  if (!network.proxy_host.empty()) {
    options.SetProxy(network.proxy_host, network.proxy_port);
  }
  if (network.keep_alive_ms > 0) {
    options.SetKeepAlive(std::chrono::milliseconds(network.keep_alive_ms));
  }

  Tls::Builder tls;
  tls.SetVerifyMode(network.tls_verify ? VerifyMode::kVerifyPeer
                                       : VerifyMode::kSkipVerification);
  if (!network.tls_ca_file.empty()) {
    tls.SetCaFile(network.tls_ca_file);
  } else if (!network.tls_ca_pem.empty()) {
    tls.SetCaPem(network.tls_ca_pem);
  }
  if (!network.tls_client_cert.empty() && !network.tls_client_key.empty()) {
    tls.SetCertificate(network.tls_client_cert, network.tls_client_key);
  }
  if (!network.tls_sni.empty()) {
    tls.SetSni(network.tls_sni);
  }
  options.SetTls(tls.Build());

  return options;
}

Result<Request> MakeRequest(Method method, const std::string& url,
                            const std::string& body,
                            const std::string& content_type,
                            std::chrono::milliseconds timeout) {
  Request::Builder builder;
  builder.SetMethod(method).Url(url);
  // Empty Expect header suppresses libcurl's 100-continue handshake for the
  // SDP POST body.
  builder.Header(kHeaderExpect, "");
  if (!content_type.empty()) {
    builder.Header(kHeaderContentType, content_type);
  }
  builder.Timeout(timeout);
  if (!body.empty()) {
    builder.Body(body);
  }
  return builder.Build();
}

}  // namespace

WhipSession::WhipSession(const NetworkConfig& network)
    : total_timeout_(std::chrono::milliseconds(network.total_timeout_ms)) {
  Result<Client> client = Client::Create(MakeOptions(network));
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

  Result<Request> req = MakeRequest(Method::kPost, whip_endpoint, offer_sdp, kMimeSdp,
                                    total_timeout_);
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

  Result<Request> req = MakeRequest(Method::kPatch, url, body.str(), kMimeTrickleIce,
                                    total_timeout_);
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

  Result<Request> req = MakeRequest(Method::kDelete, url, "", "", total_timeout_);
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
