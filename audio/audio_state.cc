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

#include <algorithm>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "audio/audio_receive_stream.h"
#include "media/engine/webrtcvoicefilestream.h"
#include "modules/audio_device/include/audio_device.h"
#include "rtc_base/atomicops.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/thread.h"

namespace webrtc {
namespace internal {

AudioState::AudioState(const AudioState::Config& config)
    : config_(config),
      audio_transport_(config_.audio_mixer,
                       config_.record_audio_mixer,
                       config_.audio_processing.get()) {
  process_thread_checker_.DetachFromThread();
  RTC_DCHECK(config_.audio_mixer);
  RTC_DCHECK(config_.audio_device_module);
}

AudioState::~AudioState() {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  RTC_DCHECK(receiving_streams_.empty());
  RTC_DCHECK(sending_streams_.empty());
}

AudioProcessing* AudioState::audio_processing() {
  RTC_DCHECK(config_.audio_processing);
  return config_.audio_processing.get();
}

AudioTransport* AudioState::audio_transport() {
  return &audio_transport_;
}

bool AudioState::typing_noise_detected() const {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  return audio_transport_.typing_noise_detected();
}

void AudioState::AddReceivingStream(webrtc::AudioReceiveStream* stream) {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  RTC_DCHECK_EQ(0, receiving_streams_.count(stream));
  receiving_streams_.insert(stream);
  if (!config_.audio_mixer->AddSource(
          static_cast<internal::AudioReceiveStream*>(stream))) {
    RTC_DLOG(LS_ERROR) << "Failed to add source to mixer.";
  }

  InitAndStartPlayout();
}

void AudioState::RemoveReceivingStream(webrtc::AudioReceiveStream* stream) {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  auto count = receiving_streams_.erase(stream);
  RTC_DCHECK_EQ(1, count);
  config_.audio_mixer->RemoveSource(
      static_cast<internal::AudioReceiveStream*>(stream));
  if (receiving_streams_.empty()) {
    if (config_.audio_device_module->BuiltInAECIsAvailable()) {
      if (sending_streams_.empty()) {
        config_.audio_device_module->StopRecording();
      } else {
        return;
      }
    }

    config_.audio_device_module->StopPlayout();
  }
}

void AudioState::AddSendingStream(webrtc::AudioSendStream* stream,
                                  int sample_rate_hz,
                                  size_t num_channels) {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  auto& properties = sending_streams_[stream];
  properties.sample_rate_hz = sample_rate_hz;
  properties.num_channels = num_channels;
  UpdateAudioTransportWithSendingStreams();

  InitAndStartRecording();
}

void AudioState::RemoveSendingStream(webrtc::AudioSendStream* stream) {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  auto count = sending_streams_.erase(stream);
  RTC_DCHECK_EQ(1, count);
  UpdateAudioTransportWithSendingStreams();
  if (sending_streams_.empty()) {
    config_.audio_device_module->StopRecording();
    if (config_.audio_device_module->BuiltInAECIsAvailable()) {
      if (receiving_streams_.empty()) {
        config_.audio_device_module->StopPlayout();
      }
    }
  }
}

void AudioState::AddFileStream(webrtc::WebRtcVoiceFileStream* stream) {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());

  //   receiving_streams_.insert((webrtc::AudioReceiveStream*)stream);
  if (!config_.audio_mixer->AddSource(stream->GetPlaySource())) {
    RTC_DLOG(LS_ERROR) << "Failed to add source to mixer.";
  }

  if (!config_.record_audio_mixer->AddSource(stream->GetRecordSource())) {
    RTC_DLOG(LS_ERROR) << "Failed to add source to record_audio_mixer.";
  }

  InitAndStartPlayout();
  InitAndStartRecording();

