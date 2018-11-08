/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/desktop_capture/win/lyric_render.h"
#include <list>
#include <string>
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "modules/audio_device/include/audio_file_playback.h"
#include "rtc_base/logging.h"
#include "third_party/freetype/src/include/ft2build.h"
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include "modules/desktop_capture/win/lyric_prase.h"
#include "rtc_base/stringutils.h"
#include "third_party/libyuv/include/libyuv/convert_argb.h"
#include "third_party/libyuv/include/libyuv/convert_from_argb.h"
#include "rtc_base/criticalsection.cc"

namespace webrtc {

class LyricRender : public LyricRenderInterface {
 public:
  LyricRender();
  ~LyricRender();

  // LyricRenderInterface
  bool SetLyric(const std::string& lyric) override;
  bool MaskFrame(const VideoFrame& frame) override;

 protected:
  // PlayCallback
  void OnPlayTimer(int64_t cur, int64_t total) override;
  void OnPlayStart(bool start) override;
  void OnPlayPause(bool pause) override;

 private:
  std::list<FT_Glyph> _bitmaps;
  LyricPrase _lyrc_prase;

  LyricLine* _playline = NULL;

  uint8_t _xoffset = 10;
  uint8_t _yoffset = 10;
  uint8_t _xspace = 5;
};

LyricRenderInterface* LyricRenderInterface::Create() {
  return new LyricRender();
}

LyricRender::LyricRender() {
};

LyricRender::~LyricRender(){};

bool LyricRender::SetLyric(const std::string& lyrictext) {
  return _lyrc_prase.Prase(lyrictext);
}

bool LyricRender::MaskFrame(const VideoFrame& frame) {
  if (_playline == NULL) {
    return false;
  }

  rtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer(
      frame.video_frame_buffer());
  if (buffer->type() != webrtc::VideoFrameBuffer::Type::kI420) {
    return NULL;
  }

//   uint8_t played_length = 0;
//   _critSect.Enter();
//   for (auto it = line->_words.begin(); it != line->_words.end(); it++) {
//     if (_play_time < line->_offset + it->_offset + it->_continue) {
// 
//     }
//   }
//   _critSect.Leave();

  unsigned int xoffset = _xoffset;

  rtc::scoped_refptr<I420BufferInterface> i420buf = buffer->ToI420();

  uint8_t* dataY = const_cast<uint8_t*>(i420buf->DataY());
  uint8_t* dataU = const_cast<uint8_t*>(i420buf->DataU());
  uint8_t* dataV = const_cast<uint8_t*>(i420buf->DataV());
  const unsigned int width = i420buf->width();
  const unsigned int height = i420buf->height();
  const int stride_y = i420buf->StrideY();
  const int stride_u = i420buf->StrideU();
  const int stride_v = i420buf->StrideV();

  unsigned int x = 0;
  unsigned int y = 0;

  for (auto it = _playline->_words.begin(); it != _playline->_words.end();
       it++) {
    for (size_t i = 0; i < it->glyphs.size(); i++) {
      FT_Glyph& glyph = it->glyphs.at(i);
      FT_BitmapGlyph bitmap_glyph = (FT_BitmapGlyph)glyph;
      FT_Bitmap& bitmap = bitmap_glyph->bitmap;

      //每个字的高度不一样，已底线为准
//       int yindex = _yoffset + _playline->_hegiht - bitmap.rows;
      int yindex = _yoffset + it->glyph_height_offsets.at(i);

      for (unsigned int i = 0; i < bitmap.rows; i++) {
        y = i + yindex;
        if (y >= height) {
          break;
        }

        for (unsigned int j = 0; j < bitmap.width; j++) {
          x = j + xoffset;
          if (x >= width) {
            break;
          }

          if (bitmap.buffer[i * bitmap.width + j]) {
            dataY[y * stride_y + x] = 120;
            dataU[y / 2 * stride_u + x / 2] = 0;
            dataV[y / 2 * stride_v + x / 2] = 240;
          }
        }
      }
      xoffset += bitmap.width + _xspace;
    }
  }

  //   uint8_t* dst_argb = new uint8_t[width * height * 4];
  //   memset(dst_argb, 200, width * height * 4);
  //   libyuv::ARGBToI420(dst_argb, width, dataY, stride_y, dataU,
  //                                stride_u, dataV, stride_v, width, height);
  //   free(dst_argb);
  return true;
}

void LyricRender::OnPlayTimer(int64_t cur, int64_t total) {
  //获取字体数据
  std::vector<LyricLine>& lines = _lyrc_prase.GetLines();

  for (auto it = lines.begin(); it != lines.end(); it++) {
    if (_lyrc_prase._offset + it->_offset + it->_continue > (uint64_t)cur) {
      _playline = &*it;
      break;
    }
  }
}

void LyricRender::OnPlayStart(bool start) {
  if (!start) {
    _playline = NULL;
  }
}

void LyricRender::OnPlayPause(bool pause) {}

}  // namespace webrtc
