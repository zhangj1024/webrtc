/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_DESKTOP_CAPTURE_WIN_LYRIC_RENDER_H_
#define MODULES_DESKTOP_CAPTURE_WIN_LYRIC_RENDER_H_

#include <string>
#include "rtc_base/refcount.h"
#include "rtc_base/scoped_ref_ptr.h"
#include "modules/audio_device/include/audio_file_playback.h"

namespace webrtc {

// class I420Buffer;
class VideoFrame;

class LyricRenderInterface : public PlayCallback {
 public:
  static LyricRenderInterface* Create();

 public:
  virtual bool SetLyric(const std::string& lyric) = 0;
  virtual bool MaskFrame(const VideoFrame& frame) = 0;
};

}  // namespace webrtc

#endif  // MODULES_DESKTOP_CAPTURE_WIN_LYRIC_RENDER_H_
