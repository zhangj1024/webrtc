#include "webrtcrecord.h"

/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "media/engine/webrtcinternalfileaudiosource.h"
#include "modules/audio_mixer/audio_mixer_impl.h"
#include "rtc_base/system/file_wrapper.h"
#include "system_wrappers/include/sleep.h"

const int kRecordingFixedSampleRate = 48000;
const size_t kRecordingNumChannels = 1;

const size_t kRecordingFramesIn10MS =
    static_cast<size_t>(kRecordingFixedSampleRate / 100);
const size_t kRecordingBufferSizeIn10Ms =
    kRecordingFramesIn10MS * kRecordingNumChannels;

namespace webrtc {

class AudioSkin : public AudioSinkInterface {
 public:
  AudioSkin(){};
  ~AudioSkin() override { Reset(); };

  // AudioSinkInterface imp
  void OnData(const Data& audio) override {
    AudioFrame* audio_frame = new AudioFrame();
    audio_frame->UpdateFrame(audio.timestamp, audio.data,
                             audio.samples_per_channel, audio.sample_rate,
                             AudioFrame::kUndefined, AudioFrame::kVadUnknown,
                             audio.channels);

    rtc::CritScope lock(&_critSect);
    audio_frame_list_.push_back(audio_frame);
  }

  void Reset() {
    while (audio_frame_list_.size() > 0) {
      AudioFrame* audio_frame = audio_frame_list_.front();
      audio_frame_list_.pop_front();
      delete audio_frame;
    }
  }

  std::unique_ptr<AudioFrame> GetFrame() {
    rtc::CritScope lock(&_critSect);
    if (audio_frame_list_.size() == 0) {
      return NULL;
    }

    AudioFrame* audio_frame = audio_frame_list_.front();
    audio_frame_list_.pop_front();
    return std::unique_ptr<AudioFrame>(audio_frame);
  }

  int framesize() {
    rtc::CritScope lock(&_critSect);
    return audio_frame_list_.size();
  };

