#pragma once

#include <memory>

#include <rtc/rtc.hpp>

namespace video {
namespace stream {
namespace webrtc {

using PeerConnectionPtr = std::shared_ptr<rtc::PeerConnection>;

}  // namespace webrtc
}  // namespace stream
}  // namespace video