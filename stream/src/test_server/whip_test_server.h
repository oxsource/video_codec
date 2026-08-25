#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <rtc/rtc.hpp>

namespace video {
namespace stream {

struct ServerWhipSession {
  std::string id;
  std::shared_ptr<rtc::PeerConnection> pc;
  std::shared_ptr<rtc::Track> video_track;
  bool active = false;
};

struct SubscriberSession {
  std::string id;
  std::shared_ptr<rtc::PeerConnection> pc;
  std::shared_ptr<rtc::Track> video_track;
};

class WhipTestServer {
 public:
  // media_path: optional path to an Annex-B .h264 file or a directory of such
  // files. When set, the server broadcasts the file(s) in a loop to every
  // /subscribe client, letting the browser playback be verified independently
  // of any WHIP pusher/encoder.
  explicit WhipTestServer(uint16_t port, const std::string& media_path = "",
                           bool no_loop = false);
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
  std::string HandleWhepPost(const std::string& body);
  std::string HandleSubscribePost(const std::string& body);
  std::string HandleGetPlayer();
  std::string BuildPlayerPage();
  void ForwardToSubscribers(rtc::message_variant data);

  // File-player mode (--media): load + parse + broadcast.
  bool LoadMedia();
  void StartFilePlayer();

  uint16_t port_;
  std::string media_path_;
  bool no_loop_ = false;
  int server_fd_ = -1;
  bool running_ = false;
  std::unordered_map<std::string, ServerWhipSession> sessions_;
  uint64_t next_session_id_ = 1;
  uint64_t next_subscriber_id_ = 1;
  std::vector<std::shared_ptr<SubscriberSession>> subscribers_;
  // Guards subscribers_ (touched by the HTTP thread, the WHIP onMessage
  // callback and the file-player thread).
  std::mutex subscribers_mtx_;

  std::vector<std::vector<std::byte>> media_frames_;
  std::thread file_thread_;
  std::thread forward_thread_;
  std::vector<rtc::binary> forward_queue_;
  std::mutex forward_mtx_;
  std::condition_variable forward_cv_;
  bool forward_done_ = false;
};

}  // namespace stream
}  // namespace video