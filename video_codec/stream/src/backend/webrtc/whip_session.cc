// Module log tag: identifies all VC_LOG output from this file (see
// log_slot.h). Must be defined before the first header include so the
// framework's LOG_TAG mechanism picks it up instead of defaulting to __FILE__.
#define LOG_TAG "whip_session"

#include "stream/src/backend/webrtc/whip_session.h"

#include <cctype>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "codec/src/framework/core/log_slot.h"
#include "http/http_umbrella.h"

namespace video {
namespace stream {

using cpp_network::comm::ErrorCodeToString;
using cpp_network::comm::Options;
using cpp_network::comm::Result;
using cpp_network::comm::Tls;
using cpp_network::comm::VerifyMode;
using cpp_network::http::Client;
using cpp_network::http::Headers;
using cpp_network::http::Method;
using cpp_network::http::Request;
using cpp_network::http::Response;

namespace {

// MIME type constants
static constexpr const char* kMimeSdp = "application/sdp";
static constexpr const char* kMimeTrickleIce =
    "application/trickle-ice-sdpfrag";

// HTTP header constants
static constexpr const char* kHeaderExpect = "Expect";
static constexpr const char* kHeaderContentType = "Content-Type";
static constexpr const char* kHeaderLocation = "Location";

std::string TrimLeft(const std::string& in) {
  size_t start = in.find_first_not_of(" \t\r\n");
  return start == std::string::npos ? std::string() : in.substr(start);
}

bool StartsWithCaseInsensitive(const std::string& haystack,
                               const std::string& needle) {
  if (haystack.size() < needle.size()) return false;
  for (size_t i = 0; i < needle.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(haystack[i])) !=
        std::tolower(static_cast<unsigned char>(needle[i]))) {
      return false;
    }
  }
  return true;
}

// Turns the WHIP Location header into an absolute URL. Absolute locations are
// returned unchanged; relative ones (e.g. "/whip/xxx/whipsession/yyy") are
// joined onto the scheme://host[:port] origin derived from base_url.
std::string ResolveLocation(const std::string& base_url,
                            const std::string& location) {
  if (StartsWithCaseInsensitive(location, "http://") ||
      StartsWithCaseInsensitive(location, "https://")) {
    return location;
  }
  size_t scheme_end = base_url.find("://");
  if (scheme_end == std::string::npos) {
    VC_LOG(video::codec::LogLevel::kError,
           std::string("cannot resolve relative Location '") + location +
               "' against non-URL base '" + base_url + "'");
    return location;
  }
  size_t origin_end = base_url.find('/', scheme_end + 3);
  std::string origin = origin_end == std::string::npos
                           ? base_url
                           : base_url.substr(0, origin_end);
  if (!location.empty() && location[0] == '/') {
    return origin + location;
  }
  return origin + "/" + location;
}

Options MakeOptions(const NetworkConfig& network) {
  Options options;
  options.SetConnectTimeout(
      std::chrono::milliseconds(network.connect_timeout_ms));
  options.SetTotalTimeout(std::chrono::milliseconds(network.total_timeout_ms));
  if (network.read_timeout_ms > 0) {
    options.SetReadTimeout(std::chrono::milliseconds(network.read_timeout_ms));
  }
  if (network.write_timeout_ms > 0) {
    options.SetWriteTimeout(
        std::chrono::milliseconds(network.write_timeout_ms));
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

void WhipSession::Reset() {
  std::lock_guard<std::mutex> lock(io_mtx_);
  session_id_.clear();
  answer_sdp_.clear();
  resource_url_.clear();
}

bool WhipSession::Create(const std::string& whip_endpoint,
                         const std::string& offer_sdp) {
  VC_LOG(video::codec::LogLevel::kDebug,
         std::string("Create: POST ") + whip_endpoint + " (" +
             std::to_string(offer_sdp.size()) + " bytes)");

  OnReadyHandler ready;
  OnErrorHandler error;
  bool ok = false;
  {
    std::lock_guard<std::mutex> lock(io_mtx_);
    // Clear stale state from any previous session up-front.
    session_id_.clear();
    answer_sdp_.clear();
    resource_url_.clear();
    ready = on_ready_;
    error = on_error_;

    if (!client_) {
      if (error) error("WHIP client not initialized");
      return false;
    }

    Result<Request> req = MakeRequest(Method::kPost, whip_endpoint, offer_sdp,
                                      kMimeSdp, total_timeout_);
    if (!req.ok()) {
      VC_LOG(
          video::codec::LogLevel::kError,
          std::string("POST request build failed: ") + req.error().message());
      if (error) error("WHIP POST failed");
      return false;
    }

    Result<Response> resp = client_->Send(req.value());
    if (!resp.ok()) {
      VC_LOG(video::codec::LogLevel::kError,
             std::string("POST failed: [") +
                 ErrorCodeToString(resp.error().code()) + "] " +
                 resp.error().message());
      if (error) error("WHIP POST failed");
      return false;
    }

    const Response& response = resp.value();
    VC_LOG(video::codec::LogLevel::kDebug,
           std::string("POST -> status ") + std::to_string(response.status()) +
               " (" + std::to_string(response.body().size()) + " bytes body)");

    if (response.status() != 201) {
      VC_LOG(video::codec::LogLevel::kError,
             std::string("WHIP POST expected 201 Created, got ") +
                 std::to_string(response.status()) +
                 ", body=" + response.body().substr(0, 200));
      if (error)
        error("WHIP POST unexpected status " +
              std::to_string(response.status()));
      return false;
    }

    if (auto content_type = response.GetHeader(kHeaderContentType)) {
      if (!StartsWithCaseInsensitive(*content_type, kMimeSdp)) {
        VC_LOG(video::codec::LogLevel::kError,
               std::string("WHIP POST returned unexpected Content-Type: ") +
                   *content_type);
        if (error) error("WHIP POST invalid Content-Type: " + *content_type);
        return false;
      }
    }

    std::string body = TrimLeft(response.body());
    if (body.empty()) {
      VC_LOG(video::codec::LogLevel::kError,
             "WHIP POST returned an empty body");
      if (error) error("WHIP POST returned an empty body");
      return false;
    }
    if (!StartsWithCaseInsensitive(body, "v=0")) {
      VC_LOG(video::codec::LogLevel::kError,
             std::string("WHIP POST body is not a valid SDP (missing v=0): ") +
                 body.substr(0, 200));
      if (error) error("WHIP POST returned a non-SDP body");
      return false;
    }

    answer_sdp_ = body;

    auto location = response.GetHeader(kHeaderLocation);
    if (!location) {
      VC_LOG(video::codec::LogLevel::kWarn,
             "WHIP POST response is missing the Location header (session "
             "cannot be torn down later)");
      if (error) error("WHIP POST missing Location header");
      return false;
    }
    resource_url_ = ResolveLocation(whip_endpoint, *location);
    VC_LOG(video::codec::LogLevel::kDebug, std::string("WHIP Location raw='") +
                                               *location + "' resolved='" +
                                               resource_url_ + "'");

    auto last_slash = resource_url_.rfind('/');
    if (last_slash != std::string::npos) {
      session_id_ = resource_url_.substr(last_slash + 1);
    }
    ok = true;
  }

  if (!ok) return false;
  if (ready) ready(answer_sdp_);
  return true;
}

bool WhipSession::PatchIce(const std::string& whip_endpoint,
                           const std::string& session_id,
                           const std::vector<WhipIceCandidate>& candidates) {
  std::string url;
  {
    std::lock_guard<std::mutex> lock(io_mtx_);
    url = resource_url_;
  }
  if (url.empty()) {
    VC_LOG(video::codec::LogLevel::kWarn,
           "WHIP PatchIce skipped: no resource URL (session was never "
           "created)");
    return false;
  }
  (void)whip_endpoint;
  (void)session_id;

  std::ostringstream body;
  for (const auto& c : candidates) {
    body << "a=candidate:" << c.candidate << "\r\n";
  }

  std::lock_guard<std::mutex> lock(io_mtx_);
  if (!client_) return false;
  Result<Request> req = MakeRequest(Method::kPatch, url, body.str(),
                                    kMimeTrickleIce, total_timeout_);
  if (!req.ok()) {
    VC_LOG(video::codec::LogLevel::kError,
           std::string("PATCH request build failed: ") + req.error().message());
    return false;
  }

  Result<Response> resp = client_->Send(req.value());
  if (!resp.ok()) {
    VC_LOG(video::codec::LogLevel::kError,
           std::string("PATCH failed: [") +
               ErrorCodeToString(resp.error().code()) + "] " +
               resp.error().message());
    return false;
  }
  return resp.value().ok();
}

bool WhipSession::Delete() {
  std::string url;
  {
    std::lock_guard<std::mutex> lock(io_mtx_);
    url = resource_url_;
  }
  if (url.empty()) {
    VC_LOG(video::codec::LogLevel::kWarn,
           "WHIP Delete skipped: no resource URL (session was never created)");
    return false;
  }

  VC_LOG(video::codec::LogLevel::kDebug, std::string("Delete: DELETE ") + url);
  {
    std::lock_guard<std::mutex> lock(io_mtx_);
    if (!client_) {
      VC_LOG(video::codec::LogLevel::kWarn,
             "WHIP Delete skipped: client not initialized");
      // Drop the stale reference regardless; there is nothing to retry.
      resource_url_.clear();
      session_id_.clear();
      return false;
    }
    Result<Request> req =
        MakeRequest(Method::kDelete, url, "", "", total_timeout_);
    if (!req.ok()) {
      VC_LOG(
          video::codec::LogLevel::kError,
          std::string("DELETE request build failed: ") + req.error().message());
      resource_url_.clear();
      session_id_.clear();
      return false;
    }

    Result<Response> resp = client_->Send(req.value());
    // Best effort: drop the reference either way so a later Delete() cannot
    // resend (or DELETE a stale session from a previous attempt).
    resource_url_.clear();
    session_id_.clear();
    if (!resp.ok()) {
      VC_LOG(video::codec::LogLevel::kError,
             std::string("DELETE failed: [") +
                 ErrorCodeToString(resp.error().code()) + "] " +
                 resp.error().message());
      return false;
    }
    VC_LOG(video::codec::LogLevel::kDebug,
           std::string("DELETE -> ") + std::to_string(resp.value().status()));
    return resp.value().ok();
  }
}

}  // namespace stream
}  // namespace video
