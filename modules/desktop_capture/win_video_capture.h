#ifndef MODULES_DESKTOP_CAPTURE_WIN_VIDEO_CAPTURE_H_
#define MODULES_DESKTOP_CAPTURE_WIN_VIDEO_CAPTURE_H_

#include <iostream>
#include <memory>
#include <vector>

#include "media/base/videocapturer.h"
#include "modules/desktop_capture/desktop_and_cursor_composer.h"
#include "modules/desktop_capture/desktop_capture_options.h"
#include "modules/desktop_capture/desktop_capturer.h"
#include "modules/desktop_capture/mouse_cursor_monitor.h"
#include "rtc_base/thread.h"
#include "rtc_base/timeutils.h"

namespace webrtc {
class WinVideoCaptureThread;

// Fake video capturer that allows the test to manually pump in frames.
class WinVideoCapture : public cricket::VideoCapturer,
                        public DesktopCapturer::Callback {
 public:
  sigslot::signal1<WinVideoCapture*> SignalDestroyed;

  WinVideoCapture(DesktopCapturer* capture);
  ~WinVideoCapture() override;

  static std::unique_ptr<cricket::VideoCapturer> CreateWindowVideoCapturer(
      WindowId windowId);

  static std::unique_ptr<cricket::VideoCapturer> CreateScreenVideoCapturer(
      DesktopRect rect,
      ScreenId screenId = kFullDesktopScreenId);

  static std::unique_ptr<cricket::VideoCapturer> CreateScreenVideoCapturer();

  void ResetSupportedFormats(const std::vector<cricket::VideoFormat>& formats);
  bool CaptureFrame();
  cricket::CaptureState Start(const cricket::VideoFormat& format) override;
  void Stop() override;
  bool IsRunning() override;
  bool IsScreencast() const override;
  bool GetPreferredFourccs(std::vector<uint32_t>* fourccs) override;
  void OnCaptureResult(
      DesktopCapturer::Result result,
      std::unique_ptr<DesktopFrame> desktop_frame) override;

  void SetRect(DesktopRect rect);

 private:
  std::unique_ptr<DesktopCapturer> capturer_;
  WinVideoCaptureThread* capture_thread_ = NULL;
  DesktopRect rect_;
};
}  // namespace webrtc

#endif  // MODULES_DESKTOP_CAPTURE_WIN_VIDEO_CAPTURE_H_
