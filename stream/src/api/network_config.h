#pragma once

#include <cstdint>
#include <string>

namespace video {
namespace stream {

// Transport/security options for network signaling (WHIP/HTTP), mapped onto the
// cpp_network http client. Timeout/latency fields are milliseconds; a value of
// 0 means "use the library default" for read/write/keep-alive.
struct NetworkConfig {
  // Timeouts (ms). 0 = library default.
  uint32_t connect_timeout_ms = 5000;
  uint32_t read_timeout_ms = 0;
  uint32_t write_timeout_ms = 0;
  uint32_t total_timeout_ms = 10000;

  // HTTP redirect handling.
  bool follow_redirects = false;
  int max_redirects = 20;

  // Optional local interface binding and HTTP(S) proxy.
  std::string local_address;
  uint16_t local_port = 0;
  std::string proxy_host;
  uint16_t proxy_port = 8080;

  // TCP keep-alive idle time (ms); 0 = library default.
  uint32_t keep_alive_ms = 0;

  // TLS: peer verification on by default. The CA can be supplied as a file
  // path (tls_ca_file) or inline PEM text (tls_ca_pem); tls_ca_file takes
  // precedence when both are set. mTLS uses tls_client_cert + tls_client_key
  // together, each either a path or inline PEM.
  bool tls_verify = true;
  std::string tls_ca_file;
  std::string tls_ca_pem;
  std::string tls_client_cert;
  std::string tls_client_key;
  std::string tls_sni;
};

}  // namespace stream
}  // namespace video