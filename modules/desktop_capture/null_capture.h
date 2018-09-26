#ifndef MODULES_DESKTOP_CAPTURE_NULL_CAPTURE_H_
#define MODULES_DESKTOP_CAPTURE_NULL_CAPTURE_H_

#include <iostream>
#include <memory>
#include <vector>

#include "media/base/videocapturer.h"

namespace webrtc {
class NullCaputre : public cricket::VideoCapturer {
 public:
  NullCaputre();
  ~NullCaputre() override;

  static std::unique_ptr<cricket::VideoCapturer> CreateNullCapturer();

  cricket::CaptureState Start(const cricket::VideoFormat& format) override;
  void Stop() override;
  bool IsRunning() override;
  bool IsScreencast() const override;
  bool GetPreferredFourccs(std::vector<uint32_t>* fourccs) override;
};
}  // namespace webrtc

#endif  // MODULES_DESKTOP_CAPTURE_NULL_CAPTURE_H_
