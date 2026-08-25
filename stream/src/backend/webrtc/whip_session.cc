#include "src/backend/webrtc/whip_session.h"

#include <cstring>
#include <sstream>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace video {
namespace stream {

namespace {

struct UrlParts {
  std::string host;
  std::string path;
  int port = 80;
};

UrlParts ParseUrl(const std::string& url) {
  UrlParts parts;
  auto pos = url.find("://");
  if (pos != std::string::npos) {
    auto rest = url.substr(pos + 3);
    auto colon = rest.find(':');
    auto slash = rest.find('/');
    if (colon != std::string::npos && colon < slash) {
      parts.port = std::stoi(rest.substr(colon + 1, slash - colon - 1));
      parts.host = rest.substr(0, colon);
    } else {
      parts.host = rest.substr(0, slash);
    }
    parts.path = slash != std::string::npos ? rest.substr(slash) : "/";
  } else {
    parts.host = url;
    parts.path = "/";
  }
  return parts;
}

}  // namespace

WhipSession::WhipSession() = default;
WhipSession::~WhipSession() = default;

std::string WhipSession::HttpPost(const std::string& url, const std::string& body,
                                   const std::string& content_type,
                                   std::string* response_content_type) {
  auto parsed = ParseUrl(url);

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return "";

  struct hostent* server = gethostbyname(parsed.host.c_str());
  if (!server) {
    close(sock);
    return "";
  }

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  std::memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
  addr.sin_port = htons(parsed.port);

  if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(sock);
    return "";
  }

  std::ostringstream req;
  req << "POST " << parsed.path << " HTTP/1.1\r\n"
      << "Host: " << parsed.host << "\r\n"
      << "Content-Type: " << content_type << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n"
      << "\r\n"
      << body;

  std::string request = req.str();
  send(sock, request.c_str(), request.size(), 0);

  shutdown(sock, SHUT_WR);

  char buf[4096];
  std::memset(buf, 0, sizeof(buf));
  std::string response;
  ssize_t n;
  while ((n = read(sock, buf, sizeof(buf) - 1)) > 0) {
    buf[n] = 0;
    response += buf;
  }
  close(sock);

  auto header_end = response.find("\r\n\r\n");
  if (header_end == std::string::npos) return "";

  std::string headers = response.substr(0, header_end);
  std::string resp_body = response.substr(header_end + 4);

  auto status_line = headers.substr(0, headers.find("\r\n"));
  std::printf("  [whip] HTTP response: %s\n", status_line.c_str());
  if (status_line.find("200") == std::string::npos &&
      status_line.find("201") == std::string::npos) {
    std::printf("  [whip] unexpected status, body=%s\n", resp_body.substr(0, 200).c_str());
    return "";
  }

  if (response_content_type) {
    auto ct_pos = headers.find("Content-Type:");
    if (ct_pos != std::string::npos) {
      auto ct_end = headers.find("\r\n", ct_pos);
      *response_content_type = headers.substr(ct_pos + 13, ct_end - ct_pos - 13);
    }
  }

  auto location = headers.find("Location:");
  if (location != std::string::npos) {
    auto loc_end = headers.find("\r\n", location);
    resource_url_ = headers.substr(location + 9, loc_end - location - 9);
  }

  return resp_body;
}

std::string WhipSession::HttpPatch(const std::string& url, const std::string& body,
                                    const std::string& content_type) {
  auto parsed = ParseUrl(url);

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return "";

  struct hostent* server = gethostbyname(parsed.host.c_str());
  if (!server) {
    close(sock);
    return "";
  }

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  std::memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
  addr.sin_port = htons(parsed.port);

  if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(sock);
    return "";
  }

  std::ostringstream req;
  req << "PATCH " << parsed.path << " HTTP/1.1\r\n"
      << "Host: " << parsed.host << "\r\n"
      << "Content-Type: " << content_type << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n"
      << "\r\n"
      << body;

  std::string request = req.str();
  send(sock, request.c_str(), request.size(), 0);

  shutdown(sock, SHUT_WR);

  char buf[4096];
  std::memset(buf, 0, sizeof(buf));
  std::string response;
  ssize_t n;
  while ((n = read(sock, buf, sizeof(buf) - 1)) > 0) {
    buf[n] = 0;
    response += buf;
  }
  close(sock);

  auto header_end = response.find("\r\n\r\n");
  if (header_end == std::string::npos) return "";
  return response.substr(header_end + 4);
}

std::string WhipSession::HttpDelete(const std::string& url) {
  auto parsed = ParseUrl(url);

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return "";

  struct hostent* server = gethostbyname(parsed.host.c_str());
  if (!server) {
    close(sock);
    return "";
  }

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  std::memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
  addr.sin_port = htons(parsed.port);

  if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(sock);
    return "";
  }

  std::ostringstream req;
  req << "DELETE " << parsed.path << " HTTP/1.1\r\n"
      << "Host: " << parsed.host << "\r\n"
      << "Connection: close\r\n"
      << "\r\n";

  std::string request = req.str();
  send(sock, request.c_str(), request.size(), 0);

  shutdown(sock, SHUT_WR);

  char buf[4096];
  std::memset(buf, 0, sizeof(buf));
  std::string response;
  ssize_t n;
  while ((n = read(sock, buf, sizeof(buf) - 1)) > 0) {
    buf[n] = 0;
    response += buf;
  }
  close(sock);

  auto header_end = response.find("\r\n\r\n");
  if (header_end == std::string::npos) return "";
  return response.substr(header_end + 4);
}

bool WhipSession::Create(const std::string& whip_endpoint,
                          const std::string& offer_sdp) {
  std::printf("  [whip] Create: POST %s (%zu bytes)\n", whip_endpoint.c_str(), offer_sdp.size());
  std::string content_type;
  auto response = HttpPost(whip_endpoint, offer_sdp, "application/sdp", &content_type);
  if (response.empty()) {
    std::printf("  [whip] POST failed (empty response)\n");
    if (on_error_) on_error_("WHIP POST failed");
    return false;
  }

  std::printf("  [whip] POST OK (%zu bytes, type=%s)\n", response.size(), content_type.c_str());
  answer_sdp_ = response;

  if (!resource_url_.empty()) {
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

  auto response = HttpPatch(url, body.str(), "application/trickle-ice-sdpfrag");
  return !response.empty() || response.empty();
}

bool WhipSession::Delete(const std::string& whip_endpoint,
                          const std::string& session_id) {
  std::string url = resource_url_.empty() ? whip_endpoint + "/" + session_id : resource_url_;
  HttpDelete(url);
  return true;
}

}  // namespace stream
}  // namespace video