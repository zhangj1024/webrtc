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
#include <fstream>  
#include <streambuf> 
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "modules/audio_device/include/audio_file_playback.h"
#include "rtc_base/logging.h"
#include "third_party/freetype/src/include/ft2build.h"
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include "modules/desktop_capture/win/lyric_prase.h"
#include "rtc_base/criticalsection.cc"
#include "rtc_base/stringutils.h"
#include "third_party/libyuv/include/libyuv/convert_argb.h"
#include "third_party/libyuv/include/libyuv/convert_from_argb.h"
#include "third_party/zlib/zlib.h"

namespace webrtc {

class LyricRender : public LyricRenderInterface {
 public:
  LyricRender();
  ~LyricRender();

  // LyricRenderInterface
  bool SetLyric(const std::string& lyric) override;
  bool SetKrcLyric(const std::string& file) override;
  bool MaskFrame(const VideoFrame& frame) override;
  void SetDisplay(bool display) override;
  bool IsDisplay() override;

 protected:
  // PlayCallback
  void OnPlayTimer(int64_t cur, int64_t total) override;
  void OnPlayStart(bool start) override;
  void OnPlayPause(bool pause) override;

 private:
  void RenderLine(LyricLine* line,
                  uint32_t xoffset,
                  uint32_t yoffset,
                  uint32_t played_length,
                  rtc::scoped_refptr<I420BufferInterface> i420buf);

 private:
  std::list<FT_Glyph> _bitmaps;
  LyricPrase _lyrc_prase;

  LyricLine* _playline = NULL;
  LyricLine* _nextline = NULL;
  uint32_t _played_length = 0;
  volatile bool _display = true;

  uint32_t _xoffset = 10;
  uint32_t _yoffset = 10;
  uint32_t _xspace = 5;

  uint8_t _playedY;
  uint8_t _playedU;
  uint8_t _playedV;

  uint8_t _Y;
  uint8_t _U;
  uint8_t _V;
};

LyricRenderInterface* LyricRenderInterface::Create() {
  return new LyricRender();
}

LyricRender::LyricRender() {
  uint8_t R = 255;
  uint8_t G = 0;
  uint8_t B = 0;

  _playedY = ((66 * R + 129 * G + 25 * B) >> 8) + 16;
  _playedU = ((-38 * R - 74 * G + 112 * B) >> 8) + 128;
  _playedV = ((112 * R - 94 * G - 18 * B) >> 8) + 128;

  R = 0;
  G = 0;
  B = 255;

  _Y = ((66 * R + 129 * G + 25 * B) >> 8) + 16;
  _U = ((-38 * R - 74 * G + 112 * B) >> 8) + 128;
  _V = ((112 * R - 94 * G - 18 * B) >> 8) + 128;
};

LyricRender::~LyricRender(){};

bool LyricRender::SetLyric(const std::string& lyrictext) {
  return _lyrc_prase.Prase(lyrictext);
}

std::string UTF8_To_string(const std::string& str) {
  int nwLen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);

  wchar_t* pwBuf = new wchar_t[nwLen + 1];  //一定要加1，不然会出现尾巴
  memset(pwBuf, 0, nwLen * 2 + 2);

  MultiByteToWideChar(CP_UTF8, 0, str.c_str(), str.length(), pwBuf, nwLen);

  int nLen = WideCharToMultiByte(CP_ACP, 0, pwBuf, -1, NULL, NULL, NULL, NULL);

  char* pBuf = new char[nLen + 1];
  memset(pBuf, 0, nLen + 1);

  WideCharToMultiByte(CP_ACP, 0, pwBuf, nwLen, pBuf, nLen, NULL, NULL);

  std::string retStr = pBuf;

  delete[] pBuf;
  delete[] pwBuf;

  pBuf = NULL;
  pwBuf = NULL;

  return retStr;
}

static const unsigned char KrcKeys[] = {64, 71, 97, 119, 94,  50,  116, 71,
                              81, 54, 49, 45,  206, 210, 110, 105};

bool LyricRender::SetKrcLyric(const std::string& file) {
  FILE* pFile = fopen(file.c_str(), "rb");
  if (pFile == NULL) {
    RTC_LOG(LERROR) << "File error";
    return false;
  }

  /* 获取文件大小 */
  fseek(pFile, 0, SEEK_END);
  long lSize = ftell(pFile);
  rewind(pFile);

  /* 分配内存存储整个文件 */
  char* buffer = new char[lSize];
  if (buffer == NULL) {
    fclose(pFile);
    RTC_LOG(LERROR) << "Memory error";
    return false;
  }

  /* 将文件拷贝到buffer中 */
  size_t result = fread(buffer, 1, lSize, pFile);
  if (result != (size_t)lSize) {
    fclose(pFile);
    RTC_LOG(LERROR) << "Reading error";
    return false;
  }

  fclose(pFile);

  if (strncmp(buffer, "krc1", 4) != 0) {
    delete[] buffer;
    return false;
  }
  unsigned char* krcData = (unsigned char *)(buffer + 4);
  long krcDataLen = lSize - 4;

  // XOR 大法解码
  for (long i = 0; i < krcDataLen; i++) {
    krcData[i] ^= KrcKeys[i % 16];
  }

  unsigned long decodeLen = compressBound(krcDataLen * 3);
  unsigned char* decodeData = new unsigned char[decodeLen];

  if (uncompress(decodeData, &decodeLen, krcData, krcDataLen) != Z_OK) {
    delete[] buffer;
    delete[] decodeData;
    return false;
  }

  bool ret = _lyrc_prase.Prase(UTF8_To_string(std::string((char*)decodeData)));

  delete[] buffer;
  delete[] decodeData;

  return ret;
}

