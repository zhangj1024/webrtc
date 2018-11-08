/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_DESKTOP_CAPTURE_WIN_LYRIC_PRASE_H_
#define MODULES_DESKTOP_CAPTURE_WIN_LYRIC_PRASE_H_

#include <map>
#include <string>
#include <vector>
#include "rtc_base/refcount.h"
#include "rtc_base/scoped_ref_ptr.h"
#include "third_party/freetype/src/include/ft2build.h"
#include FT_FREETYPE_H
#include FT_GLYPH_H

namespace webrtc {

class LyricBase {
 public:
  LyricBase(){};
  virtual ~LyricBase(){};

  virtual bool Prase(std::string& text) = 0;
  virtual std::string GetStream() = 0;
  virtual void PraseBitmapGlyph(FT_Face& pFTFace) = 0;

  uint64_t _offset = 0;
  uint64_t _continue = 0;
};

class LyricWord : public LyricBase {
 public:
  LyricWord();
  LyricWord(const LyricWord& other);
  ~LyricWord() override;

  bool Prase(std::string& wordTime) override;
  std::string GetStream() override;
  void PraseBitmapGlyph(FT_Face& pFTFace) override;
  void CalcHeightOffset(int8_t line_height);
  std::string _word;
  std::vector<FT_Glyph> glyphs;
  std::vector<int> glyph_height_offsets;
};

class LyricLine : public LyricBase {
 public:
  LyricLine();
  LyricLine(const LyricLine& other);
  ~LyricLine() override;

  bool Prase(std::string& line) override;
  std::string GetStream() override;
  void PraseBitmapGlyph(FT_Face& pFTFace) override;

  std::vector<LyricWord> _words;
  uint8_t _hegiht = 0;
};

class LyricPrase {
 public:
  LyricPrase();
  ~LyricPrase();

  bool Prase(const std::string& text);
  void Print();
  std::vector<LyricLine>& GetLines() { return _lines; };

  std::string _ar;
  std::string _ti;
  uint64_t _total = 0;
  uint64_t _offset = 0;
  std::vector<LyricLine> _lines;

 private:
  FT_Library pFTLib = NULL;
  FT_Face pFTFace = NULL;
  bool PraseInfo(std::string& line);
  void PraseBitmapGlyph();
};

}  // namespace webrtc

#endif  // MODULES_DESKTOP_CAPTURE_WIN_LYRIC_PRASE_H_
