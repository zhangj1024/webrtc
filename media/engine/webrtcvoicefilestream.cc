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

#include "audio/remix_resample.h"
#include "audio/audio_state.h"
#include "rtc_base/thread_checker.h"
#include "audio/utility/audio_frame_operations.h"

const int kRecordingFixedSampleRate = 44100;
const size_t kRecordingNumChannels = 1;
const size_t kRecordingBufferSize =
    kRecordingFixedSampleRate / 100 * kRecordingNumChannels * 2;

const size_t _recordingFramesIn10MS =
    static_cast<size_t>(kRecordingFixedSampleRate / 100);
const size_t _recordingBufferSizeIn10MS =
    _recordingFramesIn10MS * kRecordingNumChannels;

namespace webrtc {

WebRtcVoiceFileStream::WebRtcVoiceFileStream(
    const rtc::scoped_refptr<webrtc::AudioState>& audio_state,
    webrtc::RtcEventLog* event_log)
    : audio_state_(audio_state), _inputFile(*FileWrapper::Create()) {}

WebRtcVoiceFileStream::~WebRtcVoiceFileStream() {
  Stop();
  delete &_inputFile;
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
    _recordingBuffer = new int16_t[_recordingBufferSizeIn10MS];
  }

  playing_ = true;
  audio_state()->AddFileStream(this);
}

void WebRtcVoiceFileStream::Stop() {
  if (!playing_) {
    return;
  }
  playing_ = false;
  audio_state()->RemoveFileStream(this);

  _inputFile.CloseFile();
}

void WebRtcVoiceFileStream::SetSink(AudioSinkInterface* sink) {}

void WebRtcVoiceFileStream::SetGain(float gain) {
  output_gain = gain;
}

AudioMixer::Source::AudioFrameInfo WebRtcVoiceFileStream::GetAudioFrameWithInfo(
    int sample_rate_hz,
    AudioFrame* audio_frame) {
  rtc::CritScope lock(&crit_sect_);

  // get file data
  if (_inputFile.Read(_recordingBuffer, kRecordingBufferSize) > 0) {
  } else {
    _inputFile.Rewind();
    return AudioMixer::Source::AudioFrameInfo::kNormal;
  }

  audio_frame->num_channels_ = kRecordingNumChannels;
  audio_frame->sample_rate_hz_ = sample_rate_hz;

  voe::RemixAndResample(_recordingBuffer, _recordingFramesIn10MS,
                        kRecordingNumChannels, kRecordingFixedSampleRate,
                        &resampler, audio_frame);

  //“Ù¡ø…Ë÷√
  AudioFrameOperations::ScaleWithSat(output_gain, audio_frame);

  return AudioMixer::Source::AudioFrameInfo::kNormal;
}

int WebRtcVoiceFileStream::Ssrc() const {
  return 0;
}

int WebRtcVoiceFileStream::PreferredSampleRate() const {
  return kRecordingFixedSampleRate;
}

internal::AudioState* WebRtcVoiceFileStream::audio_state() const {
  auto* audio_state = static_cast<internal::AudioState*>(audio_state_.get());
  RTC_DCHECK(audio_state);
  return audio_state;
}

void WebRtcVoiceFileStream::SetPlayFile(const std::string& file) {
  _inputFilename = file;
}

}  // namespace webrtc
