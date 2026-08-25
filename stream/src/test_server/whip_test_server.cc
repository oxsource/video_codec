#include "src/test_server/whip_test_server.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#include <rtc/rtc.hpp>

namespace video {
namespace stream {

WhipTestServer::WhipTestServer(uint16_t port, const std::string& media_path, bool no_loop)
    : port_(port), media_path_(media_path), no_loop_(no_loop) {
  rtc::InitLogger(rtc::LogLevel::Warning);
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

  // Read back the actual bound port (e.g. when constructed with port 0 so the
  // OS picks an ephemeral one) — Port() must reflect reality.
  socklen_t addr_len = sizeof(addr);
  if (getsockname(server_fd_, (struct sockaddr*)&addr, &addr_len) == 0) {
    port_ = ntohs(addr.sin_port);
  }

  if (!media_path_.empty()) {
    if (!LoadMedia()) {
      std::printf("  [server] FAILED to load media from %s\n", media_path_.c_str());
      return false;
    }
    StartFilePlayer();
  }

  running_ = true;
  return true;
}

void WhipTestServer::Stop() {
  running_ = false;
  {
    std::lock_guard<std::mutex> lock(forward_mtx_);
    forward_done_ = true;
  }
  forward_cv_.notify_one();
  for (auto& [id, session] : sessions_) {
    if (session.pc) session.pc->close();
  }
  sessions_.clear();
  {
    std::lock_guard<std::mutex> lock(subscribers_mtx_);
    for (auto& sub : subscribers_) {
      if (sub->pc) sub->pc->close();
    }
    subscribers_.clear();
  }
  if (server_fd_ >= 0) {
    close(server_fd_);
    server_fd_ = -1;
  }
}

bool WhipTestServer::HandleRequest(int client_fd) {
  // Read the full request. A browser offer SDP can arrive in several TCP
  // segments, so a single read() would truncate it — read until the headers
  // are complete, then consume the advertised Content-Length.
  std::string request;
  char buf[4096];
  ssize_t n;
  std::string::size_type header_end = std::string::npos;
  size_t content_length = 0;
  while (request.size() < 64 * 1024) {
    n = read(client_fd, buf, sizeof(buf));
    if (n <= 0) return false;
    request.append(buf, n);
    if (header_end == std::string::npos) {
      header_end = request.find("\r\n\r\n");
      if (header_end != std::string::npos) {
        auto cl_pos = request.find("Content-Length:");
        if (cl_pos != std::string::npos && cl_pos < header_end) {
          auto cl_end = request.find("\r\n", cl_pos);
          content_length = static_cast<size_t>(std::atol(
              request.substr(cl_pos + 15, cl_end - cl_pos - 15).c_str()));
        }
        if (request.size() - (header_end + 4) >= content_length) break;
      }
    } else if (request.size() - (header_end + 4) >= content_length) {
      break;
    }
  }

  std::string method, path, version;
  std::istringstream req_stream(request);
  req_stream >> method >> path >> version;

  std::string status_line;
  std::string content_type = "text/plain";
  std::string body;
  std::string response_extra;

  std::string req_body;
  if (header_end != std::string::npos) {
    req_body = request.substr(header_end + 4, content_length);
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
  } else if (method == "POST" && path == "/subscribe") {
    body = HandleSubscribePost(req_body);
    status_line = "201 Created";
    content_type = "application/sdp";
  } else if (method == "POST" && path == "/whep") {
    body = HandleWhepPost(req_body);
    status_line = "201 Created";
    content_type = "application/sdp";
    // WHEP protocol requires a Location header with the session resource URL.
    // The session ID is stored by HandleSubscribePost (which HandleWhepPost delegates to).
    // Use the session ID from the body prefix (set by HandleWhepPost).
    auto loc_end = body.find('\n');
    std::string loc = (loc_end != std::string::npos) ? body.substr(0, loc_end) : "";
    body = (loc_end != std::string::npos) ? body.substr(loc_end + 1) : body;
    response_extra = "Location: /whep/" + loc + "\r\n";
  } else if (method == "PATCH" && path.find("/whep/") == 0) {
    // WHEP ICE candidate exchange (trickle ICE). Accept and ignore.
    body = "";
    status_line = "200 OK";
    content_type = "application/trickle-ice-sdpfrag";
  } else if (method == "DELETE" && path.find("/whep/") == 0) {
    // WHEP session teardown. The subscriber is cleaned up on disconnect.
    body = "";
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
                  << response_extra
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
    track->onMessage([this](rtc::binary message) {
      size_t size = message.size();
      std::printf("  [server] track message: %zu bytes, forwarding to %zu subscribers\n",
                  size, subscribers_.size());
      ForwardToSubscribers(std::move(message));
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

  // No explicit setLocalDescription(): libdatachannel auto-negotiates an answer
  // inside setRemoteDescription() when receiving an offer (auto-negotiation is
  // on by default). Calling setLocalDescription() again here would generate a
  // SECOND description — an offer (setup:actpass), because the signaling state
  // is already Stable — which the peer then rejects as an invalid answer.
  std::printf("  [server] waiting for auto-negotiated answer...\n");

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

std::string WhipTestServer::HandleWhepPost(const std::string& body) {
  auto sub_id = "sub_" + std::to_string(next_subscriber_id_);
  std::string answer = HandleSubscribePost(body);
  // Prepend the session ID so HandleRequest can extract it for the Location header.
  return sub_id + "\n" + answer;
}

std::string WhipTestServer::HandleSubscribePost(const std::string& body) {
  auto sub_id = "sub_" + std::to_string(next_subscriber_id_++);
  std::printf("  [server] SUBSCRIBE: creating subscriber %s (body=%zu bytes)\n",
              sub_id.c_str(), body.size());

  std::string mid = "0";
  std::istringstream sdp_stream(body);
  std::string line;
  while (std::getline(sdp_stream, line)) {
    if (line.find("a=mid:") == 0) {
      mid = line.substr(6);
      while (!mid.empty() && (mid.back() == '\r' || mid.back() == '\n'))
        mid.pop_back();
      std::printf("  [server]   -> found mid=\"%s\"\n", mid.c_str());
      break;
    }
  }

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

  // Add track BEFORE setRemoteDescription so the auto-negotiation inside
  // setRemoteDescription includes it in the answer.  Adding the track after
  // would trigger a renegotiation and setLocalDescription would create an
  // offer (setup:actpass) instead of an answer.
  rtc::Description::Video media(mid, rtc::Description::Direction::SendOnly);
  // media.addH264Codec(96, "42e01f");
  // media.addH264Codec(96);
  media.addH264Codec(96, "profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1");
  media.addSSRC(42, "video-send", "stream1", "video-send");
  auto track = pc->addTrack(media);

  auto sub = std::make_shared<SubscriberSession>();
  sub->id = sub_id;
  sub->pc = pc;
  sub->video_track = track;
  {
    std::lock_guard<std::mutex> lock(subscribers_mtx_);
    subscribers_.push_back(sub);
  }

  pc->onStateChange([sub_id](rtc::PeerConnection::State state) {
    std::printf("  [server] subscriber %s state change: %d\n", sub_id.c_str(), static_cast<int>(state));
  });

  pc->onLocalDescription([state](const rtc::Description& description) {
    std::lock_guard<std::mutex> lock(state->mtx);
    state->answer_sdp = std::string(description);
    std::printf("  [server] subscriber initial answer (%zu bytes)\n", state->answer_sdp.size());
  });

  pc->onGatheringStateChange([state, pc](rtc::PeerConnection::GatheringState gstate) {
    std::printf("  [server] subscriber gathering state: %d\n", static_cast<int>(gstate));
    if (gstate == rtc::PeerConnection::GatheringState::Complete) {
      auto local_desc = pc->localDescription();
      if (local_desc) {
        std::lock_guard<std::mutex> lock(state->mtx);
        state->answer_sdp = std::string(*local_desc);
        state->answer_ready = true;
        std::printf("  [server] subscriber final answer after gathering (%zu bytes)\n",
                    state->answer_sdp.size());
        state->cv.notify_one();
      }
    }
  });

  try {
    pc->setRemoteDescription(std::string(body));
    std::printf("  [server] subscriber setRemoteDescription OK\n");
  } catch (const std::exception& e) {
    std::printf("  [server] subscriber setRemoteDescription FAILED: %s\n", e.what());
    return "";
  }

  // The auto-negotiation inside setRemoteDescription() already created the
  // answer.  Wait for ICE gathering to deliver the final SDP with candidates.
  std::printf("  [server] subscriber waiting for auto-negotiated answer...\n");

  {
    std::unique_lock<std::mutex> lock(state->mtx);
    if (state->cv.wait_for(lock, std::chrono::seconds(5), [&] { return state->answer_ready; })) {
      std::printf("  [server] subscriber answer ready (%zu bytes)\n", state->answer_sdp.size());
    } else {
      std::printf("  [server] subscriber timeout\n");
    }
  }

  return state->answer_sdp;
}

void WhipTestServer::ForwardToSubscribers(rtc::message_variant data) {
  rtc::binary* binary_data = std::get_if<rtc::binary>(&data);
  if (!binary_data) return;

  std::printf("  [server] ForwardToSubscribers: %zu bytes to %zu subscribers\n",
              binary_data->size(), subscribers_.size());

  // Rewrite the RTP SSRC to match the subscriber's SDP (42) so the browser
  // accepts the packets as coming from the expected stream.
  if (binary_data->size() >= 12) {
    auto* rtp = reinterpret_cast<rtc::RtpHeader*>(binary_data->data());
    rtp->setSsrc(42);
  }

  std::lock_guard<std::mutex> lock(subscribers_mtx_);
  // Forward to all subscribers
  for (auto it = subscribers_.begin(); it != subscribers_.end(); ) {
    auto& sub = *it;
    if (sub->pc && sub->pc->state() == rtc::PeerConnection::State::Connected) {
      if (sub->video_track) {
        try {
          sub->video_track->send(*binary_data);
        } catch (const std::exception& e) {
          std::printf("  [server] forward to %s failed: %s\n", sub->id.c_str(), e.what());
          ++it;
        }
        ++it;
      } else {
        ++it;
      }
    } else if (sub->pc && (sub->pc->state() == rtc::PeerConnection::State::New ||
                           sub->pc->state() == rtc::PeerConnection::State::Connecting)) {
      // ICE/DTLS still coming up — keep the subscriber so the browser doesn't
      // miss the stream just because the pusher started first.
      ++it;
    } else {
      // Remove disconnected subscribers
      std::printf("  [server] removing disconnected subscriber %s\n", sub->id.c_str());
      if (sub->pc) sub->pc->close();
      it = subscribers_.erase(it);
    }
  }
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
    #stats { font-family: monospace; font-size: 12px; text-align: left; margin: 20px auto; max-width: 600px; color: #aaa; }
  </style>
</head>
<body>
  <h1>WHIP Stream Player</h1>
  <video id="player" autoplay muted playsinline></video>
  <div id="status" class="status disconnected">Initializing...</div>
  <pre id="stats"></pre>
  <script>
    const player = document.getElementById('player');
    const statusDiv = document.getElementById('status');
    const statsDiv = document.getElementById('stats');

    function setStatus(text, cls) {
      statusDiv.textContent = text;
      statusDiv.className = 'status ' + cls;
    }

    async function subscribe() {
      const pc = new RTCPeerConnection({
        iceServers: [{ urls: 'stun:stun.l.google.com:19302' }]
      });

      pc.ontrack = (event) => {
        player.srcObject = event.streams[0];
        setStatus('Streaming', 'connected');
      };

      pc.oniceconnectionstatechange = () => {
        const s = pc.iceConnectionState;
        if (s === 'disconnected' || s === 'failed') {
          setStatus('Disconnected', 'disconnected');
        } else if (s === 'connected') {
          setStatus('Connected', 'connected');
        } else {
          setStatus(s, 'connecting');
        }
      };

      pc.addTransceiver('video', { direction: 'recvonly' });

      const offer = await pc.createOffer();
      await pc.setLocalDescription(offer);

      setStatus('Gathering ICE candidates...', 'connecting');
      if (pc.iceGatheringState !== 'complete') {
        await new Promise((resolve) => {
          pc.onicegatheringstatechange = () => {
            if (pc.iceGatheringState === 'complete') resolve();
          };
        });
      }

      setStatus('Subscribing...', 'connecting');
      const resp = await fetch('/whep', {
        method: 'POST',
        headers: { 'Content-Type': 'application/sdp' },
        body: pc.localDescription.sdp
      });
      if (!resp.ok) {
        throw new Error('Subscribe failed: ' + resp.status);
      }
      const answerSdp = await resp.text();
      await pc.setRemoteDescription({ type: 'answer', sdp: answerSdp });

      setInterval(async () => {
        try {
          const reports = await pc.getStats();
          let totalPkts = 0, totalBytes = 0, framesDecoded = 0, framesReceived = 0;
          for (const r of reports) {
            if (r.type === 'inbound-rtp' && r.kind === 'video') {
              totalPkts = r.packetsReceived || 0;
              totalBytes = r.bytesReceived || 0;
              framesDecoded = r.framesDecoded || 0;
              framesReceived = r.framesReceived || 0;
            }
          }
          const state = [
            'ontrack: ' + (player.srcObject ? 'FIRED' : 'N/A'),
            'video.error=' + (player.error ? player.error.message : 'none'),
            'readyState=' + player.readyState,
            'inbound: ' + totalPkts + ' pkts / ' + (totalBytes / 1024).toFixed(1) + ' KB',
            'framesReceived=' + framesReceived,
            'framesDecoded=' + framesDecoded
          ];
          if (framesDecoded > 0 && framesReceived > 0) {
            state.push('>>> DECODING OK (' + framesDecoded + '/' + framesReceived + ')');
          } else if (framesReceived === 0 && totalPkts > 0) {
            state.push('>>> RTP arrives but NO complete frames assembled');
          }
          statsDiv.textContent = state.join('\n');
        } catch (e) {}
      }, 1000);
    }

    setStatus('Subscribing...', 'connecting');
    subscribe().catch(err => {
      console.error(err);
      setStatus('Error: ' + err.message, 'disconnected');
    });
  </script>
</body>
</html>)raw";
}

// Split an Annex-B H.264 file into access units (frames). Each returned frame
// keeps its start codes. Boundary rule: a VCL NAL (slice, type 1-5) following
// another VCL NAL starts a new access unit. This is correct for the typical
// single-slice-per-frame streams this test server feeds (keyframes carry
// SPS/PPS inline, so those non-VCL NALs stay attached to their IDR).
std::vector<std::vector<std::byte>> SplitAnnexBFrames(const std::vector<uint8_t>& data) {
  struct Nal {
    size_t begin;
    size_t end;  // [begin, end)
    uint8_t type;
  };
  std::vector<Nal> nals;
  const size_t n = data.size();
  size_t i = 0;
  auto is_start_code = [&](size_t j) {
    if (j + 3 > n) return 0;  // not a full pattern
    if (data[j] == 0 && data[j + 1] == 0 && data[j + 2] == 1) return 3;
    if (j + 4 <= n && data[j] == 0 && data[j + 1] == 0 && data[j + 2] == 0 && data[j + 3] == 1)
      return 4;
    return 0;
  };
  while (i + 3 <= n) {
    int sc = is_start_code(i);
    if (!sc) {
      ++i;
      continue;
    }
    size_t begin = i;
    size_t body = i + sc;
    if (body >= n) break;
    uint8_t type = data[body] & 0x1f;
    size_t j = body + 1;
    while (j + 3 <= n && !is_start_code(j)) ++j;
    nals.push_back({begin, j, type});
    i = j;
  }

  std::vector<std::vector<std::byte>> frames;
  std::vector<std::byte> current;
  bool prev_was_vcl = false;
  for (const auto& nal : nals) {
    bool is_vcl = nal.type >= 1 && nal.type <= 5;
    if (is_vcl && prev_was_vcl) {
      frames.push_back(std::move(current));
      current.clear();
    }
    current.reserve(current.size() + (nal.end - nal.begin));
    for (size_t k = nal.begin; k < nal.end; ++k)
      current.emplace_back(static_cast<std::byte>(data[k]));
    prev_was_vcl = is_vcl;
  }
  if (!current.empty()) frames.push_back(std::move(current));
  return frames;
}

bool WhipTestServer::LoadMedia() {
  std::vector<std::string> files;

  struct stat st;
  if (stat(media_path_.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
    DIR* dir = opendir(media_path_.c_str());
    if (!dir) return false;
    while (struct dirent* ent = readdir(dir)) {
      std::string name = ent->d_name;
      if (name.size() > 5 && name.substr(name.size() - 5) == ".h264") {
        files.push_back(media_path_ + "/" + name);
      }
    }
    closedir(dir);
    std::sort(files.begin(), files.end());
  } else {
    files.push_back(media_path_);
  }

  if (files.empty()) return false;

  for (const auto& path : files) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      std::printf("  [server] media: cannot open %s\n", path.c_str());
      continue;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    if (bytes.size() < 4) continue;
    auto frames = SplitAnnexBFrames(bytes);
    std::printf("  [server] media: %s -> %zu frames\n", path.c_str(), frames.size());
    media_frames_.insert(media_frames_.end(),
                         std::make_move_iterator(frames.begin()),
                         std::make_move_iterator(frames.end()));
  }
  std::printf("  [server] media: total %zu frames loaded\n", media_frames_.size());
  return !media_frames_.empty();
}

void WhipTestServer::StartFilePlayer() {
  // Forwarding thread: pops RTP packets from the queue and sends them to all
  // subscribers. Decouples the 30 fps pacing from the (potentially slow) SRTP
  // encryption + ICE send, so the file player never stalls.
  forward_thread_ = std::thread([this]() {
    while (running_ || !forward_done_) {
      rtc::binary pkt;
      {
        std::unique_lock<std::mutex> lock(forward_mtx_);
        forward_cv_.wait_for(lock, std::chrono::milliseconds(100),
                             [this] { return !forward_queue_.empty() || forward_done_; });
        if (forward_queue_.empty()) continue;
        pkt = std::move(forward_queue_.front());
        forward_queue_.erase(forward_queue_.begin());
      }
      ForwardToSubscribers(rtc::message_variant(std::move(pkt)));
    }
  });
  forward_thread_.detach();

  // File player thread: reads H.264 frames, packetizes into RTP, and pushes
  // RTP packets into the forwarding queue. Pacing is a simple sleep_until loop
  // that is never blocked by subscriber I/O.
  file_thread_ = std::thread([this]() {
    constexpr uint32_t kClockRate = 90000;
    constexpr uint32_t kFps = 30;
    constexpr uint32_t kTsStep = kClockRate / kFps;
    constexpr auto kFrameDuration = std::chrono::microseconds(1'000'000 / kFps);
    auto rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(42, "video-send", 96, kClockRate);
    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
        rtc::NalUnit::Separator::StartSequence, rtp_config);

    uint32_t frame_index = 0;
    auto next_frame = std::chrono::steady_clock::now();
    uint32_t play_count = 0;
    while (running_ && (no_loop_ ? play_count < 1 : true)) {
      for (const auto& frame : media_frames_) {
        if (!running_) break;
        rtp_config->timestamp = rtp_config->startTimestamp + frame_index * kTsStep;
        ++frame_index;

        rtc::message_vector msgs{rtc::make_message(rtc::binary(frame.begin(), frame.end()))};
        packetizer->outgoingChain(msgs, [](rtc::message_ptr) {});
        for (auto& m : msgs) {
          rtc::binary pkt(m->begin(), m->end());
          {
            std::lock_guard<std::mutex> lock(forward_mtx_);
            forward_queue_.push_back(std::move(pkt));
          }
          forward_cv_.notify_one();
        }

        // If we're more than 2 frames behind, reset the pacing to avoid a
        // long burst when a subscriber connects mid-stream or at loop boundary.
        auto now = std::chrono::steady_clock::now();
        if (next_frame + kFrameDuration * 2 < now) {
          next_frame = now + kFrameDuration;
        } else {
          next_frame += kFrameDuration;
        }
        std::this_thread::sleep_until(next_frame);
      }
      ++play_count;
    }
    // Signal the forward thread to drain remaining packets then exit.
    {
      std::lock_guard<std::mutex> lock(forward_mtx_);
      forward_done_ = true;
    }
    forward_cv_.notify_one();
  });
  file_thread_.detach();
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