bool LyricRender::MaskFrame(const VideoFrame& frame) {
  if (_playline == NULL || !_display) {
    return false;
  }

  rtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer(
      frame.video_frame_buffer());
  if (buffer->type() != webrtc::VideoFrameBuffer::Type::kI420) {
    return NULL;
  }

  rtc::scoped_refptr<I420BufferInterface> i420buf = buffer->ToI420();

  RenderLine(_playline, _xoffset, _yoffset, _played_length, i420buf);

  if (_nextline != NULL) {
    RenderLine(_nextline, _xoffset + 5, _yoffset + _playline->_hegiht + 5, 0,
               i420buf);
  }

#ifdef TESTTEST
  uint8_t* dst_argb = new uint8_t[width * height * 4];
  memset(dst_argb, 200, width * height * 4);
  libyuv::ARGBToI420(dst_argb, width, dataY, stride_y, dataU, stride_u, dataV,
                     stride_v, width, height);
  free(dst_argb);
#endif

  return true;
}

void LyricRender::RenderLine(LyricLine* line,
                             uint32_t xoffset,
                             uint32_t yoffset,
                             uint32_t played_length,
                             rtc::scoped_refptr<I420BufferInterface> i420buf) {
  played_length += xoffset;
  uint8_t* dataY = const_cast<uint8_t*>(i420buf->DataY());
  uint8_t* dataU = const_cast<uint8_t*>(i420buf->DataU());
  uint8_t* dataV = const_cast<uint8_t*>(i420buf->DataV());
  const uint32_t width = i420buf->width();
  const uint32_t height = i420buf->height();
  const int stride_y = i420buf->StrideY();
  const int stride_u = i420buf->StrideU();
  const int stride_v = i420buf->StrideV();

  uint32_t x = 0;
  uint32_t y = 0;

  for (auto it = line->_words.begin(); it != line->_words.end(); it++) {
    for (size_t glyph_index = 0; glyph_index < it->glyphs.size();
         glyph_index++) {
      FT_Glyph& glyph = it->glyphs.at(glyph_index);
      FT_BitmapGlyph bitmap_glyph = (FT_BitmapGlyph)glyph;
      FT_Bitmap& bitmap = bitmap_glyph->bitmap;

      //每个字的高度不一样，已底线为准
      int yindex = yoffset + it->glyph_height_offsets.at(glyph_index);

      for (uint32_t i = 0; i < bitmap.rows; i++) {
        y = i + yindex;
        if (y >= height) {
          break;
        }

        for (uint32_t j = 0; j < bitmap.width; j++) {
          x = j + xoffset;
          if (x > width) {
            break;
          }

          if (bitmap.buffer[i * bitmap.width + j]) {
            if (x < played_length) {
              dataY[y * stride_y + x] = _playedY;
              dataU[y / 2 * stride_u + x / 2] = _playedU;
              dataV[y / 2 * stride_v + x / 2] = _playedV;
            } else {
              dataY[y * stride_y + x] = _Y;
              dataU[y / 2 * stride_u + x / 2] = _U;
              dataV[y / 2 * stride_v + x / 2] = _V;
            }
          }
        }
      }
      xoffset += bitmap.width + _xspace;
    }
  }
}

void LyricRender::OnPlayTimer(int64_t cur, int64_t total) {
  if (!_display) {
    return;
  }
  //获取字体数据
  uint64_t curtime = (uint64_t)cur;

  if (curtime < _lyrc_prase._offset) {
    _playline = NULL;
    _played_length = 0;
    return;
  }

  curtime -= _lyrc_prase._offset;
  std::vector<LyricLine>& lines = _lyrc_prase.GetLines();
  for (auto lineitr = lines.begin(); lineitr != lines.end(); lineitr++) {
    if ((lineitr->_offset + lineitr->_continue) > curtime) {
      _playline = &*lineitr;

      if (++lineitr != lines.end()) {
        _nextline = &*lineitr;
      } else {
        _nextline = NULL;
      }

      break;
    }
  }

  if (curtime < _playline->_offset) {
    _played_length = 0;
    return;
  }

  curtime -= _playline->_offset;
  uint32_t played_length = 0;
  for (auto lineitr = _playline->_words.begin();
       lineitr != _playline->_words.end(); lineitr++) {
    uint32_t word_length = 0;
    for (auto glyphitr = lineitr->glyphs.begin();
         glyphitr != lineitr->glyphs.end(); glyphitr++) {
      FT_BitmapGlyph bitmap_glyph = (FT_BitmapGlyph)*glyphitr;
      RTC_LOG(LS_ERROR) << lineitr->_word
                        << " width:" << bitmap_glyph->bitmap.width;
      word_length += bitmap_glyph->bitmap.width;
      if (glyphitr != lineitr->glyphs.begin()) {
        played_length += _xspace;
      }
    }

    if (lineitr != _playline->_words.begin()) {
      played_length += _xspace;
    }

    if (curtime > (lineitr->_offset + lineitr->_continue)) {
      played_length += word_length;
    } else if (curtime > lineitr->_offset) {
      played_length += word_length * ((curtime - lineitr->_offset) * 1.0 /
                                      lineitr->_continue);
      break;
    }
  }

  RTC_LOG(LS_ERROR) << "curtime:" << curtime
                    << " played_length:" << played_length;

  _played_length = played_length;
}

void LyricRender::OnPlayStart(bool start) {
  if (!start) {
    _playline = NULL;
  }
}

void LyricRender::OnPlayPause(bool pause) {}

void LyricRender::SetDisplay(bool display) {
  _display = display;
}

bool LyricRender::IsDisplay() {
  return _display;
}

}  // namespace webrtc
