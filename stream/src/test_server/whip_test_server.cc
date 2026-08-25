#include "src/test_server/whip_test_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>

#include <rtc/rtc.hpp>
#include <rtc/rtcpreceivingsession.hpp>

namespace video {
namespace stream {

WhipTestServer::WhipTestServer(uint16_t port) : port_(port) {
  rtc::InitLogger(rtc::LogLevel::Warning);
  // For debugging: rtc::InitLogger(rtc::LogLevel::Verbose);
}

WhipTestServer::~WhipTestServer() {
  Stop();
}

bool WhipTestServer::Start() {
  server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) return false;

  int opt = 1;
  setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port_);

  if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(server_fd_);
    return false;
  }

  if (listen(server_fd_, 5) < 0) {
    close(server_fd_);
    return false;
  }

  running_ = true;
  return true;
}

void WhipTestServer::Stop() {
  running_ = false;
  for (auto& [id, session] : sessions_) {
    if (session.pc) session.pc->close();
  }
  sessions_.clear();
  if (server_fd_ >= 0) {
    close(server_fd_);
    server_fd_ = -1;
  }
}

bool WhipTestServer::HandleRequest(int client_fd) {
  char buf[4096];
  std::memset(buf, 0, sizeof(buf));
  ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
  if (n <= 0) return false;

  std::string request(buf);
  std::string method, path, version;
  std::istringstream req_stream(request);
  req_stream >> method >> path >> version;

  std::string status_line;
  std::string content_type = "text/plain";
  std::string body;

  auto body_pos = request.find("\r\n\r\n");
  std::string req_body;
  if (body_pos != std::string::npos) {
    req_body = request.substr(body_pos + 4);
  }

  if (method == "POST" && path.find("/whip") == 0) {
    body = HandleWhipPost(req_body);
    status_line = "201 Created";
    content_type = "application/sdp";
  } else if (method == "PATCH" && path.find("/whip/") == 0) {
    auto session_id = path.substr(6);
    body = HandleWhipPatch(session_id, req_body);
    status_line = "200 OK";
    content_type = "application/trickle-ice-sdpfrag";
  } else if (method == "DELETE" && path.find("/whip/") == 0) {
    auto session_id = path.substr(6);
    body = HandleWhipDelete(session_id);
    status_line = "200 OK";
  } else if (method == "GET" && (path == "/" || path == "/index.html")) {
    body = HandleGetPlayer();
    status_line = "200 OK";
    content_type = "text/html";
  } else {
    status_line = "404 Not Found";
    body = "Not Found";
  }

  std::ostringstream response_stream;
  response_stream << "HTTP/1.1 " << status_line << "\r\n"
                  << "Content-Type: " << content_type << "\r\n"
                  << "Content-Length: " << body.size() << "\r\n"
                  << "Access-Control-Allow-Origin: *\r\n"
                  << "Access-Control-Allow-Methods: POST, PATCH, DELETE, GET, OPTIONS\r\n"
                  << "Access-Control-Allow-Headers: *\r\n"
                  << "\r\n"
                  << body;

  std::string response = response_stream.str();
  write(client_fd, response.c_str(), response.size());
  close(client_fd);
  return true;
}

