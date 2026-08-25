// whip_subscribe_verify_test.cc
//
// End-to-end verification of the /subscribe relay path without a browser:
//   - a "pusher" PeerConnection POSTs a sendonly H.264 offer to /whip
//   - a "subscriber" PeerConnection POSTs a recvonly offer to /subscribe
//     (exactly what player.html does) and must receive RTP packets that a
//     real WebRTC stack can depacketize into H.264 access units.
//
// The subscriber captures the raw RTP it receives and, at the end, re-feeds
// it to a fresh H264RtpDepacketizer. That mirrors what the browser does with
// its own WebRTC stack: decode the forwarded RTP directly.
//
// This is a manual verification target, not part of the default test suite.
// Run with: bazel run //tests:whip_subscribe_verify_test

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <rtc/rtc.hpp>

#include "src/test_server/whip_test_server.h"

namespace video {
namespace stream {
namespace {

using namespace std::chrono_literals;

std::string HttpPost(const std::string& url, const std::string& body,
                     const std::string& content_type) {
  auto pos = url.find("://");
  std::string rest = pos == std::string::npos ? url : url.substr(pos + 3);
  auto slash = rest.find('/');
  std::string hostport = rest.substr(0, slash);
  std::string path = slash == std::string::npos ? "/" : rest.substr(slash);

  std::string host = hostport;
  int port = 80;
  auto colon = host.find(':');
  if (colon != std::string::npos) {
    host = hostport.substr(0, colon);
    port = std::stoi(hostport.substr(colon + 1));
  }

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return "";

  struct hostent* h = gethostbyname(host.c_str());
  if (!h) {
    close(sock);
    return "";
  }
  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  std::memcpy(&addr.sin_addr.s_addr, h->h_addr, h->h_length);
  addr.sin_port = htons(port);

  if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(sock);
    return "";
  }

  std::ostringstream req;
  req << "POST " << path << " HTTP/1.1\r\n"
      << "Host: " << host << "\r\n"
      << "Content-Type: " << content_type << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n"
      << "\r\n"
      << body;
  std::string request = req.str();
  send(sock, request.c_str(), request.size(), 0);
  shutdown(sock, SHUT_WR);

  std::string response;
  char buf[4096];
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

// Send an offer once ICE gathering completes, return the answer SDP.
std::string SendOffer(const std::shared_ptr<rtc::PeerConnection>& pc,
                      const std::string& url, const std::string& path) {
  std::string answer;
  std::mutex mtx;
  std::condition_variable cv;
  bool gathering_done = false;

  pc->onGatheringStateChange([&](rtc::PeerConnection::GatheringState gstate) {
    if (gstate == rtc::PeerConnection::GatheringState::Complete) {
      std::lock_guard<std::mutex> lock(mtx);
      gathering_done = true;
      cv.notify_one();
    }
  });

  pc->setLocalDescription();
  {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait_for(lock, 5s, [&] { return gathering_done; });
  }
  if (gathering_done) {
    auto local = pc->localDescription();
    if (local) answer = HttpPost(url, std::string(*local), "application/sdp");
  }
  return answer;
}

// Minimal Annex-B H.264 access unit (SPS + PPS + IDR slice). Real enough for
// the relay to carry it and a depacketizer to reassemble it.
std::vector<std::byte> MakeH264Frame() {
  const uint8_t kSps[] = {0x67, 0x42, 0x00, 0x1e, 0xe8, 0x80, 0x50, 0x05, 0xba, 0x01, 0x00, 0x00,
                          0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x32, 0x0f, 0x18, 0x31, 0x96};
  const uint8_t kPps[] = {0x68, 0xce, 0x38, 0x80};
  const uint8_t kIdr[] = {0x65, 0x88, 0x84, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00};
  std::vector<std::byte> out;
  const uint8_t kStart[] = {0x00, 0x00, 0x00, 0x01};
  auto append = [&](const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) out.emplace_back(static_cast<std::byte>(p[i]));
  };
  append(kStart, sizeof(kStart));
  append(kSps, sizeof(kSps));
  append(kStart, sizeof(kStart));
  append(kPps, sizeof(kPps));
  append(kStart, sizeof(kStart));
  append(kIdr, sizeof(kIdr));
  return out;
}

TEST(WhipSubscribeVerify, RelayDeliversDepacketizableRtp) {
  setbuf(stdout, nullptr);

  WhipTestServer server(0);
  ASSERT_TRUE(server.Start());
  std::thread run_thread([&] { server.Run(); });
  run_thread.detach();

  std::string base = "http://127.0.0.1:" + std::to_string(server.Port());

  // ---- Subscriber (mirrors player.html) ----
  // Without a media handler, Track::incoming delivers the raw RTP to onMessage
  // (the same packets a browser would depacketize itself).
  std::atomic<int> rtp_packets{0};
  std::vector<std::vector<std::byte>> captured;
  std::mutex cap_mtx;
  std::atomic<bool> sub_connected{false};

  rtc::Configuration sub_cfg;
  sub_cfg.iceServers.emplace_back("stun:stun.l.google.com:19302");
  auto sub_pc = std::make_shared<rtc::PeerConnection>(sub_cfg);
  sub_pc->onStateChange([&](rtc::PeerConnection::State state) {
    if (state == rtc::PeerConnection::State::Connected) sub_connected = true;
  });

  auto sub_media = rtc::Description::Video("0", rtc::Description::Direction::RecvOnly);
  auto sub_track = sub_pc->addTrack(sub_media);
  sub_track->onMessage(
      [&](rtc::binary msg) {
        rtp_packets++;
        std::lock_guard<std::mutex> lock(cap_mtx);
        if (captured.size() < 500) captured.emplace_back(msg);
      },
      [](std::string) {});

  std::string sub_answer = SendOffer(sub_pc, base + "/subscribe", "/subscribe");
  ASSERT_FALSE(sub_answer.empty()) << "subscribe returned empty answer";
  sub_pc->setRemoteDescription(sub_answer);

  // ---- Pusher (mirrors webrtc_backend.cc) ----
  std::atomic<bool> pub_connected{false};
  rtc::Configuration pub_cfg;
  pub_cfg.iceServers.emplace_back("stun:stun.l.google.com:19302");
  auto pub_pc = std::make_shared<rtc::PeerConnection>(pub_cfg);
  pub_pc->onStateChange([&](rtc::PeerConnection::State state) {
    if (state == rtc::PeerConnection::State::Connected) pub_connected = true;
  });

  auto pub_media = rtc::Description::Video("video", rtc::Description::Direction::SendOnly);
  pub_media.addH264Codec(96);
  pub_media.addSSRC(42, "video-send");
  auto pub_track = pub_pc->addTrack(pub_media);
  auto rtp_cfg = std::make_shared<rtc::RtpPacketizationConfig>(42, "video-send", 96, 90000);
  auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
      rtc::NalUnit::Separator::StartSequence, rtp_cfg);
  pub_track->setMediaHandler(packetizer);

