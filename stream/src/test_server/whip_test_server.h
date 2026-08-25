#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <rtc/rtc.hpp>

namespace video {
namespace stream {

struct ServerWhipSession {
  std::string id;
  std::shared_ptr<rtc::PeerConnection> pc;
  std::shared_ptr<rtc::Track> video_track;
  bool active = false;
};

class WhipTestServer {
 public:
  explicit WhipTestServer(uint16_t port);
  ~WhipTestServer();

  bool Start();
  void Stop();
  uint16_t Port() const { return port_; }

  void Run();

 private:
  bool HandleRequest(int client_fd);
  std::string HandleWhipPost(const std::string& body);
  std::string HandleWhipPatch(const std::string& session_id, const std::string& body);
  std::string HandleWhipDelete(const std::string& session_id);
  std::string HandleGetPlayer();
  std::string BuildPlayerPage();

  uint16_t port_;
  int server_fd_ = -1;
  bool running_ = false;
  std::unordered_map<std::string, ServerWhipSession> sessions_;
  uint64_t next_session_id_ = 1;
};

}  // namespace stream
}  // namespace video