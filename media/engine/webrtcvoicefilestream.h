#ifndef _WEBRTCVOICEFILESTREAM_
#define _WEBRTCVOICEFILESTREAM_

#include "api/audio/audio_mixer.h"
#include "api/call/audio_sink.h"
#include "audio/audio_state.h"
#include "call/call.h"
#include "call/syncable.h"
#include "common_audio/resampler/include/push_resampler.h"
#include "rtc_base/system/file_wrapper.h"
#include "rtc_base/thread_checker.h"

namespace webrtc {

class WebRtcVoiceFileStream final : public AudioMixer::Source,
                                    public AudioTick {
 public:
  WebRtcVoiceFileStream(
      const rtc::scoped_refptr<webrtc::AudioState>& audio_state,
      webrtc::RtcEventLog* event_log);
  ~WebRtcVoiceFileStream() override;

  void Start();
  void Stop();
  void SetSink(AudioSinkInterface* sink);
  void SetGain(float gain);

  // AudioMixer::Source
  AudioFrameInfo GetAudioFrameWithInfo(int sample_rate_hz,
                                       AudioFrame* audio_frame) override;
  int Ssrc() const override;
  int PreferredSampleRate() const override;

  void SetPlayFile(const std::string &file);

  void OnTick() override {};

 private:
  internal::AudioState* audio_state() const;
  rtc::scoped_refptr<webrtc::AudioState> audio_state_;

  bool playing_ = false;

  std::string _inputFilename;
  FileWrapper& _inputFile;
  int16_t* _recordingBuffer = NULL;  // In bytes.

  rtc::CriticalSection crit_sect_;

  PushResampler<int16_t> resampler;

  float output_gain = 0.1f;
  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(WebRtcVoiceFileStream);
};
}  // namespace webrtc

#endif  //_WEBRTCVOICEFILESTREAM_
