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

class WebRtcVoiceFileStream final : public webrtc::AudioReceiveStream,
                                    public AudioMixer::Source,
                                    public Syncable {
 public:
  WebRtcVoiceFileStream(
      const rtc::scoped_refptr<webrtc::AudioState>& audio_state,
      webrtc::RtcEventLog* event_log);
  ~WebRtcVoiceFileStream() override;

  // webrtc::AudioReceiveStream implementation.
  void Reconfigure(const webrtc::AudioReceiveStream::Config& config) override;
  void Start() override;
  void Stop() override;
  webrtc::AudioReceiveStream::Stats GetStats() const override;
  void SetSink(AudioSinkInterface* sink) override;
  void SetGain(float gain) override;
  std::vector<webrtc::RtpSource> GetSources() const override;

  // AudioMixer::Source
  AudioFrameInfo GetAudioFrameWithInfo(int sample_rate_hz,
                                       AudioFrame* audio_frame) override;
  int Ssrc() const override;
  int PreferredSampleRate() const override;

  // Syncable
  int id() const override;
  absl::optional<Syncable::Info> GetInfo() const override;
  uint32_t GetPlayoutTimestamp() const override;
  void SetMinimumPlayoutDelay(int delay_ms) override;

  void AssociateSendStream(AudioSendStream* send_stream);
  void SignalNetworkState(NetworkState state);

  void SetPlayFile(const std::string &file);
 private:
  internal::AudioState* audio_state() const;
  rtc::scoped_refptr<webrtc::AudioState> audio_state_;

  bool playing_ = false;

  std::string _inputFilename;
  FileWrapper& _inputFile;
  int16_t* _recordingBuffer = NULL;  // In bytes.

  //   int current_sample_rate_hz = 44100;

  rtc::CriticalSection crit_sect_;

  PushResampler<int16_t> resampler;

  float output_gain = 0.1f;
  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(WebRtcVoiceFileStream);
};
}  // namespace webrtc

#endif  //_WEBRTCVOICEFILESTREAM_
