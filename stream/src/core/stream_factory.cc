#include "src/core/stream_impl.h"
#include "src/core/backend_registry.h"

#include "src/framework/core/status.h"

namespace video {
namespace stream {

std::unique_ptr<Stream> Stream::Create(const StreamConfig& config) {
  return std::make_unique<StreamImpl>(config);
}

}  // namespace stream
}  // namespace video