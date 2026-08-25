#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "src/test_server/whip_test_server.h"

namespace {

video::stream::WhipTestServer* g_server = nullptr;

void SignalHandler(int) {
  if (g_server) g_server->Stop();
}

}  // namespace

int main(int argc, char* argv[]) {
  uint16_t port = 8080;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      port = static_cast<uint16_t>(std::atoi(argv[++i]));
    }
  }

  signal(SIGINT, SignalHandler);
  signal(SIGTERM, SignalHandler);

  video::stream::WhipTestServer server(port);
  g_server = &server;

  if (!server.Start()) {
    std::fprintf(stderr, "Failed to start server on port %u\n", port);
    return 1;
  }

  std::printf("WHIP test server listening on http://localhost:%u\n", port);
  std::printf("Push endpoint: http://localhost:%u/whip\n", port);
  std::printf("Player page: http://localhost:%u/\n", port);

  server.Run();

  std::printf("Server stopped.\n");
  return 0;
}