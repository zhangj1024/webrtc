#include "win_video_capture.h"

#include "api/video/i420_buffer.h"
#include "libyuv/convert.h"
#include "libyuv/video_common.h"
#include "media/base/videocapturer.h"
#include "media/base/videocommon.h"
#include "rtc_base/logging.h"

#include "modules/desktop_capture/win/screen_capture_utils.h"
#include "modules/desktop_capture/win/window_capture_utils.h"

namespace webrtc {

class WinVideoCaptureThread : public rtc::AutoThread,
                              public rtc::MessageHandler {
 private:
  DesktopCapturer* capturer_;
  volatile bool finished_;
  volatile bool running_;

 public:
  WinVideoCaptureThread(DesktopCapturer* capturer)
      : capturer_(capturer), finished_(false), running_(false){};

  ~WinVideoCaptureThread() override {};

 protected:
  void Run() override {
    // fps=15
    static const int CaptureInterval = 1000 / 15;
    running_ = true;
    while (!IsQuitting()) {
      capturer_->CaptureFrame();
      SleepMs(CaptureInterval);
    }
    running_ = false;
    finished_ = true;
  };

 public:
  void OnMessage(rtc::Message* msg) override {};
  inline bool Finished() const { return finished_; };
  inline bool Running() const { return running_; };

 private:
  void operator=(const WinVideoCaptureThread&) = delete;
  WinVideoCaptureThread(const WinVideoCaptureThread&) = delete;
};

WinVideoCapture::WinVideoCapture(DesktopCapturer* capture)
    : capturer_(capture) {
  capturer_->Start(this);
}

WinVideoCapture::~WinVideoCapture() {
  if (capture_thread_) {
    capture_thread_->Stop();
    delete capture_thread_;
  }

  SignalDestroyed(this);
}

void WinVideoCapture::ResetSupportedFormats(
    const std::vector<cricket::VideoFormat>& formats) {
  SetSupportedFormats(formats);
}

bool WinVideoCapture::CaptureFrame() {
  capturer_->CaptureFrame();
  return true;
}

cricket::CaptureState WinVideoCapture::Start(
    const cricket::VideoFormat& format) {
  cricket::VideoFormat supported;
  if (GetBestCaptureFormat(format, &supported)) {
    SetCaptureFormat(&supported);
  }
  if (capture_thread_ == NULL) {
    capture_thread_ = new WinVideoCaptureThread(capturer_.get());
  }
  capture_thread_->Start();
  SetCaptureState(cricket::CS_RUNNING);
  return cricket::CS_RUNNING;
}

void WinVideoCapture::Stop() {
  if (capture_thread_ != NULL) {
    capture_thread_->Stop();
  }
  SetCaptureFormat(NULL);
  SetCaptureState(cricket::CS_STOPPED);
}

bool WinVideoCapture::IsRunning() {
  return capture_thread_ ? capture_thread_->Running() : false;
}

bool WinVideoCapture::IsScreencast() const {
  return true;
}

bool WinVideoCapture::GetPreferredFourccs(std::vector<uint32_t>* fourccs) {
  fourccs->push_back(cricket::FOURCC_I420);
  fourccs->push_back(cricket::FOURCC_MJPG);
  return true;
}

void WinVideoCapture::SetRect(DesktopRect rect) {
  rect_ = rect;
};

void WinVideoCapture::OnCaptureResult(
    DesktopCapturer::Result result,
    std::unique_ptr<DesktopFrame> desktop_frame) {
  if (!capture_thread_->Running()) {
    return;
  }

  if (result != DesktopCapturer::Result::SUCCESS) {
    return;
  }

  if (!desktop_frame || desktop_frame->updated_region().is_empty()) {
    return;
  }

  if (!rect_.is_empty() && !desktop_frame->rect().equals(rect_)) {
    std::unique_ptr<DesktopFrame> rect_desktop_frame(
        new BasicDesktopFrame(DesktopSize(rect_.width(), rect_.height())));
    rect_desktop_frame->CopyPixelsFrom(
        *desktop_frame, rect_.top_left(),
        DesktopRect::MakeWH(rect_.width(), rect_.height()));
    desktop_frame.reset(rect_desktop_frame.release());
  }

  int width = desktop_frame->size().width();
  int height = desktop_frame->size().height();
  uint32_t size = width * height * DesktopFrame::kBytesPerPixel;

  int stride_y = width;
  int stride_uv = (width + 1) / 2;
  int target_width = width;
  int target_height = height;

  rtc::scoped_refptr<I420Buffer> buffer = I420Buffer::Create(
      target_width, target_height, stride_y, stride_uv, stride_uv);

  const int conversionResult = libyuv::ConvertToI420(
      desktop_frame->data(), size, buffer->MutableDataY(), buffer->StrideY(),
      buffer->MutableDataU(), buffer->StrideU(), buffer->MutableDataV(),
      buffer->StrideV(), 0, 0, width, height, buffer->width(), buffer->height(),
      libyuv::kRotate0, libyuv::FOURCC_ARGB);

  if (conversionResult < 0) {
    RTC_LOG(LS_ERROR) << "Failed to convert FOURCC_ARGB to I420 ";
    return;
  }

  VideoFrame frame(buffer, kVideoRotation_0, desktop_frame->capture_time_ms());
  OnFrame(frame, width, height);
}

std::unique_ptr<cricket::VideoCapturer>
WinVideoCapture::CreateWindowVideoCapturer(WindowId windowId) {
  std::unique_ptr<DesktopCapturer> capturer =
      DesktopCapturer::CreateWindowCapturer(
          DesktopCaptureOptions::CreateDefault());

  capturer->SelectSource(windowId);

  MouseCursorMonitor* cursor = MouseCursorMonitor::CreateForWindow(
      DesktopCaptureOptions::CreateDefault(), windowId);

  std::unique_ptr<DesktopAndCursorComposer> composer(
      new DesktopAndCursorComposer(capturer.release(), cursor));

  std::unique_ptr<WinVideoCapture> winVideoCapture(
      new WinVideoCapture(composer.release()));

  return std::move(winVideoCapture);
}

std::unique_ptr<cricket::VideoCapturer>
WinVideoCapture::CreateScreenVideoCapturer(
    DesktopRect rect,
    ScreenId screenId /* = kFullDesktopScreenId*/) {
  DesktopCaptureOptions opts = DesktopCaptureOptions::CreateDefault();
  opts.set_allow_directx_capturer(true);

  std::unique_ptr<DesktopCapturer> capturer =
      DesktopCapturer::CreateScreenCapturer(opts);

  capturer->SelectSource(screenId);

  MouseCursorMonitor* cursor_ = MouseCursorMonitor::CreateForScreen(
      DesktopCaptureOptions::CreateDefault(), screenId);

  std::unique_ptr<DesktopAndCursorComposer> composer(
      new DesktopAndCursorComposer(capturer.release(), cursor_));

  std::unique_ptr<WinVideoCapture> winVideoCapture(
      new WinVideoCapture(composer.release()));

  winVideoCapture->SetRect(rect);

  return std::move(winVideoCapture);
}

std::unique_ptr<cricket::VideoCapturer>
WinVideoCapture::CreateScreenVideoCapturer() {
  return WinVideoCapture::CreateScreenVideoCapturer(GetFullscreenRect());
}
}  // namespace webrtc