  this->audio_transport_.RegisterTickCallback((AudioTick*)stream);
}

void AudioState::RemoveFileStream(webrtc::WebRtcVoiceFileStream* stream) {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  //   receiving_streams_.erase((webrtc::AudioReceiveStream*)stream);
  //   if (receiving_streams_.empty()) {
  //     config_.audio_device_module->StopPlayout();
  //   }

  config_.audio_mixer->RemoveSource(stream->GetPlaySource());
  config_.record_audio_mixer->RemoveSource(stream->GetRecordSource());

  this->audio_transport_.RegisterTickCallback(NULL);
}

void AudioState::AddPlayerAudioSink(webrtc::AudioSinkInterface* skin) {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  this->audio_transport_.SetPlayerAudioSkin(skin);
}

void AudioState::RemovePlayerAudioSink(webrtc::AudioSinkInterface* skin) {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  this->audio_transport_.SetPlayerAudioSkin(NULL);
}

void AudioState::AddRecordAudioSink(webrtc::AudioSinkInterface* skin) {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  this->audio_transport_.SetRecordAudioSkin(skin);
}

void AudioState::RemoveRecordAudioSink(webrtc::AudioSinkInterface* skin) {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  this->audio_transport_.SetRecordAudioSkin(NULL);
}

void AudioState::SetPlayout(bool enabled) {
  RTC_LOG(INFO) << "SetPlayout(" << enabled << ")";
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  if (playout_enabled_ != enabled) {
    playout_enabled_ = enabled;
    if (enabled) {
      null_audio_poller_.reset();
      if (!receiving_streams_.empty()) {
        config_.audio_device_module->StartPlayout();
      }
    } else {
      if (config_.audio_device_module->BuiltInAECIsAvailable()) {
        if (sending_streams_.empty()) {
          config_.audio_device_module->StopRecording();
        } else {
          playout_enabled_ = true;
          return;
        }
      }
      config_.audio_device_module->StopPlayout();
      null_audio_poller_ =
          absl::make_unique<NullAudioPoller>(&audio_transport_);
    }
  }
}

void AudioState::SetRecording(bool enabled) {
  RTC_LOG(INFO) << "SetRecording(" << enabled << ")";
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  if (recording_enabled_ != enabled) {
    recording_enabled_ = enabled;
    if (enabled) {
      if (!sending_streams_.empty()) {
        if (config_.audio_device_module->BuiltInAECIsAvailable()) {
          InitAndStartPlayout();
          null_audio_poller_.reset();
          playout_enabled_ = true;
        }

        config_.audio_device_module->StartRecording();
      }
    } else {
      config_.audio_device_module->StopRecording();
    }
  }
}

AudioState::Stats AudioState::GetAudioInputStats() const {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  const voe::AudioLevel& audio_level = audio_transport_.audio_level();
  Stats result;
  result.audio_level = audio_level.LevelFullRange();
  RTC_DCHECK_LE(0, result.audio_level);
  RTC_DCHECK_GE(32767, result.audio_level);
  result.total_energy = audio_level.TotalEnergy();
  result.total_duration = audio_level.TotalDuration();
  return result;
}

void AudioState::SetStereoChannelSwapping(bool enable) {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  audio_transport_.SetStereoChannelSwapping(enable);
}

// Reference count; implementation copied from rtc::RefCountedObject.
void AudioState::AddRef() const {
  rtc::AtomicOps::Increment(&ref_count_);
}

// Reference count; implementation copied from rtc::RefCountedObject.
rtc::RefCountReleaseStatus AudioState::Release() const {
  if (rtc::AtomicOps::Decrement(&ref_count_) == 0) {
    delete this;
    return rtc::RefCountReleaseStatus::kDroppedLastRef;
  }
  return rtc::RefCountReleaseStatus::kOtherRefsRemained;
}

void AudioState::UpdateAudioTransportWithSendingStreams() {
  RTC_DCHECK(thread_checker_.CalledOnValidThread());
  std::vector<webrtc::AudioSendStream*> sending_streams;
  int max_sample_rate_hz = 8000;
  size_t max_num_channels = 1;
  for (const auto& kv : sending_streams_) {
    sending_streams.push_back(kv.first);
    max_sample_rate_hz = std::max(max_sample_rate_hz, kv.second.sample_rate_hz);
    max_num_channels = std::max(max_num_channels, kv.second.num_channels);
  }
  audio_transport_.UpdateSendingStreams(std::move(sending_streams),
                                        max_sample_rate_hz, max_num_channels);
}
void AudioState::InitAndStartRecording() {
  // Make sure recording is initialized; start recording if enabled.
  auto* adm = config_.audio_device_module.get();
  if (!adm->Recording()) {
    if (adm->InitRecording() == 0) {
      if (recording_enabled_) {
        if (adm->BuiltInAECIsAvailable()) {
          InitAndStartPlayout();
        }
        adm->StartRecording();
      }
    } else {
      RTC_DLOG_F(LS_ERROR) << "Failed to initialize recording.";
    }
  }
}
void AudioState::InitAndStartPlayout() {
  // Make sure playback is initialized; start playing if enabled.
  auto* adm = config_.audio_device_module.get();
  if (!adm->Playing()) {
    if (adm->InitPlayout() == 0) {
      if (playout_enabled_) {
        adm->StartPlayout();
      }
    } else {
      RTC_DLOG_F(LS_ERROR) << "Failed to initialize playout.";
    }
  }
}

}  // namespace internal

rtc::scoped_refptr<AudioState> AudioState::Create(
    const AudioState::Config& config) {
  return rtc::scoped_refptr<AudioState>(new internal::AudioState(config));
}
}  // namespace webrtc
