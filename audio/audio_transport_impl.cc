/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio/audio_transport_impl.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "api/call/audio_sink.h"
#include "audio/remix_resample.h"
#include "audio/utility/audio_frame_operations.h"
#include "call/audio_send_stream.h"
#include "modules/audio_mixer/audio_mixer_impl.h"
#include "rtc_base/logging.h"

namespace webrtc {

namespace {

// We want to process at the lowest sample rate and channel count possible
// without losing information. Choose the lowest native rate at least equal to
// the minimum of input and codec rates, choose lowest channel count, and
// configure the audio frame.
void InitializeCaptureFrame(int input_sample_rate,
                            int send_sample_rate_hz,
                            size_t input_num_channels,
                            size_t send_num_channels,
                            AudioFrame* audio_frame) {
  RTC_DCHECK(audio_frame);
  int min_processing_rate_hz = std::min(input_sample_rate, send_sample_rate_hz);
  for (int native_rate_hz : AudioProcessing::kNativeSampleRatesHz) {
    audio_frame->sample_rate_hz_ = native_rate_hz;
    if (audio_frame->sample_rate_hz_ >= min_processing_rate_hz) {
      break;
    }
  }
  audio_frame->num_channels_ = std::min(input_num_channels, send_num_channels);
}

void ProcessCaptureFrame(uint32_t delay_ms,
                         bool key_pressed,
                         bool swap_stereo_channels,
                         AudioProcessing* audio_processing,
                         AudioFrame* audio_frame) {
  RTC_DCHECK(audio_processing);
  RTC_DCHECK(audio_frame);
  RTC_DCHECK(
      !audio_processing->echo_cancellation()->is_drift_compensation_enabled());
  audio_processing->set_stream_delay_ms(delay_ms);
  audio_processing->set_stream_key_pressed(key_pressed);
  int error = audio_processing->ProcessStream(audio_frame);
  RTC_DCHECK_EQ(0, error) << "ProcessStream() error: " << error;
  if (swap_stereo_channels) {
    AudioFrameOperations::SwapStereoChannels(audio_frame);
  }
}

// Resample audio in |frame| to given sample rate preserving the
// channel count and place the result in |destination|.
int Resample(const AudioFrame& frame,
             const int destination_sample_rate,
             PushResampler<int16_t>* resampler,
             int16_t* destination) {
  const int number_of_channels = static_cast<int>(frame.num_channels_);
  const int target_number_of_samples_per_channel =
      destination_sample_rate / 100;
  resampler->InitializeIfNeeded(frame.sample_rate_hz_, destination_sample_rate,
                                number_of_channels);

  // TODO(yujo): make resampler take an AudioFrame, and add special case
  // handling of muted frames.
  return resampler->Resample(
      frame.data(), frame.samples_per_channel_ * number_of_channels,
      destination, number_of_channels * target_number_of_samples_per_channel);
}
}  // namespace

class InternalAudioSource : public AudioMixer::Source {
 public:
  InternalAudioSource() : _sample_rate(0){};
  AudioFrameInfo GetAudioFrameWithInfo(int sample_rate_hz,
                                       AudioFrame* audio_frame) override {
    //no useable
    InitializeCaptureFrame(_sample_rate, sample_rate_hz, _number_of_channels,
                           _send_num_channels, audio_frame);

    audio_frame->sample_rate_hz_ = sample_rate_hz;
    audio_frame->num_channels_ =
        std::min(_number_of_channels, _send_num_channels);

    voe::RemixAndResample(_audio_data, _number_of_frames, _number_of_channels,
                          _sample_rate, &capture_resampler_, audio_frame);

    return AudioFrameInfo::kNormal;
  }

  // A way for a mixer implementation to distinguish participants.
  int Ssrc() const override { return 0; }

  // A way for this source to say that GetAudioFrameWithInfo called
  // with this sample rate or higher will not cause quality loss.
  int PreferredSampleRate() const override { return _sample_rate; }

  ~InternalAudioSource() override {}

  void SetFrame(const void* audio_data,
                const size_t number_of_frames,
                const size_t bytes_per_sample,
                const size_t number_of_channels,
                const uint32_t sample_rate) {
    _audio_data = static_cast<const int16_t*>(audio_data),
    _number_of_frames = number_of_frames;
    _bytes_per_sample = bytes_per_sample;
    _number_of_channels = number_of_channels;
    _sample_rate = sample_rate;
  };

  void SetSendParam(int send_sample_rate_hz, size_t send_num_channels) {
    _send_sample_rate_hz = send_sample_rate_hz;
    _send_num_channels = send_num_channels;
  }

 private:
  const int16_t* _audio_data = NULL;
  size_t _number_of_frames = 0;
  size_t _bytes_per_sample = 0;
  size_t _number_of_channels = 0;
  uint32_t _sample_rate = 0;