  std::string pub_answer = SendOffer(pub_pc, base + "/whip", "/whip");
  ASSERT_FALSE(pub_answer.empty()) << "whip returned empty answer";
  pub_pc->setRemoteDescription(pub_answer);

  // Wait for both connections to establish.
  auto wait_until = std::chrono::steady_clock::now() + 10s;
  while ((!sub_connected || !pub_connected) && std::chrono::steady_clock::now() < wait_until)
    std::this_thread::sleep_for(20ms);
  ASSERT_TRUE(pub_connected) << "pusher never connected";
  ASSERT_TRUE(sub_connected) << "subscriber never connected";

  // Push frames with advancing RTP timestamps (the exact bug fixed in
  // WebrtcBackend::SendVideo — a constant timestamp makes the receiver treat
  // the whole stream as one access unit and never emit a frame).
  auto frame = MakeH264Frame();
  for (int i = 0; i < 30; ++i) {
    rtp_cfg->timestamp = rtp_cfg->startTimestamp + static_cast<uint32_t>(i * 3000);
    pub_track->send(frame);
    std::this_thread::sleep_for(5ms);
  }

  wait_until = std::chrono::steady_clock::now() + 5s;
  while (rtp_packets == 0 && std::chrono::steady_clock::now() < wait_until)
    std::this_thread::sleep_for(20ms);
  ASSERT_GT(rtp_packets.load(), 0) << "subscriber received no RTP";

  // The subscriber saw the relayed RTP. Verify it is decodable: re-feed the
  // captured packets to a fresh H.264 depacketizer, exactly like the browser
  // depacketizes the stream it receives.
  std::vector<rtc::message_ptr> rtp_msgs;
  {
    std::lock_guard<std::mutex> lock(cap_mtx);
    for (auto& bytes : captured) rtp_msgs.push_back(rtc::make_message(std::move(bytes)));
  }

  auto fresh = std::make_shared<rtc::H264RtpDepacketizer>(rtc::NalUnit::Separator::StartSequence);
  int access_units = 0;
  for (auto& m : rtp_msgs) {
    rtc::message_vector in{std::move(m)};
    fresh->incomingChain(in, [](rtc::message_ptr) {});
    access_units += static_cast<int>(in.size());
  }

  std::printf("[verify] captured_rtp=%zu access_units=%d\n", rtp_msgs.size(), access_units);
  EXPECT_GT(access_units, 0) << "relayed RTP is not depacketizable H.264";

  server.Stop();
}

}  // namespace
}  // namespace stream
}  // namespace video
