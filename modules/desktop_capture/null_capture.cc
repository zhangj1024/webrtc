#include "null_capture.h"

namespace webrtc {

std::unique_ptr<cricket::VideoCapturer> NullCaputre::CreateNullCapturer() {
  return std::unique_ptr<cricket::VideoCapturer>(new NullCaputre());
}

NullCaputre::NullCaputre() {}
NullCaputre::~NullCaputre() {}

cricket::CaptureState NullCaputre::Start(const cricket::VideoFormat& format) {
  return cricket::CS_RUNNING;
}

void NullCaputre::Stop() {}

bool NullCaputre::IsRunning() {
  return false;
}

bool NullCaputre::IsScreencast() const {
  return false;
}

bool NullCaputre::GetPreferredFourccs(std::vector<uint32_t>* fourccs) {
  return false;
}

}  // namespace webrtc
