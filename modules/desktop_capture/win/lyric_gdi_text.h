#ifndef MODULES_DESKTOP_CAPTURE_WIN_LYRIC_GDI_TEXT_H_
#define MODULES_DESKTOP_CAPTURE_WIN_LYRIC_GDI_TEXT_H_
#include <string>
#include <vector>
#include "api/video/i420_buffer.h"
#include "lyric_render.h"
#include "rtc_base/scoped_ref_ptr.h"

namespace webrtc {

class TextRender {
 public:
  virtual ~TextRender(){};
  virtual bool SetFont(FontSetting settings) = 0;
  virtual bool SetColor(ColorSetting settings) = 0;
  // return ARGB
  virtual bool RenderText(const std::wstring wtext,
                          std::shared_ptr<uint8_t[]>& rgb_data,
                          int& bitsize) = 0;
  virtual void GetTextSize(int& height, int& width) = 0;
  virtual void CalculateTextSizes(const std::wstring wtext,
                                  int& height,
                                  int& width) = 0;

  static TextRender* Create();
};

class LyricBase {
 public:
  virtual ~LyricBase() {}

  virtual bool Prase(std::string& text) = 0;
  virtual std::string GetStream() = 0;
  virtual void PraseBitmap(std::shared_ptr<TextRender> render, bool played) = 0;

  uint64_t _offset = 0;
  uint64_t _continue = 0;

  std::string _text;
  int height = 0;
  int width = 0;
};

class LyricWord : public LyricBase {
 public:
  ~LyricWord() override;

  bool Prase(std::string& wordTime) override;
  std::string GetStream() override;
  void PraseBitmap(std::shared_ptr<TextRender> render, bool played) override;
};

class LyricLine : public LyricBase {
 public:
  LyricLine();
  LyricLine(const LyricLine& other);
  ~LyricLine() override;

  bool Prase(std::string& line) override;
  std::string GetStream() override;
  void PraseBitmap(std::shared_ptr<TextRender> render, bool played) override;

  std::vector<LyricWord> _words;

  std::shared_ptr<uint8_t[]> rgb_data_;
  int rgb_data_size_ = 0;
  rtc::scoped_refptr<I420Buffer> buffer_noplay;
  rtc::scoped_refptr<I420Buffer> buffer_played;
};

class LyricPrase {
 public:
  LyricPrase();
  ~LyricPrase();

  bool Prase(const std::string& text);
  void Print();
  std::vector<LyricLine>& GetLines() { return _lines; };

  void SetPlayedColor(ColorSetting color);
  void SetNoplayColor(ColorSetting color);
  void SetFont(FontSetting font);

  inline ColorSetting GetPlayedColor() { return _playedColor; };
  inline ColorSetting GetNoplayColor() { return _notPlayColor; };
  inline FontSetting GetFont() { return _font; };

  std::string _ar;
  std::string _ti;
  uint64_t _total = 0;
  uint64_t _offset = 0;
  std::vector<LyricLine> _lines;

 private:
  std::shared_ptr<TextRender> _render;
  ColorSetting _notPlayColor;
  ColorSetting _playedColor;
  FontSetting _font;
  bool PraseInfo(std::string& line);
};

void InitGdi();
void UnInitGdi();

}  // namespace webrtc

#endif  // MODULES_DESKTOP_CAPTURE_WIN_LYRIC_GDI_TEXT_H_