  int _send_sample_rate_hz = 0;
  size_t _send_num_channels = 0;
  PushResampler<int16_t> capture_resampler_;
};

AudioTransportImpl::AudioTransportImpl(AudioMixer* mixer,
                                       AudioMixer* record_mixer,
                                       AudioProcessing* audio_processing)
    : audio_processing_(audio_processing),
      play_mixer_(mixer),
      record_source_(new InternalAudioSource()),
      record_mixer_(record_mixer),
      tick_ (NULL){
  RTC_DCHECK(mixer);
  RTC_DCHECK(audio_processing);
  record_mixer_->AddSource(record_source_);
}

AudioTransportImpl::~AudioTransportImpl() {}

// Not used in Chromium. Process captured audio and distribute to all sending
// streams, and try to do this at the lowest possible sample rate.
int32_t AudioTransportImpl::RecordedDataIsAvailable(
    const void* audio_data,
    const size_t number_of_frames,
    const size_t bytes_per_sample,
    const size_t number_of_channels,
    const uint32_t sample_rate,
    const uint32_t audio_delay_milliseconds,
    const int32_t /*clock_drift*/,
    const uint32_t /*volume*/,
    const bool key_pressed,
    uint32_t& /*new_mic_volume*/) {  // NOLINT: to avoid changing APIs
  RTC_DCHECK(audio_data);
  RTC_DCHECK_GE(number_of_channels, 1);
  RTC_DCHECK_LE(number_of_channels, 2);
  RTC_DCHECK_EQ(2 * number_of_channels, bytes_per_sample);
  RTC_DCHECK_GE(sample_rate, AudioProcessing::NativeRate::kSampleRate8kHz);
  // 100 = 1 second / data duration (10 ms).
  RTC_DCHECK_EQ(number_of_frames * 100, sample_rate);
  RTC_DCHECK_LE(bytes_per_sample * number_of_frames * number_of_channels,
                AudioFrame::kMaxDataSizeBytes);

  int send_sample_rate_hz = 0;
  size_t send_num_channels = 0;
  bool swap_stereo_channels = false;
  {
    rtc::CritScope lock(&capture_lock_);
    send_sample_rate_hz = send_sample_rate_hz_;
    send_num_channels = send_num_channels_;
    swap_stereo_channels = swap_stereo_channels_;
  }

  if (tick_) {
    tick_->OnBeforeRecordData();
  }

  {
	// do not mix bgm, bgm mix in player
    rtc::CritScope cs(&record_skin_lock_);
    if (record_sink_) {
      AudioSinkInterface::Data data(static_cast<const int16_t*>(audio_data),
                                    number_of_frames, sample_rate,
                                    number_of_channels, 0);
      record_sink_->OnData(data);
    }
  }

  std::unique_ptr<AudioFrame> audio_frame(new AudioFrame());

  if (record_mixer_ && record_mixer_->SourceCnt() > 1) {
    record_source_->SetSendParam(send_sample_rate_hz, send_num_channels);
    record_source_->SetFrame(audio_data, number_of_frames, bytes_per_sample,
                             number_of_channels, sample_rate);

    record_mixer_->Mix(number_of_channels, &record_mixed_frame_);

    audio_frame->CopyFrom(record_mixed_frame_);
  } else {
    InitializeCaptureFrame(sample_rate, send_sample_rate_hz, number_of_channels,
                           send_num_channels, audio_frame.get());

    voe::RemixAndResample(static_cast<const int16_t*>(audio_data),
                          number_of_frames, number_of_channels, sample_rate,
                          &capture_resampler_, audio_frame.get());
  }

  ProcessCaptureFrame(audio_delay_milliseconds, key_pressed,
                      swap_stereo_channels, audio_processing_,
                      audio_frame.get());

  // Typing detection (utilizes the APM/VAD decision). We let the VAD determine
  // if we're using this feature or not.
  // TODO(solenberg): is_enabled() takes a lock. Work around that.
  bool typing_detected = false;
  if (audio_processing_->voice_detection()->is_enabled()) {
    if (audio_frame->vad_activity_ != AudioFrame::kVadUnknown) {
      bool vad_active = audio_frame->vad_activity_ == AudioFrame::kVadActive;
      typing_detected = typing_detection_.Process(key_pressed, vad_active);
    }
  }

  // Measure audio level of speech after all processing.
  double sample_duration = static_cast<double>(number_of_frames) / sample_rate;
  audio_level_.ComputeLevel(*audio_frame.get(), sample_duration);

  // Copy frame and push to each sending stream. The copy is required since an
  // encoding task will be posted internally to each stream.
  {
    rtc::CritScope lock(&capture_lock_);
    typing_noise_detected_ = typing_detected;

    RTC_DCHECK_GT(audio_frame->samples_per_channel_, 0);
    if (!sending_streams_.empty()) {
      auto it = sending_streams_.begin();
      while (++it != sending_streams_.end()) {
        std::unique_ptr<AudioFrame> audio_frame_copy(new AudioFrame());
        audio_frame_copy->CopyFrom(*audio_frame.get());
        (*it)->SendAudioData(std::move(audio_frame_copy));
      }
      // Send the original frame to the first stream w/o copying.
      (*sending_streams_.begin())->SendAudioData(std::move(audio_frame));
    }
  }

  return 0;
}

// Mix all received streams, feed the result to the AudioProcessing module, then
// resample the result to the requested output rate.
int32_t AudioTransportImpl::NeedMorePlayData(const size_t nSamples,
                                             const size_t nBytesPerSample,
                                             const size_t nChannels,
                                             const uint32_t samplesPerSec,
                                             void* audioSamples,
                                             size_t& nSamplesOut,
                                             int64_t* elapsed_time_ms,
                                             int64_t* ntp_time_ms) {
  RTC_DCHECK_EQ(sizeof(int16_t) * nChannels, nBytesPerSample);
  RTC_DCHECK_GE(nChannels, 1);
  RTC_DCHECK_LE(nChannels, 2);
  RTC_DCHECK_GE(
      samplesPerSec,
      static_cast<uint32_t>(AudioProcessing::NativeRate::kSampleRate8kHz));

  // 100 = 1 second / data duration (10 ms).
  RTC_DCHECK_EQ(nSamples * 100, samplesPerSec);
  RTC_DCHECK_LE(nBytesPerSample * nSamples * nChannels,
                AudioFrame::kMaxDataSizeBytes);

  if (tick_) {
    tick_->OnBeforePlayData();
  }

  play_mixer_->Mix(nChannels, &play_mixed_frame_);
  *elapsed_time_ms = play_mixed_frame_.elapsed_time_ms_;
  *ntp_time_ms = play_mixed_frame_.ntp_time_ms_;

  const auto error =
      audio_processing_->ProcessReverseStream(&play_mixed_frame_);
  RTC_DCHECK_EQ(error, AudioProcessing::kNoError);

  nSamplesOut = Resample(play_mixed_frame_, samplesPerSec, &render_resampler_,
                         static_cast<int16_t*>(audioSamples));

  rtc::CritScope cs(&player_skin_lock_);
  if (player_sink_) {
    AudioSinkInterface::Data data(static_cast<int16_t*>(audioSamples), nSamples,
                                  samplesPerSec, nChannels,
                                  play_mixed_frame_.timestamp_);
    player_sink_->OnData(data);
  }

  RTC_DCHECK_EQ(nSamplesOut, nChannels * nSamples);
  return 0;
}

#ifdef ChromiumWebrtc
// Used by Chromium - same as NeedMorePlayData() but because Chrome has its
// own APM instance, does not call audio_processing_->ProcessReverseStream().
void AudioTransportImpl::PullRenderData(int bits_per_sample,
                                        int sample_rate,
                                        size_t number_of_channels,
                                        size_t number_of_frames,
                                        void* audio_data,
                                        int64_t* elapsed_time_ms,
                                        int64_t* ntp_time_ms) {
  RTC_DCHECK_EQ(bits_per_sample, 16);
  RTC_DCHECK_GE(number_of_channels, 1);
  RTC_DCHECK_LE(number_of_channels, 2);
  RTC_DCHECK_GE(sample_rate, AudioProcessing::NativeRate::kSampleRate8kHz);

  // 100 = 1 second / data duration (10 ms).
  RTC_DCHECK_EQ(number_of_frames * 100, sample_rate);

  // 8 = bits per byte.
  RTC_DCHECK_LE(bits_per_sample / 8 * number_of_frames * number_of_channels,
                AudioFrame::kMaxDataSizeBytes);
  play_mixer_->Mix(number_of_channels, &play_mixed_frame_);
  *elapsed_time_ms = play_mixed_frame_.elapsed_time_ms_;
  *ntp_time_ms = play_mixed_frame_.ntp_time_ms_;

  auto output_samples =
      Resample(play_mixed_frame_, sample_rate, &render_resampler_,
               static_cast<int16_t*>(audio_data));
  RTC_DCHECK_EQ(output_samples, number_of_channels * number_of_frames);
}
#endif  // ChromiumWebrtc

void AudioTransportImpl::RegisterTickCallback(AudioTick* tick) {
  tick_ = tick;
}

void AudioTransportImpl::UpdateSendingStreams(
    std::vector<AudioSendStream*> streams,
    int send_sample_rate_hz,
    size_t send_num_channels) {
  rtc::CritScope lock(&capture_lock_);
  sending_streams_ = std::move(streams);
  send_sample_rate_hz_ = send_sample_rate_hz;
  send_num_channels_ = send_num_channels;
}

void AudioTransportImpl::SetStereoChannelSwapping(bool enable) {
  rtc::CritScope lock(&capture_lock_);
  swap_stereo_channels_ = enable;
}

bool AudioTransportImpl::typing_noise_detected() const {
  rtc::CritScope lock(&capture_lock_);
  return typing_noise_detected_;
}

void AudioTransportImpl::SetPlayerAudioSkin(AudioSinkInterface* audio_sink) {
  rtc::CritScope lock(&record_skin_lock_);
  player_sink_ = audio_sink;
}

void AudioTransportImpl::SetRecordAudioSkin(AudioSinkInterface* audio_sink) {
  rtc::CritScope lock(&player_skin_lock_);
  record_sink_ = audio_sink;
}

}  // namespace webrtc
