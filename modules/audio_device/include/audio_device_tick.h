/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_AUDIO_DEVICE_INCLUDE_AUDIO_DEVICE_TICK_H_
#define MODULES_AUDIO_DEVICE_INCLUDE_AUDIO_DEVICE_TICK_H_

#include "modules/audio_device/include/audio_device_defines.h"
#include "rtc_base/refcount.h"
#include "rtc_base/scoped_ref_ptr.h"

namespace webrtc {
class AudioTick {
 public:
  virtual void OnTick() {};

 protected:
 private:
};
}  // namespace webrtc

#endif  // MODULES_AUDIO_DEVICE_INCLUDE_AUDIO_DEVICE_TICK_H_
