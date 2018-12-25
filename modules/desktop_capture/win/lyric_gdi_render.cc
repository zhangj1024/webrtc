/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <fstream>
#include <list>
#include <streambuf>
#include <string>
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "modules/audio_device/include/audio_file_playback.h"
#include "modules/desktop_capture/win/lyric_gdi_text.h"
#include "modules/desktop_capture/win/lyric_render.h"
#include "rtc_base/logging.h"
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
  void SetOffset(int x, int y) override;
  void GetOffset(int& x, int& y) override;

  void SetPlayedColor(ColorSetting color) override;
  void SetNoplayColor(ColorSetting color) override;
  void SetFont(FontSetting font) override;

  ColorSetting GetPlayedColor() override;
  ColorSetting GetNoplayColor() override;
  FontSetting GetFont() override;

 protected:
  // PlayCallback
  void OnPlayTimer(int64_t cur, int64_t total) override;
  void OnPlayStart(bool start) override;
  void OnPlayPause(bool pause) override;

 private:
  void RenderLine(LyricLine* line,
                  int xoffset,
                  int yoffset,
                  int played_length,
                  rtc::scoped_refptr<I420BufferInterface> i420buf);

 private:
  LyricPrase _lyrc_prase;

  LyricLine* _playline = NULL;
  LyricLine* _nextline = NULL;
  int _played_length = 0;
  volatile bool _display = true;

  int _line_space = 6;
  int _xoffset = 0;
  int _yoffset = 0;
};

LyricRenderInterface* LyricRenderInterface::Create() {
  return new LyricRender();
}

void LyricRenderInterface::GlobleInit() {
  InitGdi();
}
void LyricRenderInterface::GlobleUnInit() {
  UnInitGdi();
}

LyricRender::LyricRender(){};

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
  unsigned char* krcData = (unsigned char*)(buffer + 4);
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
    RenderLine(_nextline, _xoffset, _yoffset + _playline->height + _line_space,
               0, i420buf);
  }

  return true;
}

