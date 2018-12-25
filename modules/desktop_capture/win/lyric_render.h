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
#include "modules/audio_device/include/audio_file_playback.h"
#include "rtc_base/refcount.h"
#include "rtc_base/scoped_ref_ptr.h"

namespace webrtc {

class FontSetting {
 public:
  std::string face;
  std::string face_style;
  int face_size = 0;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool strikeout = false;
  inline bool operator==(FontSetting obj_) const {
    return face == obj_.face && face_style == obj_.face_style &&
           face_size == obj_.face_size && bold == obj_.bold &&
           italic == obj_.italic && underline == obj_.underline &&
           strikeout == obj_.strikeout;
  }

  inline void operator=(FontSetting obj_) {
    face = obj_.face;
    face_style = obj_.face_style;
    face_size = obj_.face_size;
    bold = obj_.bold;
    italic = obj_.italic;
    underline = obj_.underline;
    strikeout = obj_.strikeout;
  }
};

class ColorSetting {
 public:
  uint32_t color = 0xFFFFFF;
  uint32_t opacity = 100;
  inline bool operator==(ColorSetting obj_) const {
    return color == obj_.color && opacity == obj_.opacity;
  }
  inline void operator=(ColorSetting obj_) {
    color = obj_.color;
    opacity = obj_.opacity;
  }
};

// class I420Buffer;
class VideoFrame;

class LyricRenderInterface : public PlayCallback {
 public:
  static LyricRenderInterface* Create();
  static void GlobleInit();
  static void GlobleUnInit();

 public:
  virtual bool SetLyric(const std::string& lyric) = 0;
  virtual bool SetKrcLyric(const std::string& file) = 0;
  virtual bool MaskFrame(const VideoFrame& frame) = 0;
  virtual void SetDisplay(bool display) = 0;
  virtual bool IsDisplay() = 0;
  virtual void SetOffset(int x, int y) = 0;
  virtual void GetOffset(int &x, int &y) = 0;
  virtual void SetPlayedColor(ColorSetting color) = 0;
  virtual void SetNoplayColor(ColorSetting color) = 0;
  virtual void SetFont(FontSetting font) = 0;
  virtual ColorSetting GetPlayedColor() = 0;
  virtual ColorSetting GetNoplayColor() = 0;
  virtual FontSetting GetFont() = 0;
};

}  // namespace webrtc

#endif  // MODULES_DESKTOP_CAPTURE_WIN_LYRIC_RENDER_H_
