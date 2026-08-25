#include "src/backend/webrtc/whip_session.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include <curl/curl.h>

#include "src/framework/core/log_slot.h"

namespace video {
namespace stream {

namespace {

// HTTP method constants
static constexpr const char* kMethodPost = "POST";
static constexpr const char* kMethodPatch = "PATCH";
static constexpr const char* kMethodDelete = "DELETE";

// HTTP header constants
static constexpr const char* kHeaderExpect = "Expect:";
static constexpr const char* kHeaderContentType = "Content-Type: ";

// MIME type constants
static constexpr const char* kMimeSdp = "application/sdp";
static constexpr const char* kMimeTrickleIce = "application/trickle-ice-sdpfrag";

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total = size * nmemb;
  reinterpret_cast<std::string*>(userp)->append(static_cast<char*>(contents), total);
  return total;
}

struct curl_slist* MakeHeaders(const std::string& content_type) {
  struct curl_slist* list = nullptr;
  list = curl_slist_append(list, kHeaderExpect);
  if (!content_type.empty()) {
    std::string ct = kHeaderContentType + content_type;
    list = curl_slist_append(list, ct.c_str());
  }
  return list;
}

std::string CurlEasyPerform(const std::string& url, const std::string& method,
                             const std::string& body, const std::string& content_type,
                             std::string* response_content_type = nullptr,
                             std::string* location = nullptr) {
  CURL* curl = curl_easy_init();
  if (!curl) return "";

  std::string response_body;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);

  struct curl_slist* headers = MakeHeaders(content_type);
  if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  if (method == kMethodPost) {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
  } else if (method == kMethodPatch) {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, kMethodPatch);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
  } else if (method == kMethodDelete) {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, kMethodDelete);
  }

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);

  if (res != CURLE_OK) {
    VC_LOG(video::codec::LogLevel::kError, std::string("curl error: ") + curl_easy_strerror(res));
    curl_easy_cleanup(curl);
    return "";
  }

  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  VC_LOG(video::codec::LogLevel::kDebug, std::string("HTTP ") + method + " -> " + std::to_string(http_code));

  if (http_code < 200 || http_code >= 300) {
    VC_LOG(video::codec::LogLevel::kError,
           std::string("unexpected status, body=") + response_body.substr(0, 200));
    curl_easy_cleanup(curl);
    return "";
  }

  if (response_content_type) {
    char* ct = nullptr;
    CURLcode r = curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
    if (r == CURLE_OK && ct) *response_content_type = ct;
  }

  if (location) {
    char* loc = nullptr;
    CURLcode r = curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &loc);
    if (r == CURLE_OK && loc) *location = loc;
    // Also check the Location header manually for 201 responses
    if (location->empty()) {
      char* effective = nullptr;
      curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective);
      (void)effective;
    }
  }

  curl_easy_cleanup(curl);
  return response_body;
}

}  // namespace

WhipSession::WhipSession() { curl_global_init(CURL_GLOBAL_ALL); }
WhipSession::~WhipSession() { curl_global_cleanup(); }

bool WhipSession::Create(const std::string& whip_endpoint,
                          const std::string& offer_sdp) {
  VC_LOG(video::codec::LogLevel::kDebug,
         std::string("Create: POST ") + whip_endpoint + " (" + std::to_string(offer_sdp.size()) + " bytes)");

  std::string content_type;
  std::string location;
  auto response = CurlEasyPerform(whip_endpoint, kMethodPost, offer_sdp,
                                   kMimeSdp, &content_type, &location);
  if (response.empty()) {
    VC_LOG(video::codec::LogLevel::kError, "POST failed");
    if (on_error_) on_error_("WHIP POST failed");
    return false;
  }

  VC_LOG(video::codec::LogLevel::kDebug,
         std::string("POST OK (") + std::to_string(response.size()) + " bytes, type=" + content_type + ")");
  answer_sdp_ = response;

  // Extract session ID from the Location header
  if (!location.empty()) {
    resource_url_ = location;
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

  std::ostringstream body;
  for (const auto& c : candidates) {
    body << "a=candidate:" << c.candidate << "\r\n";
  }

  auto response = CurlEasyPerform(url, kMethodPatch, body.str(), kMimeTrickleIce);
  return !response.empty() || response.empty();
}

bool WhipSession::Delete(const std::string& whip_endpoint,
                          const std::string& session_id) {
  std::string url = resource_url_.empty() ? whip_endpoint + "/" + session_id : resource_url_;
  CurlEasyPerform(url, kMethodDelete, "", "");
  return true;
}

}  // namespace stream
}  // namespace video