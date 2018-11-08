/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/desktop_capture/win/lyric_prase.h"
#include <list>
#include <string>
#include "rtc_base/logging.h"

namespace webrtc {

std::vector<std::string> split(std::string str, std::string pat) {
  std::vector<std::string> bufStr;
  while (true) {
    int index = str.find(pat);
    std::string subStr = str.substr(0, index);
    if (!subStr.empty())
      bufStr.push_back(subStr);

    str.erase(0, index + pat.size());

    if (index == -1)
      break;
  }
  return bufStr;
}

LyricWord::LyricWord() {}
LyricWord::LyricWord(const LyricWord& other) = default;
LyricWord::~LyricWord() {
  for (auto it = glyphs.begin(); it != glyphs.end(); it++) {
    if (*it != NULL) {
      FT_Done_Glyph(*it);
    }
  }
}

bool LyricWord::Prase(std::string& wordTime) {
  // 0,255,0>月
  int rIndex = 0;
  if ((rIndex = wordTime.find_first_of('>')) < 0) {
    return false;
  }

  std::vector<std::string> times = split(wordTime.substr(0, rIndex), ",");
  if (times.size() > 2) {
    _offset = stoll(times.at(0));
    _continue = stoll(times.at(1));
  }

  _word = wordTime.erase(0, rIndex + 1);
  return true;
}

std::string LyricWord::GetStream() {
  char text[56];
  sprintf(text, "<%llu,%llu,0>%s", _offset, _continue, _word.c_str());
  return text;
}

#define SPACE_WIDTH 10
void LyricWord::PraseBitmapGlyph(FT_Face& pFTFace) {
  for (auto it = glyphs.begin(); it != glyphs.end(); it++) {
    if (*it != NULL) {
      FT_Done_Glyph(*it);
    }
  }
  glyphs.clear();

  int wcsLen = MultiByteToWideChar(
      CP_ACP, NULL, (LPCSTR)_word.c_str(), _word.length(), NULL, 0);
  wchar_t* wszString = new wchar_t[wcsLen + 1];
  memset(wszString, 0, wcsLen * 2 + 2);
  MultiByteToWideChar(CP_ACP, 0, (LPCSTR)_word.c_str(), _word.length(),
                      wszString, wcsLen);
  WORD word;

  // for循环实现一个字一个字
  for (int k = 0; k < wcsLen; k++) {
    //复制内存块，把wszString中存储的文字一个一个取出来，复制到word中，已方便读取字体位图
    memcpy(&word, wszString + k, 2);
    //读取一个字体位图到face中
    FT_Glyph glyph;
    FT_Load_Glyph(pFTFace, FT_Get_Char_Index(pFTFace, word), FT_LOAD_DEFAULT);
    if (!FT_Get_Glyph(pFTFace->glyph, &glyph)) {
      //  convert glyph to bitmap with 256 gray
      FT_Glyph_To_Bitmap(&glyph, ft_render_mode_normal, 0, 0);
      FT_BitmapGlyph bitmap_glyph = (FT_BitmapGlyph)glyph;
      FT_Bitmap& bitmap = bitmap_glyph->bitmap;
      if (bitmap.width == 0) {
        bitmap.width = SPACE_WIDTH;
      }
      glyphs.push_back(glyph);
    }
  }

  free(wszString);
}
void LyricWord::CalcHeightOffset(int8_t line_height) {
  glyph_height_offsets.clear();
  for (auto glyph = glyphs.begin(); glyph != glyphs.end(); glyph++) {
    FT_BitmapGlyph bitmap_glyph = (FT_BitmapGlyph)*glyph;
    FT_Bitmap& bitmap = bitmap_glyph->bitmap;

    //每个字的高度不一样，已底线为准
    glyph_height_offsets.push_back(line_height - bitmap.rows);
  }
}

LyricLine::LyricLine() {}
LyricLine::LyricLine(const LyricLine& other) = default;
LyricLine::~LyricLine() {}
bool LyricLine::Prase(std::string& line) {
  _words.clear();
  //[11007,3033]<0,255,0>月<255,304,0>光<559,353,0>透<912,303,0>过<1215,252,0>雕<1467,202,0>花<1669,202,0>的<1871,1162,0>窗
  int lIndex = 0;
  int rIndex = 0;
  if ((lIndex = line.find_first_of('[')) < 0 ||
      (rIndex = line.find_first_of(']', lIndex)) < 0) {
    return false;
  }

  std::string lineTime = line.substr(lIndex + 1, rIndex - lIndex - 1);

  int mIndex = 0;
  if ((mIndex = line.find_first_of(',', lIndex)) <= 0) {
    return false;
  }

  _offset = stoll(lineTime.substr(lIndex, mIndex - lIndex - 1));
  _continue = stoll(lineTime.substr(mIndex, rIndex - mIndex - 1));

  line.erase(0, rIndex + 1);

  std::vector<std::string> words = split(line, "<");
  for (auto it = words.begin(); it != words.end(); it++) {
    LyricWord lyricWord;
    if (lyricWord.Prase(*it))
      _words.push_back(lyricWord);
  }
  return true;
}

std::string LyricLine::GetStream() {
  char text[56];
  sprintf(text, "[%llu,%llu]", _offset, _continue);
  std::string string = text;

  for (auto word : _words) {
    string += word.GetStream();
  }
  return string;
}

void LyricLine::PraseBitmapGlyph(FT_Face& pFTFace) {
  _hegiht = 0;
  for (auto it = _words.begin(); it != _words.end(); it++) {
    it->PraseBitmapGlyph(pFTFace);
    for (auto glply = it->glyphs.begin(); glply != it->glyphs.end(); glply++) {
      FT_BitmapGlyph bitmap_glyph = (FT_BitmapGlyph)*glply;
      FT_Bitmap& bitmap = bitmap_glyph->bitmap;
      _hegiht = (_hegiht > bitmap.rows ? _hegiht : bitmap.rows);
    }
  }

  for (auto it = _words.begin(); it != _words.end(); it++) {
    it->CalcHeightOffset(_hegiht);
  }
}

LyricPrase::LyricPrase() {
  if (FT_Init_FreeType(&pFTLib)) {
    pFTLib = NULL;
    RTC_LOG(LS_ERROR) << "Failed to Init FreeType";
    return;
  }
  //从字体文件创建face，simhei.ttf是黑体
  if (FT_New_Face(pFTLib, "C:/Windows/Fonts/simhei.ttf", 0, &pFTFace)) {
    pFTFace = NULL;
    RTC_LOG(LS_ERROR) << "Failed to open F://FreeSerif.ttf";
    return;
  }
  FT_Set_Char_Size(pFTFace, 0, 16 * 64, 300, 300);  //设置字体大小
  // FT_Set_Pixel_Sizes(pFTFace,0,16 );
}

LyricPrase::~LyricPrase() {
  if (pFTFace != NULL) {
    FT_Done_Face(pFTFace);
    pFTFace = NULL;
  }

  if (pFTLib != NULL) {
    //  free FreeType Lib
    FT_Done_FreeType(pFTLib);
    pFTLib = NULL;
  }
}

bool LyricPrase::Prase(const std::string& text) {
  if (pFTLib == NULL || pFTFace == NULL) {
    return false;
  }
  _lines.clear();
  std::vector<std::string> lines = split(text, "\r\n");
  if (lines.size() == 1) {
    lines.clear();
    lines = split(text, "\n");
  }

  for (auto it = lines.begin(); it != lines.end(); it++) {
    if (!PraseInfo(*it)) {
      LyricLine lyricLine;
      if (lyricLine.Prase(*it))
        _lines.push_back(lyricLine);
    }
  }

  PraseBitmapGlyph();

  return !_lines.empty();
}

void LyricPrase::Print() {
  RTC_LOG(LS_INFO) << "ar:" << _ar;
  RTC_LOG(LS_INFO) << "ti:" << _ti;
  RTC_LOG(LS_INFO) << "total:" << _total;
  RTC_LOG(LS_INFO) << "offset:" << _offset;
  for (auto line : _lines) {
    RTC_LOG(LS_INFO) << line.GetStream();
  }
}

bool LyricPrase::PraseInfo(std::string& line) {
  int lIndex = 0;
  int rIndex = 0;
  int mIndex = 0;

  if ((mIndex = line.find_first_of(':')) < 0 ||
      (lIndex = line.find_first_of('[')) < 0 ||
      (rIndex = line.find_first_of(']')) < 0) {
    return false;
  }

  std::string key = line.substr(lIndex + 1, mIndex - lIndex - 1);
  std::string value = line.substr(mIndex + 1, rIndex - mIndex - 1);

  if (key == "ar")
    _ar = value;
  else if (key == "ti")
    _ti = value;
  else if (key == "total")
    _total = stoll(value);
  else if (key == "offset")
    _offset = stoll(value);

  return true;
}

void LyricPrase::PraseBitmapGlyph() {
  for (auto it = _lines.begin(); it != _lines.end(); it++) {
    it->PraseBitmapGlyph(pFTFace);
  }
}

//   std::vector<std::vector<LyricLineData> > GetLines(int64_t time,
//                                                       int cnt) override {
//     std::vector<std::vector<LyricLineData> > lines;
//     auto line = _lines.begin();
//     for (auto it = _lines.begin(); it != _lines.end(); it++) {
//       if (it->_offset + it->_continue > time) {
//         line = it;
//         break;
//       }
//     }
//
//     if (line == _lines.end()) {
//       return lines;
//     }
//
// 	for (int i=0; i<cnt; i++)
// 	{
//       std::vector<LyricLineData> words;
//
//       LyricLineData data;
//
//       words.push_back(data);
//
//       lines.push_back(words);
//     }
//
//     return lines;
//   };

}  // namespace webrtc
