#ifndef _WEBRTCRECORD_
#define _WEBRTCRECORD_

#include "api/audio/audio_mixer.h"
#include "api/call/audio_sink.h"
#include "audio/audio_state.h"
#include "call/call.h"
#include "call/syncable.h"
#include "common_audio/resampler/include/push_resampler.h"
#include "modules/audio_device/include/audio_file_playback.h"
#include "rtc_base/asyncinvoker.h"
#include "rtc_base/platform_thread.h"
#include "rtc_base/system/file_wrapper.h"
#include "rtc_base/thread_checker.h"

namespace webrtc {

class InternalFileAudioSource;

class AudioSkin;

class WebRtcRecordPlayerMix {
 public:
  WebRtcRecordPlayerMix(
      const rtc::scoped_refptr<webrtc::AudioState>& audio_state,
      webrtc::RtcEventLog* event_log);
  ~WebRtcRecordPlayerMix();

  void Start();
  void Stop();

  void SetDataCallback(AudioSinkInterface* cb) { sink_mix_data_callback_ = cb; };
  bool IsRunning();

 private:
  static DWORD WINAPI AudioMixThreadFunc(LPVOID context);
  bool AudioMixThreadProcess();

  rtc::CriticalSection _critSect;
  std::list<AudioFrame*> audio_frame_list_mixed_;

  internal::AudioState* audio_state() const;
  rtc::scoped_refptr<webrtc::AudioState> audio_state_;

  rtc::Thread* worker_thread_ = nullptr;

  HANDLE _hMixThread = NULL;
  HANDLE _hShutdownMixEvent = NULL;

  rtc::scoped_refptr<AudioMixer> audio_mixer;
  InternalFileAudioSource* playsource_;
  InternalFileAudioSource* recordsource_;

  AudioSkin* sink_player_;
  AudioSkin* sink_record_;

  rtc::CriticalSection _critSectCb;
  AudioSinkInterface* sink_mix_data_callback_;

  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(WebRtcRecordPlayerMix);
};
}  // namespace webrtc


#endif  //_WEBRTCRECORD_
