#ifndef _WEBRTINTERNALFILEAUDIOSOURCE_H_
#define _WEBRTINTERNALFILEAUDIOSOURCE_H_

#include "api/audio/audio_mixer.h"
#include "audio/remix_resample.h"

namespace webrtc {

class InternalFileAudioSource : public AudioMixer::Source {
 public:
  InternalFileAudioSource();
  AudioFrameInfo GetAudioFrameWithInfo(int sample_rate_hz,
                                       AudioFrame* audio_frame) override;

  // A way for a mixer implementation to distinguish participants.
  int Ssrc() const override;

  // A way for this source to say that GetAudioFrameWithInfo called
  // with this sample rate or higher will not cause quality loss.
  int PreferredSampleRate() const override;

  void SetSampleRate(uint32_t sample_rate);

  ~InternalFileAudioSource() override;

  void SetFrame(std::unique_ptr<AudioFrame> audio_data);

public:
  std::unique_ptr<AudioFrame> audio_data_;
  PushResampler<int16_t> capture_resampler_;
  uint32_t _sample_rate = 0;
};

}  // namespace webrtc

#endif  //_WEBRTINTERNALFILEAUDIOSOURCE_H_
