// stream_config.cc
//
// Unified JSON configuration for StreamConfig: the schema (field-key table)
// and every default live in this module / the struct initializers. Callers
// only hand over the JSON content (string or file) — never re-specify the
// schema. A missing key always falls back to the module default (the struct
// initializers in stream_config.h / network_config.h).

#include "stream/src/api/stream_config.h"

#include <fstream>
#include <sstream>
#include <string>

#include "nlohmann/json.hpp"

#include "codec/src/framework/core/log_slot.h"
#include "codec/src/framework/core/result.h"
#include "codec/src/framework/core/status.h"

namespace video {
namespace stream {
namespace {

// ---- JSON field-key constants (the schema, owned by this module) ------------

// signal / remote URL
constexpr char kKeyUrl[]     = "url";
constexpr char kKeyHost[]    = "host";
constexpr char kKeyPath[]    = "path";
constexpr char kWhipSuffix[] = "whip";

// top-level
constexpr char kKeyBackend[]    = "backend";
constexpr char kKeyVideoCodec[] = "video_codec";
constexpr char kKeyAudioCodec[] = "audio_codec";

// bitrate / resolution / frame rate
constexpr char kKeyInitialBitrateKbps[] = "initial_bitrate_kbps";
constexpr char kKeyMaxBitrateKbps[]     = "max_bitrate_kbps";
constexpr char kKeyMinBitrateKbps[]     = "min_bitrate_kbps";
constexpr char kKeyWidth[]              = "width";
constexpr char kKeyHeight[]             = "height";
constexpr char kKeyFps[]                = "fps";

// reconnect / buffering
constexpr char kKeyBufferDurationS[]       = "buffer_duration_s";
constexpr char kKeyReconnectMaxIntervalS[] = "reconnect_max_interval_s";

// ICE (STUN/TURN)
constexpr char kKeyStunServer[] = "stun_server";
constexpr char kKeyTurnServer[] = "turn_server";

// network (nested "network" object) -> NetworkConfig
constexpr char kKeyNetwork[] = "network";
constexpr char kKeyConnectTimeoutMs[] = "connect_timeout_ms";
constexpr char kKeyReadTimeoutMs[]    = "read_timeout_ms";
constexpr char kKeyWriteTimeoutMs[]   = "write_timeout_ms";
constexpr char kKeyTotalTimeoutMs[]   = "total_timeout_ms";
constexpr char kKeyFollowRedirects[]  = "follow_redirects";
constexpr char kKeyMaxRedirects[]     = "max_redirects";
constexpr char kKeyLocalAddress[]     = "local_address";
constexpr char kKeyLocalPort[]        = "local_port";
constexpr char kKeyProxyHost[]        = "proxy_host";
constexpr char kKeyProxyPort[]        = "proxy_port";
constexpr char kKeyKeepAliveMs[]      = "keep_alive_ms";
constexpr char kKeyTlsVerify[]        = "tls_verify";
constexpr char kKeyTlsCaFile[]        = "tls_ca_file";
constexpr char kKeyTlsCaPem[]         = "tls_ca_pem";
constexpr char kKeyTlsClientCert[]    = "tls_client_cert";
constexpr char kKeyTlsClientKey[]     = "tls_client_key";
constexpr char kKeyTlsSni[]           = "tls_sni";

// Defaults for signal.host / signal.path live here (used only when the JSON
// omits them; the struct-initializer defaults above cover everything else).
constexpr const char* kDefaultHost = "http://localhost:8889";
constexpr const char* kDefaultPath = "test";

// Normalize away leading/trailing '/' (e.g. trailing host '/', leading path '/').
void TrimSlash(std::string& s) {
  size_t first = 0;
  size_t last = s.size();
  while (first < last && s[first] == '/') first++;
  while (last > first && s[last - 1] == '/') last--;
  s = s.substr(first, last - first);
}

// WHIP URL = host + "/" + path + "/whip".
std::string BuildWhipUrl(const std::string& host, const std::string& path) {
  std::string h = host.empty() ? kDefaultHost : host;
  std::string p = path.empty() ? kDefaultPath : path;
  TrimSlash(h);
  TrimSlash(p);
  if (p.find("/whip") == std::string::npos) {
    p += "/" + std::string(kWhipSuffix);
  }
  return h + "/" + p;
}

// Reads a nested "network" object (if present) into NetworkConfig. Falls back
// to the struct defaults for whatever is absent.
void ParseNetwork(const nlohmann::json& root, NetworkConfig* network) {
  if (!root.contains(kKeyNetwork) || !root[kKeyNetwork].is_object()) return;
  const auto& n = root[kKeyNetwork];

  network->connect_timeout_ms = n.value(kKeyConnectTimeoutMs, network->connect_timeout_ms);
  network->read_timeout_ms = n.value(kKeyReadTimeoutMs, network->read_timeout_ms);
  network->write_timeout_ms = n.value(kKeyWriteTimeoutMs, network->write_timeout_ms);
  network->total_timeout_ms = n.value(kKeyTotalTimeoutMs, network->total_timeout_ms);
  network->follow_redirects = n.value(kKeyFollowRedirects, network->follow_redirects);
  network->max_redirects = n.value(kKeyMaxRedirects, network->max_redirects);
  network->local_address = n.value(kKeyLocalAddress, network->local_address);
  network->local_port = n.value(kKeyLocalPort, network->local_port);
  network->proxy_host = n.value(kKeyProxyHost, network->proxy_host);
  network->proxy_port = n.value(kKeyProxyPort, network->proxy_port);
  network->keep_alive_ms = n.value(kKeyKeepAliveMs, network->keep_alive_ms);
  network->tls_verify = n.value(kKeyTlsVerify, network->tls_verify);
  network->tls_ca_file = n.value(kKeyTlsCaFile, network->tls_ca_file);
  network->tls_ca_pem = n.value(kKeyTlsCaPem, network->tls_ca_pem);
  network->tls_client_cert = n.value(kKeyTlsClientCert, network->tls_client_cert);
  network->tls_client_key = n.value(kKeyTlsClientKey, network->tls_client_key);
  network->tls_sni = n.value(kKeyTlsSni, network->tls_sni);
}

video::codec::Result<StreamConfig> ParseJsonText(const std::string& json_text,
                                                 const std::string& source_name) {
  nlohmann::json root;
  try {
    // Ignore comments so sample configs in examples/ can be self-documenting.
    root = nlohmann::json::parse(json_text, nullptr, /*allow_exceptions=*/true,
                                 /*ignore_comments=*/true);
  } catch (const nlohmann::json::parse_error& e) {
    VC_LOG(video::codec::LogLevel::kError,
           source_name + ": JSON parse error: " + e.what());
    return video::codec::Err<StreamConfig>(video::codec::Status::kInvalidArgument);
  }
  if (!root.is_object()) {
    VC_LOG(video::codec::LogLevel::kError,
           source_name + ": config root must be a JSON object");
    return video::codec::Err<StreamConfig>(video::codec::Status::kInvalidArgument);
  }

  StreamConfig cfg;

  // signal: explicit "url" wins; otherwise host + path + /whip.
  std::string remote_url = root.value(kKeyUrl, std::string());
  if (remote_url.empty()) {
    const std::string host = root.value(kKeyHost, std::string());
    const std::string path = root.value(kKeyPath, std::string());
    remote_url = BuildWhipUrl(host, path);
  }
  cfg.remote_url = remote_url;

  cfg.backend_type = root.value(kKeyBackend, cfg.backend_type);
  cfg.video_codec = root.value(kKeyVideoCodec, cfg.video_codec);
  cfg.audio_codec = root.value(kKeyAudioCodec, cfg.audio_codec);

  cfg.initial_bitrate_kbps = root.value(kKeyInitialBitrateKbps, cfg.initial_bitrate_kbps);
  cfg.max_bitrate_kbps = root.value(kKeyMaxBitrateKbps, cfg.max_bitrate_kbps);
  cfg.min_bitrate_kbps = root.value(kKeyMinBitrateKbps, cfg.min_bitrate_kbps);

  cfg.resolution_width = root.value(kKeyWidth, cfg.resolution_width);
  cfg.resolution_height = root.value(kKeyHeight, cfg.resolution_height);
  cfg.framerate = root.value(kKeyFps, cfg.framerate);

  cfg.buffer_duration_s = root.value(kKeyBufferDurationS, cfg.buffer_duration_s);
  cfg.reconnect_max_interval_s = root.value(kKeyReconnectMaxIntervalS, cfg.reconnect_max_interval_s);

  cfg.stun_server = root.value(kKeyStunServer, cfg.stun_server);
  cfg.turn_server = root.value(kKeyTurnServer, cfg.turn_server);

  ParseNetwork(root, &cfg.network);

  return video::codec::Result<StreamConfig>::Ok(std::move(cfg));
}

}  // namespace

video::codec::Result<StreamConfig> StreamConfig::ParseFromJson(const std::string& json_text) {
  return ParseJsonText(json_text, "<json_string>");
}

video::codec::Result<StreamConfig> StreamConfig::LoadFromFile(const std::string& file_path) {
  std::ifstream file(file_path);
  if (!file.is_open()) {
    VC_LOG(video::codec::LogLevel::kError,
           "config file not found: " + file_path);
    return video::codec::Err<StreamConfig>(video::codec::Status::kInvalidArgument);
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return ParseJsonText(buffer.str(), file_path);
}

}  // namespace stream
}  // namespace video