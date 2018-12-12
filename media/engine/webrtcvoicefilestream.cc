#include "webrtcvoicefilestream.h"
/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio/audio_state.h"
#include "audio/remix_resample.h"
#include "audio/utility/audio_frame_operations.h"
#include "media/engine/webrtcinternalfileaudiosource.h"
#include "rtc_base/bind.h"
#include "rtc_base/thread_checker.h"
#include "system_wrappers/include/sleep.h"

const int kRecordingFixedSampleRate = 44100;
const size_t kRecordingNumChannels = 1;

const size_t kRecordingFramesIn10MS =
    static_cast<size_t>(kRecordingFixedSampleRate / 100);
const size_t kRecordingBufferSizeIn10Ms =
    kRecordingFramesIn10MS * kRecordingNumChannels;

static const int64_t datalenIn10Ms =
    kRecordingBufferSizeIn10Ms * sizeof(int16_t);

namespace webrtc {

WebRtcVoiceFileStream::WebRtcVoiceFileStream(
    const rtc::scoped_refptr<webrtc::AudioState>& audio_state,
    webrtc::RtcEventLog* event_log)
    : audio_state_(audio_state),
      _inputFile(*FileWrapper::Create()),
      playsource_(new InternalFileAudioSource()),
      recordsource_(new InternalFileAudioSource()),
      worker_thread_(rtc::Thread::Current()) {
  _hShutdownRenderEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  _hPauseEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
}

WebRtcVoiceFileStream::~WebRtcVoiceFileStream() {
  Stop();
  delete &_inputFile;
  delete playsource_;
  delete recordsource_;
}

void WebRtcVoiceFileStream::Start() {
  if (playing_) {
    return;
  }

  if (_hPlayThread != NULL) {
    return;
  }

  if (!_inputFilename.empty() &&
      !_inputFile.OpenFile(_inputFilename.c_str(), true)) {
    RTC_LOG(LS_ERROR) << "Failed to open audio input file: " << _inputFilename;
    return;
  }

  totalTime_ = _inputFile.Length() / datalenIn10Ms * 10 /*ms*/;

  if (!_recordingBuffer) {
    _recordingBuffer = new int16_t[kRecordingBufferSizeIn10Ms];
  }

  {
    rtc::CritScope critScoped(&_critSect);
    audio_frame_list_player_.clear();
    audio_frame_list_record_.clear();

    // Create thread which will drive the rendering.
    assert(_hPlayThread == NULL);
    _hPlayThread = CreateThread(NULL, 0, FileThreadFunc, this, 0, NULL);
    if (_hPlayThread == NULL) {
      RTC_LOG(LS_ERROR) << "failed to create the playout thread";
      return;
    }

    // Set thread priority to highest possible.
    SetThreadPriority(_hPlayThread, THREAD_PRIORITY_TIME_CRITICAL);
  }  // critScoped

  playing_ = true;
  playsource_->SetSampleRate(kRecordingFixedSampleRate);
  recordsource_->SetSampleRate(kRecordingFixedSampleRate);

  RTC_LOG(LS_INFO) << "Started recording from input file: " << _inputFilename;

  audio_state()->AddFileStream(this);
}

void WebRtcVoiceFileStream::Stop() {
  if (!playing_) {
    return;
  }

  SetPause(false);

  {
    rtc::CritScope critScoped(&_critSect);
    _inputFile.CloseFile();
    audio_state()->RemoveFileStream(this);

    if (_hPlayThread == NULL) {
      RTC_LOG(LS_VERBOSE)
          << "no rendering stream is active => close down WASAPI only";
      playing_ = false;
      return;
    }

    // stop the driving thread...
    RTC_LOG(LS_VERBOSE)
        << "closing down the webrtc_core_audio_render_thread...";
    SetEvent(_hShutdownRenderEvent);
  }  // critScoped

  DWORD ret = WaitForSingleObject(_hPlayThread, 2000);
  if (ret != WAIT_OBJECT_0) {
    // the thread did not stop as it should
    RTC_LOG(LS_ERROR) << "failed to close down webrtc_core_audio_render_thread";
    CloseHandle(_hPlayThread);
    _hPlayThread = NULL;
    playing_ = false;
    return;
  }

  {
    rtc::CritScope critScoped(&_critSect);
    RTC_LOG(LS_VERBOSE) << "webrtc_core_audio_render_thread is now closed";

    // to reset this event manually at each time we finish with it,
    // in case that the render thread has exited before StopPlayout(),
    // this event might be caught by the new render thread within same VoE
    // instance.
    ResetEvent(_hShutdownRenderEvent);

    CloseHandle(_hPlayThread);
    _hPlayThread = NULL;
    audio_frame_list_player_.clear();
    audio_frame_list_record_.clear();
    playing_ = false;
  }
}

internal::AudioState* WebRtcVoiceFileStream::audio_state() const {
  auto* audio_state = static_cast<internal::AudioState*>(audio_state_.get());
  RTC_DCHECK(audio_state);
  return audio_state;
}

void WebRtcVoiceFileStream::SetPlayFile(const std::string& file) {
  _inputFilename = file;
}

void WebRtcVoiceFileStream::OnBeforePlayData() {
  rtc::CritScope lock(&crit_sect_);
  if (!playing_) {
    return;
  }

  if (audio_frame_list_player_.size() == 0) {
    return;
  }

  AudioFrame* audio_frame = audio_frame_list_player_.front();
  audio_frame_list_player_.pop_front();

  AudioFrame* audio_frame_record = new AudioFrame();
  audio_frame_record->CopyFrom(*audio_frame);
  audio_frame_list_record_.push_back(audio_frame_record);

  std::unique_ptr<AudioFrame> audio_frame_player(audio_frame);
  playsource_->SetFrame(std::move(audio_frame_player));
};

void WebRtcVoiceFileStream::OnBeforeRecordData() {
  rtc::CritScope lock(&crit_sect_);
  if (!playing_) {
    return;
  }

  if (audio_frame_list_record_.size() == 0) {
    return;
  }

  AudioFrame* audio_frame = audio_frame_list_record_.front();
  audio_frame_list_record_.pop_front();

  std::unique_ptr<AudioFrame> audio_frame_record(audio_frame);
  recordsource_->SetFrame(std::move(audio_frame_record));
}

DWORD WINAPI WebRtcVoiceFileStream::FileThreadFunc(LPVOID context) {
  return reinterpret_cast<WebRtcVoiceFileStream*>(context)->FileThreadProcess();
}

bool WebRtcVoiceFileStream::FileThreadProcess() {
  for (auto tick : ticks_) {
    tick->OnPlayStart(true);
  }

  lastTime_ = 0;

  bool keepPlaying = true;

  while (keepPlaying) {
    // Wait for a render notification event or a shutdown event
    DWORD waitResult = WaitForSingleObject(_hPauseEvent, INFINITE);
    switch (waitResult) {
      case WAIT_OBJECT_0:  // _hPauseEvent, not pause
        break;
      case WAIT_TIMEOUT:  // timeout notification
        break;
      default:  // unexpected error
        RTC_LOG(LS_WARNING) << "Unknown wait termination on capture side";
        break;
    }

    // Wait for a render notification event or a shutdown event
    waitResult = WaitForSingleObject(_hShutdownRenderEvent, 5);
    switch (waitResult) {
      case WAIT_OBJECT_0:  // _hShutdownCaptureEvent
        keepPlaying = false;
        break;
      case WAIT_TIMEOUT:  // timeout notification
        break;
      default:  // unexpected error
        RTC_LOG(LS_WARNING) << "Unknown wait termination on capture side";
        keepPlaying = false;
        break;
    }

    if (!keepPlaying) {
      break;
    }

    if (audio_frame_list_player_.size() >= 5) {
      continue;
    }

    if (!playing_ || !_inputFile.is_open()) {
      return false;
    }

    rtc::CritScope lock(&crit_sect_);

    if (_inputFile.Read(_recordingBuffer,
                        kRecordingBufferSizeIn10Ms * sizeof(int16_t)) > 0) {
      AudioFrame* audio_frame = new AudioFrame();

      audio_frame->UpdateFrame(0, _recordingBuffer, kRecordingFramesIn10MS,
                               kRecordingFixedSampleRate,
                               AudioFrame::kUndefined, AudioFrame::kVadUnknown,
                               kRecordingNumChannels);
      //音量设置
      AudioFrameOperations::ScaleWithSat(output_gain, audio_frame);

      audio_frame_list_player_.push_back(audio_frame);
      OnTimeTick();
    } else {
      //_inputFile.Rewind();
      break;
    }
  }

  for (auto tick : ticks_) {
    tick->OnPlayStart(false);
  }

  invoker_.AsyncInvoke<void>(RTC_FROM_HERE, worker_thread_,
                             rtc::Bind(&WebRtcVoiceFileStream::Stop, this));
  return true;
}

void WebRtcVoiceFileStream::SetPause(bool pause) {
  pause_ = pause;
  if (pause) {
    ResetEvent(_hPauseEvent);
  } else {
    SetEvent(_hPauseEvent);
  }

  for (auto tick : ticks_) {
    tick->OnPlayPause(pause_);
  }
}

void WebRtcVoiceFileStream::OnTimeTick() {
  if (ticks_.empty()) {
    return;
  }

  int64_t curTime = GetPlayTime();
  if (curTime - lastTime_ >= 100 /*ms*/) {
    lastTime_ = curTime;

    //     RTC_LOG(LS_ERROR) << "lastTime_: " << lastTime_;
    for (auto tick : ticks_) {
      tick->OnPlayTimer(lastTime_, totalTime_);
    }
  }
}

int64_t WebRtcVoiceFileStream::GetPlayTime() {
  if (!playing_ || !_inputFile.is_open()) {
    return 0;
  }
  return _inputFile.Tell() / datalenIn10Ms * 10 /*ms*/;
}

bool WebRtcVoiceFileStream::SetPlayTime(int64_t time) {
  if (!playing_ || !_inputFile.is_open()) {
    return false;
  }

  if (_inputFile.Seek(time / 10 * datalenIn10Ms) == 0) {
    lastTime_ = 0;
    return true;
  } else {
    return false;
  }
}

void WebRtcVoiceFileStream::AddPlayCallback(PlayCallback* tick) {
  for (auto tick_ : ticks_) {
    if (tick_ == tick) {
      return;
    }
  }
  ticks_.push_back(tick);
}

void WebRtcVoiceFileStream::RemovePlayCallback(PlayCallback* tick) {
  for (auto itr = ticks_.begin(); itr != ticks_.end(); itr++) {
    if (*itr == tick) {
      ticks_.erase(itr);
      break;
    }
  }
}

}  // namespace webrtc
