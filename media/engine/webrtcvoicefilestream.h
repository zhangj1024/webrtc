#ifndef _WEBRTCVOICEFILESTREAM_
#define _WEBRTCVOICEFILESTREAM_

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

class WebRtcVoiceFileStream final : public AudioTick {
 public:
  WebRtcVoiceFileStream(
      const rtc::scoped_refptr<webrtc::AudioState>& audio_state,
      webrtc::RtcEventLog* event_log);
  ~WebRtcVoiceFileStream();

  void Start();
  void Stop();
  //   void SetSink(AudioSinkInterface* sink);
  void SetGain(float gain) { output_gain = gain; };
  float GetGain() { return output_gain; };

  void SetPlayFile(const std::string& file);

  // AudioTick imp
  void OnBeforePlayData() override;
  void OnBeforeRecordData() override;

  AudioMixer::Source* GetPlaySource() {
    return (AudioMixer::Source*)playsource_;
  };
  AudioMixer::Source* GetRecordSource() {
    return (AudioMixer::Source*)recordsource_;
  };

  void SetPause(bool pause);
  bool IsPause() { return pause_; };
  bool IsPlaying() { return playing_; };

  void AddPlayCallback(PlayCallback* tick);
  void RemovePlayCallback(PlayCallback* tick);
  bool SetPlayTime(int64_t time);
  int64_t GetPlayTotalTime() { return totalTime_; };

 private:
  static DWORD WINAPI FileThreadFunc(LPVOID context);
  bool FileThreadProcess();
  int64_t GetPlayTime();
  void OnTimeTick();

  rtc::CriticalSection _critSect;
  std::list<AudioFrame*> audio_frame_list_player_;
  std::list<AudioFrame*> audio_frame_list_record_;

  HANDLE _hPlayThread = NULL;
  HANDLE _hShutdownRenderEvent = NULL;
  HANDLE _hPauseEvent = NULL;

  internal::AudioState* audio_state() const;
  rtc::scoped_refptr<webrtc::AudioState> audio_state_;

  volatile bool playing_ = false;
  volatile bool pause_ = false;

  std::string _inputFilename;
  FileWrapper& _inputFile;
  int16_t* _recordingBuffer = NULL;  // In bytes.

  rtc::CriticalSection crit_sect_;

  PushResampler<int16_t> resampler;

  volatile float output_gain = 1.0f;

  InternalFileAudioSource* playsource_;
  InternalFileAudioSource* recordsource_;

  std::vector<PlayCallback*> ticks_;
  int64_t lastTime_ = 0;
  int64_t totalTime_ = 0;

  rtc::AsyncInvoker invoker_;
  rtc::Thread* worker_thread_ = nullptr;

  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(WebRtcVoiceFileStream);
};
}  // namespace webrtc

#endif  //_WEBRTCVOICEFILESTREAM_
