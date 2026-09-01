#include "stream/src/core/stream_impl.h"
#include "stream/src/core/backend_registry.h"

#include "codec/src/framework/core/status.h"

namespace video {
namespace stream {

std::unique_ptr<Stream> Stream::Create(const StreamConfig& config) {
  return std::make_unique<StreamImpl>(config);
}

}  // namespace stream
}  // namespace video