void LyricRender::RenderLine(LyricLine* line,
                             int xoffset,
                             int yoffset,
                             int played_length,
                             rtc::scoped_refptr<I420BufferInterface> i420buf) {
  rtc::scoped_refptr<I420Buffer> i420buffer = (I420Buffer*)(i420buf.get());

#ifdef TESTTEST
  uint8_t R = 255;
  uint8_t G = 0;
  uint8_t B = 0;

  uint8_t _playedY = ((66 * R + 129 * G + 25 * B) >> 8) + 16;
  uint8_t _playedU = ((-38 * R - 74 * G + 112 * B) >> 8) + 128;
  uint8_t _playedV = ((112 * R - 94 * G - 18 * B) >> 8) + 128;

  for (int h = 0; h < height / 4; h++) {
    for (int w = 0; w < width / 4; w++) {
      dataY[h * stride_y + w] = _playedY;
      dataU[h / 2 * stride_u + w / 2] = _playedU;
      dataV[h / 2 * stride_v + w / 2] = _playedV;
    }
  }
#endif
#ifdef TESTTEST
  uint8_t* dst_argb = new uint8_t[width * height * 4];
  memset(dst_argb, 200, width * height * 4);
  libyuv::ARGBToI420(dst_argb, width, dataY, stride_y, dataU, stride_u, dataV,
                     stride_v, width, height);
  free(dst_argb);
#endif

#if 0
  int x, y;
  rtc::scoped_refptr<I420Buffer> buffer;
//   uint8_t* data = line->rgb_data_.get();

  for (int lyric_h = 0; lyric_h < line->height; lyric_h++) {
    y = lyric_h + yoffset;
    if (y >= i420buf->height()) {
      break;
    }
    for (int lyric_w = 0; lyric_w < line->width; lyric_w++) {
      x = lyric_w + xoffset;
      if (x > i420buf->width()) {
        break;
      }
      buffer =
          lyric_w < played_length ? line->buffer_played : line->buffer_noplay;

      dataY[y * i420buf->StrideY() + x] =
          buffer->DataY()[lyric_h * buffer->StrideY() + lyric_w];
      dataU[y / 2 * i420buf->StrideU() + x / 2] =
          buffer->DataU()[lyric_h / 2 * buffer->StrideU() + lyric_w / 2];
      dataV[y / 2 * i420buf->StrideV() + x / 2] =
          buffer->DataV()[lyric_h / 2 * buffer->StrideV() + lyric_w / 2];
    }
  }
#endif

  auto copy_online_data = [](rtc::scoped_refptr<I420Buffer> dst, int dst_x,
                             int dst_y, rtc::scoped_refptr<I420Buffer> src,
                             int src_x, int src_y, int len) {
    if (len > (src->width() - src_x))
      len = (src->width() - src_x);

    if (len > (dst->width() - dst_x))
      len = (dst->width() - dst_x);

    if (len < 0)
      return len;

    memcpy(dst->MutableDataY() + dst_y * dst->StrideY() + dst_x,
           src->DataY() + src_y * src->StrideY() + src_x, len);

    int index_dst_uv = dst_y / 2 * dst->StrideU() + dst_x / 2;
    int index_src_uv = src_y / 2 * src->StrideU() + src_x / 2;
    int len_uv = len / 2;

    memcpy(dst->MutableDataU() + index_dst_uv, src->DataU() + index_src_uv,
           len_uv);
    memcpy(dst->MutableDataV() + index_dst_uv, src->DataV() + index_src_uv,
           len_uv);

    return len;
  };

  int y;
  int len;

  for (int lyric_h = 0; lyric_h < line->height; lyric_h++) {
    y = lyric_h + yoffset;
    if (y >= i420buf->height()) {
      break;
    }

    if ((len = copy_online_data(i420buffer, xoffset, y, line->buffer_played, 0,
                                lyric_h, played_length)) < 0) {
      continue;
    }
    copy_online_data(i420buffer, xoffset + len, y, line->buffer_noplay, len,
                     lyric_h, line->buffer_noplay->width() - len);
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

  if (_playline == NULL || curtime < _playline->_offset) {
    _played_length = 0;
    return;
  }

  curtime -= _playline->_offset;
  uint32_t played_length = 0;
  for (auto lineitr = _playline->_words.begin();
       lineitr != _playline->_words.end(); lineitr++) {
    //     uint32_t word_length = lineitr->width;
    //     for (auto glyphitr = lineitr->glyphs.begin();
    //          glyphitr != lineitr->glyphs.end(); glyphitr++) {
    //       FT_BitmapGlyph bitmap_glyph = (FT_BitmapGlyph)*glyphitr;
    // //       RTC_LOG(LS_ERROR) << lineitr->_word
    // //                         << " width:" << bitmap_glyph->bitmap.width;
    //       word_length += bitmap_glyph->bitmap.width;
    //       if (glyphitr != lineitr->glyphs.begin()) {
    //         played_length += _xspace;
    //       }
    //     }
    //
    //     if (lineitr != _playline->_words.begin()) {
    //       played_length += _xspace;
    //     }

    if (curtime > (lineitr->_offset + lineitr->_continue)) {
      played_length += lineitr->width;
    } else if (curtime > lineitr->_offset) {
      played_length += lineitr->width * ((curtime - lineitr->_offset) * 1.0 /
                                         lineitr->_continue);
      break;
    }
  }

  RTC_LOG(LS_ERROR) << "words:" << _playline->_text << " curtime:" << curtime
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

void LyricRender::SetOffset(int x, int y) {
  _xoffset = x / 2 * 2;
  _yoffset = y / 2 * 2;
}

void LyricRender::GetOffset(int& x, int& y) {
  x = _xoffset;
  y = _yoffset;
}

void LyricRender::SetPlayedColor(ColorSetting color) {
  _lyrc_prase.SetPlayedColor(color);
}

void LyricRender::SetNoplayColor(ColorSetting color) {
  _lyrc_prase.SetNoplayColor(color);
}

void LyricRender::SetFont(FontSetting font) {
  _lyrc_prase.SetFont(font);
}

ColorSetting LyricRender::GetPlayedColor() {
  return _lyrc_prase.GetPlayedColor();
}

ColorSetting LyricRender::GetNoplayColor() {
  return _lyrc_prase.GetNoplayColor();
}

FontSetting LyricRender::GetFont() {
  return _lyrc_prase.GetFont();
}

}  // namespace webrtc
