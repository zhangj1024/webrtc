/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_AUDIO_DEVICE_INCLUDE_AUDIO_FILE_CALLBACK_H_
#define MODULES_AUDIO_DEVICE_INCLUDE_AUDIO_FILE_CALLBACK_H_

namespace webrtc {
class PlayCallback {
 public:
  virtual void OnPlayTimer(int64_t cur, int64_t total) = 0;
  virtual void OnPlayEnded() = 0;
};
}  // namespace webrtc

#endif  // MODULES_AUDIO_DEVICE_INCLUDE_AUDIO_FILE_CALLBACK_H_
