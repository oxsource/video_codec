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
  std::string media_path;
  bool no_loop = false;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      port = static_cast<uint16_t>(std::atoi(argv[++i]));
    } else if (strcmp(argv[i], "--media") == 0 && i + 1 < argc) {
      media_path = argv[++i];
    } else if (strcmp(argv[i], "--no-loop") == 0) {
      no_loop = true;
    }
  }

  signal(SIGINT, SignalHandler);
  signal(SIGTERM, SignalHandler);

  video::stream::WhipTestServer server(port, media_path, no_loop);
  g_server = &server;

  if (!server.Start()) {
    std::fprintf(stderr, "Failed to start server on port %u\n", port);
    return 1;
  }

  std::printf("WHIP test server listening on http://localhost:%u\n", port);
  std::printf("Push endpoint: http://localhost:%u/whip\n", port);
  std::printf("Player page: http://localhost:%u/\n", port);
  if (!media_path.empty()) {
    std::printf("Media mode: %s %s\n", media_path.c_str(),
                no_loop ? "play once" : "looping (no-loop to disable)");
  }

  server.Run();

  std::printf("Server stopped.\n");
  return 0;
}