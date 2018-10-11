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
#include "rtc_base/thread_checker.h"

const int kRecordingFixedSampleRate = 44100;
const size_t kRecordingNumChannels = 1;

const size_t kRecordingFramesIn10MS =
    static_cast<size_t>(kRecordingFixedSampleRate / 100);
const size_t kRecordingBufferSizeIn10Ms =
    kRecordingFramesIn10MS * kRecordingNumChannels;

namespace webrtc {

class InternalFileAudioSource : public AudioMixer::Source {
 public:
  InternalFileAudioSource() {};
  AudioFrameInfo GetAudioFrameWithInfo(int sample_rate_hz,
                                       AudioFrame* audio_frame) override {

// 	RTC_LOG(LS_ERROR) << <<  __FUNCTION__ << rtc::TimeMillis();

    if (!audio_data_) {
      return AudioFrameInfo::kError;
    }
    if (audio_data_->muted()) {
      return AudioFrameInfo::kMuted;
    }

	audio_frame->num_channels_ = audio_data_->num_channels_;
    audio_frame->sample_rate_hz_ = sample_rate_hz;

    voe::RemixAndResample(*audio_data_.get(), &capture_resampler_, audio_frame);
    audio_data_ = NULL;
    return AudioFrameInfo::kNormal;
  }

  // A way for a mixer implementation to distinguish participants.
  int Ssrc() const override { return 0; }

  // A way for this source to say that GetAudioFrameWithInfo called
  // with this sample rate or higher will not cause quality loss.
  int PreferredSampleRate() const override { return _sample_rate; }

  void SetSampleRate(uint32_t sample_rate) { _sample_rate = sample_rate; }

  ~InternalFileAudioSource() override { audio_data_ = NULL; }

  void SetFrame(std::unique_ptr<AudioFrame> audio_data) {
    audio_data_ = std::move(audio_data);
  };

 private:
  std::unique_ptr<AudioFrame> audio_data_;
  PushResampler<int16_t> capture_resampler_;
  uint32_t _sample_rate = 0;
};

WebRtcVoiceFileStream::WebRtcVoiceFileStream(
    const rtc::scoped_refptr<webrtc::AudioState>& audio_state,
    webrtc::RtcEventLog* event_log)
    : audio_state_(audio_state),
      _inputFile(*FileWrapper::Create()),
      playsource_(new InternalFileAudioSource()),
      recordsource_(new InternalFileAudioSource()) {}

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

  if (!_inputFilename.empty() &&
      !_inputFile.OpenFile(_inputFilename.c_str(), true)) {
    RTC_LOG(LS_ERROR) << "Failed to open audio input file: " << _inputFilename;
    return;
  }

  if (!_recordingBuffer) {
    _recordingBuffer = new int16_t[kRecordingBufferSizeIn10Ms];
  }

  playing_ = true;
  playsource_->SetSampleRate(kRecordingFixedSampleRate);
  recordsource_->SetSampleRate(kRecordingFixedSampleRate);
  audio_state()->AddFileStream(this);

  //get mic and player 
}

void WebRtcVoiceFileStream::Stop() {
  if (!playing_) {
    return;
  }
  playing_ = false;
  audio_state()->RemoveFileStream(this);

  _inputFile.CloseFile();
}

internal::AudioState* WebRtcVoiceFileStream::audio_state() const {
  auto* audio_state = static_cast<internal::AudioState*>(audio_state_.get());
  RTC_DCHECK(audio_state);
  return audio_state;
}

void WebRtcVoiceFileStream::SetPlayFile(const std::string& file) {
  _inputFilename = file;
}

void WebRtcVoiceFileStream::OnTick() {
  rtc::CritScope lock(&crit_sect_);
  if (!playing_) {
    return;
  }

  // get file data
  if (_inputFile.Read(_recordingBuffer,
                      kRecordingBufferSizeIn10Ms * sizeof(int16_t)) > 0) {
  } else {
    _inputFile.Rewind();
    return;
  }

  std::unique_ptr<AudioFrame> audio_frame(new AudioFrame());

  audio_frame->UpdateFrame(0, _recordingBuffer, kRecordingFramesIn10MS,
                           kRecordingFixedSampleRate,
                           AudioFrame::kUndefined,
                           AudioFrame::kVadUnknown, kRecordingNumChannels);
  
  //“Ù¡ø…Ë÷√
  AudioFrameOperations::ScaleWithSat(output_gain, audio_frame.get());

  std::unique_ptr<AudioFrame> audio_frame_copy(new AudioFrame());
  audio_frame_copy->CopyFrom(*audio_frame.get());

  playsource_->SetFrame(std::move(audio_frame_copy));
  recordsource_->SetFrame(std::move(audio_frame));
};

}  // namespace webrtc
