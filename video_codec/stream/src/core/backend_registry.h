#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "stream/src/api/stream_backend.h"
#include "stream/src/api/stream_config.h"

namespace video {
namespace stream {

void RegisterBackend(const std::string& name, BackendFactory factory);

std::unique_ptr<StreamBackend> CreateBackend(
    const std::string& name, const StreamConfig& config);

}  // namespace stream
}  // namespace video