 private:
  rtc::CriticalSection _critSect;
  std::list<AudioFrame*> audio_frame_list_;
};

WebRtcAudioMixForRecord::WebRtcAudioMixForRecord(
    const rtc::scoped_refptr<webrtc::AudioState>& audio_state,
    webrtc::RtcEventLog* event_log)
    : audio_state_(audio_state),
      worker_thread_(rtc::Thread::Current()),
      playsource_(new InternalFileAudioSource()),
      recordsource_(new InternalFileAudioSource()),
      skin_player_(new AudioSkin()),
      skin_record_(new AudioSkin()) {
  audio_mixer = webrtc::AudioMixerImpl::Create();

  if (!audio_mixer->AddSource(playsource_) ||
      !audio_mixer->AddSource(recordsource_)) {
    RTC_DLOG(LS_ERROR) << "Failed to add source to audio_mixer.";
  }

  playsource_->SetSampleRate(kRecordingFixedSampleRate);
  recordsource_->SetSampleRate(kRecordingFixedSampleRate);

  _hShutdownMixEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
}

WebRtcAudioMixForRecord::~WebRtcAudioMixForRecord() {
  Stop();
  delete playsource_;
  delete recordsource_;
}

void WebRtcAudioMixForRecord::Start() {
  if (_hMixThread != NULL) {
    return;
  }

  {
    rtc::CritScope critScoped(&_critSect);
    skin_player_->Reset();
    skin_record_->Reset();
    audio_frame_list_mixed_.clear();

    // Create thread which will drive the rendering.
    assert(_hMixThread == NULL);
    _hMixThread = CreateThread(NULL, 0, AudioMixThreadFunc, this, 0, NULL);
    if (_hMixThread == NULL) {
      RTC_LOG(LS_ERROR) << "failed to create the playout thread";
      return;
    }

    // Set thread priority to highest possible.
    SetThreadPriority(_hMixThread, THREAD_PRIORITY_TIME_CRITICAL);
  }  // critScoped

  RTC_LOG(LS_INFO) << "Started mix for recording";

  audio_state()->AddPlayerAudioSink(skin_player_);
  audio_state()->AddRecordAudioSink(skin_record_);
}

void WebRtcAudioMixForRecord::Stop() {
  {
    rtc::CritScope critScoped(&_critSect);

    audio_state()->RemovePlayerAudioSink(skin_player_);
    audio_state()->RemoveRecordAudioSink(skin_record_);

    if (_hMixThread == NULL) {
      RTC_LOG(LS_VERBOSE) << "no mix thread is active";
      return;
    }

    // stop the driving thread...
    RTC_LOG(LS_VERBOSE) << "closing down the mix thread...";
    SetEvent(_hShutdownMixEvent);
  }

  DWORD ret = WaitForSingleObject(_hMixThread, 2000);
  if (ret != WAIT_OBJECT_0) {
    // the thread did not stop as it should
    RTC_LOG(LS_ERROR) << "failed to close down webrtc_core_audio_render_thread";
    CloseHandle(_hMixThread);
    _hMixThread = NULL;
    return;
  }

  {
    rtc::CritScope critScoped(&_critSect);
    RTC_LOG(LS_VERBOSE) << "mix thread is now closed";

    // to reset this event manually at each time we finish with it,
    // in case that the render thread has exited before StopPlayout(),
    // this event might be caught by the new render thread within same VoE
    // instance.
    ResetEvent(_hShutdownMixEvent);

    CloseHandle(_hMixThread);
    _hMixThread = NULL;
    skin_player_->Reset();
    skin_record_->Reset();
    audio_frame_list_mixed_.clear();
  }
}

internal::AudioState* WebRtcAudioMixForRecord::audio_state() const {
  auto* audio_state = static_cast<internal::AudioState*>(audio_state_.get());
  RTC_DCHECK(audio_state);
  return audio_state;
}

DWORD WINAPI WebRtcAudioMixForRecord::AudioMixThreadFunc(LPVOID context) {
  return reinterpret_cast<WebRtcAudioMixForRecord*>(context)
      ->AudioMixThreadProcess();
}

bool WebRtcAudioMixForRecord::AudioMixThreadProcess() {
  bool keepMix = true;

  // wait time for cache audio data
  DWORD waitResult = WaitForSingleObject(_hShutdownMixEvent, 50);
  switch (waitResult) {
    case WAIT_OBJECT_0:  // _hShutdownCaptureEvent
      keepMix = false;
      break;
    case WAIT_TIMEOUT:  // timeout notification
      break;
    default:  // unexpected error
      RTC_LOG(LS_WARNING) << "Unknown wait termination on capture side";
      keepMix = false;
      break;
  }

  FileWrapper pcmFile = FileWrapper::Open("F://mix.pcm", false);

  int64_t _lastCallRecordMillis = rtc::TimeMillis();

  while (keepMix) {
    int64_t currentTime = rtc::TimeMillis();

    int64_t deltaTimeMillis = _lastCallRecordMillis - currentTime;
    _lastCallRecordMillis += 10;  // interval 10ms

    if (deltaTimeMillis > 0) {
      // Wait for a render notification event or a shutdown event
      DWORD waitResult =
          WaitForSingleObject(_hShutdownMixEvent, deltaTimeMillis);
      switch (waitResult) {
        case WAIT_OBJECT_0:  // _hShutdownCaptureEvent
          keepMix = false;
          break;
        case WAIT_TIMEOUT:  // timeout notification
          break;
        default:  // unexpected error
          RTC_LOG(LS_WARNING) << "Unknown wait termination on capture side";
          keepMix = false;
          break;
      }
    }

    if (!keepMix) {
      break;
    }

    {
      rtc::CritScope lock(&_critSect);

      recordsource_->SetFrame(skin_record_->GetFrame());
      playsource_->SetFrame(skin_player_->GetFrame());
    }
    AudioFrame* audio_frame = new AudioFrame();
    audio_mixer->Mix(kRecordingNumChannels, audio_frame);

    pcmFile.Write(audio_frame->data(),
                  kRecordingBufferSizeIn10Ms * sizeof(int16_t));
  }

  pcmFile.CloseFile();
  return true;
}

}  // namespace webrtc
