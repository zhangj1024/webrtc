/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_device/dummy/file_audio_device.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/platform_thread.h"
#include "system_wrappers/include/sleep.h"
#include "api/audio/audio_frame.h"
#include "audio/remix_resample.h"
#include "modules/audio_processing/include/audio_processing.h"

namespace webrtc {

const int kRecordingFixedSampleRate = 44100;
const size_t kRecordingNumChannels = 2;
const size_t kRecordingBufferSize =
    kRecordingFixedSampleRate / 100 * kRecordingNumChannels * 2;

const int kRecordingFixedSampleRate48000 = 48000;

FileAudioDevice::FileAudioDevice(const char* inputFilename,
                                 const char* outputFilename)
    : _ptrAudioBuffer(NULL),
      _recordingBuffer(NULL),
      _recordingFramesLeft(0),
      _recordingBufferSizeIn10MS(0),
      _recordingFramesIn10MS(0),
      _recording(false),
      _lastCallRecordMillis(0),
      _inputFile(*FileWrapper::Create()),
      _inputFilename(inputFilename) {}

FileAudioDevice::~FileAudioDevice() {
  delete &_inputFile;
}

int32_t FileAudioDevice::ActiveAudioLayer(
    AudioDeviceModule::AudioLayer& audioLayer) const {
  return -1;
}

AudioDeviceGeneric::InitStatus FileAudioDevice::Init() {
  return InitStatus::OK;
}

int32_t FileAudioDevice::Terminate() {
  return 0;
}

bool FileAudioDevice::Initialized() const {
  return true;
}

int16_t FileAudioDevice::PlayoutDevices() {
  return 1;
}

int16_t FileAudioDevice::RecordingDevices() {
  return 1;
}

int32_t FileAudioDevice::PlayoutDeviceName(uint16_t index,
                                           char name[kAdmMaxDeviceNameSize],
                                           char guid[kAdmMaxGuidSize]) {
  const char* kName = "dummy_device";
  const char* kGuid = "dummy_device_unique_id";
  if (index < 1) {
    memset(name, 0, kAdmMaxDeviceNameSize);
    memset(guid, 0, kAdmMaxGuidSize);
    memcpy(name, kName, strlen(kName));
    memcpy(guid, kGuid, strlen(guid));
    return 0;
  }
  return -1;
}

int32_t FileAudioDevice::RecordingDeviceName(uint16_t index,
                                             char name[kAdmMaxDeviceNameSize],
                                             char guid[kAdmMaxGuidSize]) {
  const char* kName = "dummy_device";
  const char* kGuid = "dummy_device_unique_id";
  if (index < 1) {
    memset(name, 0, kAdmMaxDeviceNameSize);
    memset(guid, 0, kAdmMaxGuidSize);
    memcpy(name, kName, strlen(kName));
    memcpy(guid, kGuid, strlen(guid));
    return 0;
  }
  return -1;
}

int32_t FileAudioDevice::SetPlayoutDevice(uint16_t index) {
  if (index == 0) {
    return 0;
  }
  return -1;
}

int32_t FileAudioDevice::SetPlayoutDevice(
    AudioDeviceModule::WindowsDeviceType device) {
  return -1;
}

int32_t FileAudioDevice::SetRecordingDevice(uint16_t index) {
  if (index == 0) {
    _record_index = index;
    return _record_index;
  }
  return -1;
}

int32_t FileAudioDevice::SetRecordingDevice(
    AudioDeviceModule::WindowsDeviceType device) {
  return -1;
}

int32_t FileAudioDevice::PlayoutIsAvailable(bool& available) {
  available = false;
  return -1;
}

int32_t FileAudioDevice::InitPlayout() {
  return 0;
}

bool FileAudioDevice::PlayoutIsInitialized() const {
  return true;
}

int32_t FileAudioDevice::RecordingIsAvailable(bool& available) {
  if (_record_index == 0) {
    available = true;
    return _record_index;
  }
  available = false;
  return -1;
}

int32_t FileAudioDevice::InitRecording() {
  rtc::CritScope lock(&_critSect);

  if (_recording) {
    return -1;
  }

  _recordingFramesIn10MS = static_cast<size_t>(kRecordingFixedSampleRate / 100);
  _recordingFramesIn10MS48000 =
      static_cast<size_t>(kRecordingFixedSampleRate48000 / 100);

  if (_ptrAudioBuffer) {
    _ptrAudioBuffer->SetRecordingSampleRate(kRecordingFixedSampleRate48000);
    _ptrAudioBuffer->SetRecordingChannels(kRecordingNumChannels);
  }
  return 0;
}

bool FileAudioDevice::RecordingIsInitialized() const {
  return _recordingFramesIn10MS != 0;
}

int32_t FileAudioDevice::StartPlayout() {
  return 0;
}

int32_t FileAudioDevice::StopPlayout() {
  return 0;
}

bool FileAudioDevice::Playing() const {
  return true;
}

int32_t FileAudioDevice::StartRecording() {
  _recording = true;

  if (!_inputFilename.empty() &&
      !_inputFile.OpenFile(_inputFilename.c_str(), true)) {
    RTC_LOG(LS_ERROR) << "Failed to open audio input file: " << _inputFilename;
    _recording = false;
    return -1;
  }

  // Make sure we only create the buffer once.
  _recordingBufferSizeIn10MS = _recordingFramesIn10MS * kRecordingNumChannels;
  if (!_recordingBuffer) {
    _recordingBuffer = new int16_t[_recordingBufferSizeIn10MS];
  }

  _recordingBufferSizeIn10MS48000 =
      _recordingFramesIn10MS48000 * kRecordingNumChannels;
  if (!_recordingBuffer48000) {
    _recordingBuffer48000 = new int16_t[_recordingBufferSizeIn10MS48000];
  }

  _ptrThreadRec.reset(new rtc::PlatformThread(
      RecThreadFunc, this, "webrtc_audio_module_capture_thread"));

  _ptrThreadRec->Start();
  _ptrThreadRec->SetPriority(rtc::kRealtimePriority);

  RTC_LOG(LS_INFO) << "Started recording from input file: " << _inputFilename;

  return 0;
}

int32_t FileAudioDevice::StopRecording() {
  {
    rtc::CritScope lock(&_critSect);
    _recording = false;
  }

  if (_ptrThreadRec) {
    _ptrThreadRec->Stop();
    _ptrThreadRec.reset();
  }

  rtc::CritScope lock(&_critSect);
  _recordingFramesLeft = 0;
  if (_recordingBuffer) {
    delete[] _recordingBuffer;
    _recordingBuffer = NULL;
  }
  _inputFile.CloseFile();

  RTC_LOG(LS_INFO) << "Stopped recording from input file: " << _inputFilename;
  return 0;
}

bool FileAudioDevice::Recording() const {
  return _recording;
}

int32_t FileAudioDevice::InitSpeaker() {
  return -1;
}

bool FileAudioDevice::SpeakerIsInitialized() const {
  return false;
}

int32_t FileAudioDevice::InitMicrophone() {
  return 0;
}

bool FileAudioDevice::MicrophoneIsInitialized() const {
  return true;
}

int32_t FileAudioDevice::SpeakerVolumeIsAvailable(bool& available) {
  return -1;
}

int32_t FileAudioDevice::SetSpeakerVolume(uint32_t volume) {
  return -1;
}

int32_t FileAudioDevice::SpeakerVolume(uint32_t& volume) const {
  return -1;
}

int32_t FileAudioDevice::MaxSpeakerVolume(uint32_t& maxVolume) const {
  return -1;
}

int32_t FileAudioDevice::MinSpeakerVolume(uint32_t& minVolume) const {
  return -1;
}

int32_t FileAudioDevice::MicrophoneVolumeIsAvailable(bool& available) {
  return -1;
}

int32_t FileAudioDevice::SetMicrophoneVolume(uint32_t volume) {
  return -1;
}

int32_t FileAudioDevice::MicrophoneVolume(uint32_t& volume) const {
  return -1;
}

int32_t FileAudioDevice::MaxMicrophoneVolume(uint32_t& maxVolume) const {
  return -1;
}

int32_t FileAudioDevice::MinMicrophoneVolume(uint32_t& minVolume) const {
  return -1;
}

int32_t FileAudioDevice::SpeakerMuteIsAvailable(bool& available) {
  return -1;
}

int32_t FileAudioDevice::SetSpeakerMute(bool enable) {
  return -1;
}

int32_t FileAudioDevice::SpeakerMute(bool& enabled) const {
  return -1;
}

int32_t FileAudioDevice::MicrophoneMuteIsAvailable(bool& available) {
  return -1;
}

int32_t FileAudioDevice::SetMicrophoneMute(bool enable) {
  return -1;
}

int32_t FileAudioDevice::MicrophoneMute(bool& enabled) const {
  return -1;
}

int32_t FileAudioDevice::StereoPlayoutIsAvailable(bool& available) {
  available = true;
  return 0;
}
int32_t FileAudioDevice::SetStereoPlayout(bool enable) {
  return 0;
}

int32_t FileAudioDevice::StereoPlayout(bool& enabled) const {
  enabled = true;
  return 0;
}

int32_t FileAudioDevice::StereoRecordingIsAvailable(bool& available) {
  available = true;
  return 0;
}

int32_t FileAudioDevice::SetStereoRecording(bool enable) {
  return 0;
}

int32_t FileAudioDevice::StereoRecording(bool& enabled) const {
  enabled = true;
  return 0;
}

int32_t FileAudioDevice::PlayoutDelay(uint16_t& delayMS) const {
  return 0;
}

void FileAudioDevice::AttachAudioBuffer(AudioDeviceBuffer* audioBuffer) {
  rtc::CritScope lock(&_critSect);

  _ptrAudioBuffer = audioBuffer;

  // Inform the AudioBuffer about default settings for this implementation.
  // Set all values to zero here since the actual settings will be done by
  // InitPlayout and InitRecording later.
  _ptrAudioBuffer->SetRecordingSampleRate(0);
  _ptrAudioBuffer->SetPlayoutSampleRate(0);
  _ptrAudioBuffer->SetRecordingChannels(0);
  _ptrAudioBuffer->SetPlayoutChannels(0);
}

bool FileAudioDevice::RecThreadFunc(void* pThis) {
  return (static_cast<FileAudioDevice*>(pThis)->RecThreadProcess());
}

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


bool FileAudioDevice::RecThreadProcess() {
  if (!_recording) {
    return false;
  }

  int64_t currentTime = rtc::TimeMillis();
  if (_lastCallRecordMillis == 0) {
    _lastCallRecordMillis = currentTime;
  }

  int64_t deltaTimeMillis = _lastCallRecordMillis - currentTime;
  if (deltaTimeMillis > 0) {
    SleepMs(deltaTimeMillis);
  }

  if (_inputFile.is_open()) {
    if (_inputFile.Read(_recordingBuffer, kRecordingBufferSize) > 0) {
#if 0
      if (resampler.InitializeIfNeeded(kRecordingFixedSampleRate,
                                       kRecordingFixedSampleRate48000,
                                       kRecordingNumChannels) == -1) {
        FATAL() << "InitializeIfNeeded failed: sample_rate_hz = ";
      }

      int out_length = resampler.Resample(
          _recordingBuffer, _recordingBufferSizeIn10MS, _recordingBuffer48000,
          _recordingBufferSizeIn10MS48000);
      if (out_length == -1) {
        FATAL() << "Resample failed: audio_ptr = ";
        return false;
      }

      _ptrAudioBuffer->SetRecordedBuffer(_recordingBuffer48000,
                                         out_length / kRecordingNumChannels);
#else
      std::unique_ptr<AudioFrame> audio_frame(new AudioFrame());
      audio_frame->num_channels_ = kRecordingNumChannels;
      audio_frame->sample_rate_hz_ = kRecordingFixedSampleRate48000;

      InitializeCaptureFrame(
          kRecordingFixedSampleRate, kRecordingFixedSampleRate,
                             kRecordingNumChannels, audio_frame->num_channels_,
                             audio_frame.get());

      voe::RemixAndResample(_recordingBuffer, _recordingFramesIn10MS,
                            kRecordingNumChannels, kRecordingFixedSampleRate,
                            &resampler, audio_frame.get());

	_ptrAudioBuffer->SetRecordedBuffer(audio_frame->mutable_data(),
                                         audio_frame->samples_per_channel_);
#endif
    } else {
      _inputFile.Rewind();
    }
    _lastCallRecordMillis += 10;
    _ptrAudioBuffer->DeliverRecordedData();
  }

  return true;
}

}  // namespace webrtc
