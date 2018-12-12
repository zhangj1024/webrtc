#include "webrtcinternalfileaudiosource.h"
/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

namespace webrtc {

InternalFileAudioSource::InternalFileAudioSource(){};

AudioMixer::Source::AudioFrameInfo
InternalFileAudioSource::GetAudioFrameWithInfo(int sample_rate_hz,
                                               AudioFrame* audio_frame) {
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
int InternalFileAudioSource::Ssrc() const {
  return 0;
}

// A way for this source to say that GetAudioFrameWithInfo called
// with this sample rate or higher will not cause quality loss.
int InternalFileAudioSource::PreferredSampleRate() const {
  return _sample_rate;
}

void InternalFileAudioSource::SetSampleRate(uint32_t sample_rate) {
  _sample_rate = sample_rate;
}

InternalFileAudioSource::~InternalFileAudioSource() {
  audio_data_ = NULL;
}

void InternalFileAudioSource::SetFrame(std::unique_ptr<AudioFrame> audio_data) {
  audio_data_ = std::move(audio_data);
};

}  // namespace webrtc