std::string WhipTestServer::HandleWhipPost(const std::string& body) {
  auto session_id = "session_" + std::to_string(next_session_id_++);
  std::printf("  [server] WHIP POST: creating session %s (body=%zu bytes)\n",
              session_id.c_str(), body.size());

  rtc::Configuration pc_config;
  pc_config.iceServers.emplace_back("stun:stun.l.google.com:19302");

  auto pc = std::make_shared<rtc::PeerConnection>(pc_config);

  struct AnswerState {
    std::mutex mtx;
    std::condition_variable cv;
    std::string answer_sdp;
    bool answer_ready = false;
  };
  auto state = std::make_shared<AnswerState>();

  // Pre-create and store the session so the onTrack callback can find it.
  ServerWhipSession session;
  session.id = session_id;
  session.pc = pc;
  sessions_[session_id] = std::move(session);

  pc->onStateChange([this, session_id](rtc::PeerConnection::State state) {
    std::printf("  [server] session %s state change: %d\n", session_id.c_str(), static_cast<int>(state));
    if (state == rtc::PeerConnection::State::Connected) {
      auto it = sessions_.find(session_id);
      if (it != sessions_.end()) {
        it->second.active = true;
      }
    }
  });

pc->onTrack([this, session_id, pc](std::shared_ptr<rtc::Track> track) {
    std::printf("  [server] session %s got track\n", session_id.c_str());
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      it->second.video_track = track;
    }
    auto rtcp_session = std::make_shared<rtc::RtcpReceivingSession>();
    track->setMediaHandler(rtcp_session);
    track->onMessage([](rtc::binary message) {
      std::printf("  [server] track message: %zu bytes\n", message.size());
    }, nullptr);
  });

  pc->onLocalDescription([state](const rtc::Description& description) {
    std::lock_guard<std::mutex> lock(state->mtx);
    state->answer_sdp = std::string(description);
    std::printf("  [server] initial answer (%zu bytes), waiting for gathering...\n",
                state->answer_sdp.size());
  });

  pc->onGatheringStateChange([state, pc](rtc::PeerConnection::GatheringState gstate) {
    std::printf("  [server] gathering state: %d\n", static_cast<int>(gstate));
    if (gstate == rtc::PeerConnection::GatheringState::Complete) {
      auto local_desc = pc->localDescription();
      if (local_desc) {
        std::lock_guard<std::mutex> lock(state->mtx);
        state->answer_sdp = std::string(*local_desc);
        state->answer_ready = true;
        std::printf("  [server] final answer ready after gathering (%zu bytes)\n",
                    state->answer_sdp.size());
        state->cv.notify_one();
      }
    }
  });

  try {
    pc->setRemoteDescription(std::string(body));
    std::printf("  [server] setRemoteDescription OK\n");
  } catch (const std::exception& e) {
    std::printf("  [server] setRemoteDescription FAILED: %s\n", e.what());
    return "";
  }

  pc->setLocalDescription();
  std::printf("  [server] setLocalDescription called, waiting for answer...\n");

  {
    std::unique_lock<std::mutex> lock(state->mtx);
    if (state->cv.wait_for(lock, std::chrono::seconds(5), [&] { return state->answer_ready; })) {
      std::printf("  [server] answer SDP ready (%zu bytes)\n", state->answer_sdp.size());
    } else {
      std::printf("  [server] timeout waiting for answer\n");
    }
  }

  return state->answer_sdp;
}

std::string WhipTestServer::HandleWhipPatch(const std::string& session_id, const std::string& body) {
  auto it = sessions_.find(session_id);
  if (it != sessions_.end() && it->second.pc) {
    (void)body;
  }
  return "";
}

std::string WhipTestServer::HandleWhipDelete(const std::string& session_id) {
  auto it = sessions_.find(session_id);
  if (it != sessions_.end()) {
    if (it->second.pc) it->second.pc->close();
    sessions_.erase(it);
  }
  return "";
}

std::string WhipTestServer::HandleGetPlayer() {
  return BuildPlayerPage();
}

std::string WhipTestServer::BuildPlayerPage() {
  return R"raw(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>WHIP Stream Player</title>
  <style>
    body { font-family: sans-serif; text-align: center; padding: 20px; background: #1a1a2e; color: #eee; }
    video { width: 80%; max-width: 854px; background: #000; border-radius: 8px; margin: 20px auto; display: block; }
    .status { margin: 10px; padding: 10px; border-radius: 4px; }
    .connected { background: #1b5e20; }
    .disconnected { background: #b71c1c; }
    .connecting { background: #e65100; }
    h1 { color: #e94560; }
  </style>
</head>
<body>
  <h1>WHIP Stream Player</h1>
  <video id="player" autoplay muted playsinline></video>
  <div id="status" class="status disconnected">Disconnected</div>
  <script>
    const pc = new RTCPeerConnection({
      iceServers: [{ urls: 'stun:stun.l.google.com:19302' }]
    });
    const player = document.getElementById('player');
    const statusDiv = document.getElementById('status');

    pc.ontrack = (event) => {
      player.srcObject = event.streams[0];
      statusDiv.textContent = 'Streaming';
      statusDiv.className = 'status connected';
    };

    pc.oniceconnectionstatechange = () => {
      if (pc.iceConnectionState === 'disconnected' || pc.iceConnectionState === 'failed') {
        statusDiv.textContent = 'Disconnected';
        statusDiv.className = 'status disconnected';
      } else if (pc.iceConnectionState === 'connected') {
        statusDiv.textContent = 'Connected';
        statusDiv.className = 'status connected';
      } else {
        statusDiv.textContent = pc.iceConnectionState;
        statusDiv.className = 'status connecting';
      }
    };

    statusDiv.textContent = 'Waiting for stream...';
    statusDiv.className = 'status connecting';

    player.addEventListener('loadedmetadata', () => {
      player.play();
    });
  </script>
</body>
</html>)raw";
}

void WhipTestServer::Run() {
  while (running_) {
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) continue;
    HandleRequest(client_fd);
  }
}

}  // namespace stream
}  // namespace video