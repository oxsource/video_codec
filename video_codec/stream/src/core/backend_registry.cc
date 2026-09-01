#include "stream/src/core/backend_registry.h"

#include <unordered_map>

namespace video {
namespace stream {

namespace {

std::unordered_map<std::string, BackendFactory>& GetRegistry() {
  static std::unordered_map<std::string, BackendFactory> registry;
  return registry;
}

}  // namespace

void RegisterBackend(const std::string& name, BackendFactory factory) {
  GetRegistry()[name] = factory;
}

std::unique_ptr<StreamBackend> CreateBackend(
    const std::string& name, const StreamConfig& config) {
  auto& registry = GetRegistry();
  auto it = registry.find(name);
  if (it == registry.end()) return nullptr;
  return it->second(config);
}

}  // namespace stream
}  // namespace video