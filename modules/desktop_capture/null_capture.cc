#include "null_capture.h"

namespace webrtc {
std::unique_ptr<cricket::VideoCapturer> NullCaputre::CreateNullCapturer() {
  return std::move(std::unique_ptr<cricket::VideoCapturer>(new NullCaputre()));
}
}  // namespace webrtc
