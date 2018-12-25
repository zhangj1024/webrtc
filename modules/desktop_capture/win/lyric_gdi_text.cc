#include "lyric_gdi_text.h"
#include <rtc_base/logging.h>
#include <sys/stat.h>
#include <windows.h>
#include <algorithm>
#include <memory>
#include <vector>
#include "third_party/libyuv/include/libyuv/convert_from_argb.h"

#include <comdef.h>
#ifndef min
#define min
#endif
#ifndef max
#define max
#endif
#include <gdiplus.h>
using namespace Gdiplus;
#pragma comment(lib, "GdiPlus.lib")
using namespace std;

#define EPSILON 1e-4f
#define os_stat stat

#define warn_stat(call)                                     \
  do {                                                      \
    if (stat != Gdiplus::Ok)                                \
      RTC_LOG(LS_WARNING) << __FUNCDNAME__ << call << stat; \
  } while (false)

#ifndef clamp
#define clamp(val, min_val, max_val) \
  if (val < min_val)                 \
    val = min_val;                   \
  else if (val > max_val)            \
    val = max_val;
#endif

#define MIN_SIZE_CX 2
#define MIN_SIZE_CY 2
#define MAX_SIZE_CX 16384
#define MAX_SIZE_CY 16384

#define MAX_AREA (4096LL * 4096LL)

/* ------------------------------------------------------------------------- */
static inline DWORD get_alpha_val(uint32_t opacity) {
  return ((opacity * 255 / 100) & 0xFF) << 24;
}

static inline DWORD calc_color(uint32_t color, uint32_t opacity) {
  return (color & 0xFFFFFF) | get_alpha_val(opacity);
}

// static inline uint32_t rgb_to_bgr(uint32_t rgb) {
//   return ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb & 0xFF0000) >> 16);
// }

wstring string2wstring(string str) {
  wstring result;
  //获取缓冲区大小，并申请空间，缓冲区大小按字符计算
  int len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), str.size(), NULL, 0);
  TCHAR* buffer = new TCHAR[len + 1];
  //多字节编码转换成宽字节编码
  MultiByteToWideChar(CP_ACP, 0, str.c_str(), str.size(), buffer, len);
  buffer[len] = '\0';  //添加字符串结尾
  //删除缓冲区并返回值
  result.append(buffer);
  delete[] buffer;
  return result;
}

wstring ConvertToWstr(string text) {
  wstring wtext = string2wstring(text);
  if (!wtext.empty())
    wtext.push_back('\n');

  return wtext;
}

/* ------------------------------------------------------------------------- */

template <typename T, typename T2, BOOL WINAPI deleter(T2)>
class GDIObj {
  T obj = nullptr;

  inline GDIObj& Replace(T obj_) {
    if (obj)
      deleter(obj);
    obj = obj_;
    return *this;
  }

 public:
  inline GDIObj() {}
  inline GDIObj(T obj_) : obj(obj_) {}
  inline ~GDIObj() { deleter(obj); }

  inline T operator=(T obj_) {
    Replace(obj_);
    return obj;
  }

  inline operator T() const { return obj; }

  inline bool operator==(T obj_) const { return obj == obj_; }
  inline bool operator!=(T obj_) const { return obj != obj_; }
};

using HDCObj = GDIObj<HDC, HDC, DeleteDC>;
using HFONTObj = GDIObj<HFONT, HGDIOBJ, DeleteObject>;
using HBITMAPObj = GDIObj<HBITMAP, HGDIOBJ, DeleteObject>;

/* ------------------------------------------------------------------------- */

enum class Align { Left, Center, Right };

enum class VAlign { Top, Center, Bottom };

namespace webrtc {

class TextRenderImpl : public TextRender {
 public:
  TextRenderImpl()
      : hdc(CreateCompatibleDC(nullptr)),
        graphics(hdc),
        format(Gdiplus::StringFormat::GenericTypographic()),
        gdi_brush(Gdiplus::Color()),
        gdi_full_bk_color(Gdiplus::Color()) {
    UpdateFont();
    UpdateStringFormat();
    UpdateColor();
  };

  ~TextRenderImpl() override{};

  bool SetFont(FontSetting settings) override;
  bool SetColor(ColorSetting settings) override;
  bool RenderText(const wstring wtext,
                  std::shared_ptr<uint8_t[]>& rgb_data,
                  int& bitsize) override;
  void GetTextSize(int& height, int& width) override;
  void CalculateTextSizes(const std::wstring wtext,
                          int& height,
                          int& width) override;

 private:
  void UpdateFont();
  void UpdateStringFormat();
  void UpdateColor();
  void RemoveNewlinePadding(RectF& box);
  void CalculateTextSizes(RectF& bounding_box,
                          SIZE& text_size,
                          const wstring& text);

  /* --------------------------- */
  int cx = 0;
  int cy = 0;
  /* --------------------------- */

  HDCObj hdc;
  Graphics graphics;

  HFONTObj hfont;
  unique_ptr<Gdiplus::Font> font;

  Gdiplus::StringFormat format;

  wstring face;
  int face_size = 0;
  uint32_t color = 0xFF0000;
  uint32_t opacity = 100;

  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool strikeout = false;
  bool vertical = false;

  /* --------------------------- */

  uint32_t bk_color = 0;
  uint32_t bk_opacity = 0;
  Align align = Align::Left;
  VAlign valign = VAlign::Top;

  /* --------------------------- */
  Gdiplus::SolidBrush gdi_brush;
  Gdiplus::Color gdi_full_bk_color;
};

void TextRenderImpl::UpdateFont() {
  hfont = nullptr;
  font.reset(nullptr);

  LOGFONT lf = {};
  lf.lfHeight = face_size;
  lf.lfWeight = bold ? FW_BOLD : FW_DONTCARE;
  lf.lfItalic = italic;
  lf.lfUnderline = underline;
  lf.lfStrikeOut = strikeout;
  lf.lfQuality = ANTIALIASED_QUALITY;
  lf.lfCharSet = DEFAULT_CHARSET;

  if (!face.empty()) {
    wcscpy(lf.lfFaceName, face.c_str());
    hfont = CreateFontIndirect(&lf);
  }

  if (!hfont) {
    wcscpy(lf.lfFaceName, L"Arial");
    hfont = CreateFontIndirect(&lf);
  }

  if (hfont)
    font.reset(new Gdiplus::Font(hdc, hfont));
}

void TextRenderImpl::UpdateStringFormat() {
  UINT flags =
      StringFormatFlagsNoFitBlackBox | StringFormatFlagsMeasureTrailingSpaces;

  if (vertical)
    flags |= StringFormatFlagsDirectionVertical |
             StringFormatFlagsDirectionRightToLeft;

  format.SetFormatFlags(flags);
  format.SetTrimming(StringTrimmingWord);

  switch (align) {
    case Align::Left:
      if (vertical)
        format.SetLineAlignment(StringAlignmentFar);
      else
        format.SetAlignment(StringAlignmentNear);
      break;
    case Align::Center:
      if (vertical)
        format.SetLineAlignment(StringAlignmentCenter);
      else
        format.SetAlignment(StringAlignmentCenter);
      break;
    case Align::Right:
      if (vertical)
        format.SetLineAlignment(StringAlignmentNear);
      else
        format.SetAlignment(StringAlignmentFar);
  }

  switch (valign) {
    case VAlign::Top:
      if (vertical)
        format.SetAlignment(StringAlignmentNear);
      else
        format.SetLineAlignment(StringAlignmentNear);
      break;
    case VAlign::Center:
      if (vertical)
        format.SetAlignment(StringAlignmentCenter);
      else
        format.SetLineAlignment(StringAlignmentCenter);
      break;
    case VAlign::Bottom:
      if (vertical)
        format.SetAlignment(StringAlignmentFar);
      else
        format.SetLineAlignment(StringAlignmentFar);
  }
}

void TextRenderImpl::UpdateColor() {
  gdi_brush.SetColor(Gdiplus::Color(calc_color(color, opacity)));
  gdi_full_bk_color = Gdiplus::Color(calc_color(bk_color, bk_opacity));
}

/* GDI+ treats '\n' as an extra character with an actual render size when
 * calculating the texture size, so we have to calculate the size of '\n' to
 * remove the padding.  Because we always add a newline to the string, we
 * also remove the extra unused newline. */
void TextRenderImpl::RemoveNewlinePadding(RectF& box) {
  RectF before;
  RectF after;
  Status stat;

  stat = graphics.MeasureString(L"W", 2, font.get(), PointF(0.0f, 0.0f),
                                &format, &before);
  warn_stat("MeasureString (without newline)");

  stat = graphics.MeasureString(L"W\n", 3, font.get(), PointF(0.0f, 0.0f),
                                &format, &after);
  warn_stat("MeasureString (with newline)");

  float offset_cx = after.Width - before.Width;
  float offset_cy = after.Height - before.Height;

  if (!vertical) {
    if (offset_cx >= 1.0f)
      offset_cx -= 1.0f;

    if (valign == VAlign::Center)
      box.Y -= offset_cy * 0.5f;
    else if (valign == VAlign::Bottom)
      box.Y -= offset_cy;
  } else {
    if (offset_cy >= 1.0f)
      offset_cy -= 1.0f;

    if (align == Align::Center)
      box.X -= offset_cx * 0.5f;
    else if (align == Align::Right)
      box.X -= offset_cx;
  }

  box.Width -= offset_cx;
  box.Height -= offset_cy;
}

void TextRenderImpl::CalculateTextSizes(RectF& bounding_box,
                                        SIZE& text_size,
                                        const wstring& text) {
  RectF layout_box;
  RectF temp_box;
  Status stat;

  if (!text.empty()) {
    stat =
        graphics.MeasureString(text.c_str(), (int)text.size() + 1, font.get(),
                               PointF(0.0f, 0.0f), &format, &bounding_box);
    warn_stat("MeasureString (non-wrapped)");

    temp_box = bounding_box;

    bounding_box.X = 0.0f;
    bounding_box.Y = 0.0f;

    RemoveNewlinePadding(bounding_box);
  }

  if (vertical) {
    if (bounding_box.Width < face_size) {
      text_size.cx = face_size;
      bounding_box.Width = float(face_size);
    } else {
      text_size.cx = LONG(bounding_box.Width + EPSILON);
    }

    text_size.cy = LONG(bounding_box.Height + EPSILON);
  } else {
    if (bounding_box.Height < face_size) {
      text_size.cy = face_size;
      bounding_box.Height = float(face_size);
    } else {
      text_size.cy = LONG(bounding_box.Height + EPSILON);
    }

    text_size.cx = LONG(bounding_box.Width + EPSILON);
  }

  text_size.cx += text_size.cx % 2;
  text_size.cy += text_size.cy % 2;

  int64_t total_size = int64_t(text_size.cx) * int64_t(text_size.cy);

  /* GPUs typically have texture size limitations */
  clamp(text_size.cx, MIN_SIZE_CX, MAX_SIZE_CX);
  clamp(text_size.cy, MIN_SIZE_CY, MAX_SIZE_CY);

  /* avoid taking up too much VRAM */
  if (total_size > MAX_AREA) {
    if (text_size.cx > text_size.cy)
      text_size.cx = (LONG)MAX_AREA / text_size.cy;
    else
      text_size.cy = (LONG)MAX_AREA / text_size.cx;
  }

  /* the internal text-rendering bounding box for is reset to
   * its internal value in case the texture gets cut off */
  bounding_box.Width = temp_box.Width;
  bounding_box.Height = temp_box.Height;
}

bool TextRenderImpl::RenderText(const wstring wtext,
                                std::shared_ptr<uint8_t[]>& rgb_data,
                                int& bitsize) {
  Status stat;
  RectF bounding_box;
  SIZE text_size;

  CalculateTextSizes(bounding_box, text_size, wtext);

  if (bitsize != text_size.cx * text_size.cy * 4) {
    bitsize = text_size.cx * text_size.cy * 4;
    rgb_data.reset(new uint8_t[bitsize]);
  }

  Bitmap bitmap(text_size.cx, text_size.cy, 4 * text_size.cx,
                PixelFormat32bppARGB, rgb_data.get());

  Graphics graphics_bitmap(&bitmap);

  if (text_size.cx > bounding_box.Width || text_size.cy > bounding_box.Height) {
    stat = graphics_bitmap.Clear(Gdiplus::Color(0));
    warn_stat("graphics_bitmap.Clear");

    Gdiplus::SolidBrush bk_brush(gdi_full_bk_color);
    stat = graphics_bitmap.FillRectangle(&bk_brush, bounding_box);
    warn_stat("graphics_bitmap.FillRectangle");
  } else {
    stat = graphics_bitmap.Clear(gdi_full_bk_color);
    warn_stat("graphics_bitmap.Clear");
  }

  graphics_bitmap.SetTextRenderingHint(TextRenderingHintAntiAlias);
  graphics_bitmap.SetCompositingMode(CompositingModeSourceOver);
  graphics_bitmap.SetSmoothingMode(SmoothingModeAntiAlias);

  if (!wtext.empty()) {
    stat =
        graphics_bitmap.DrawString(wtext.c_str(), (int)wtext.size(), font.get(),
                                   bounding_box, &format, &gdi_brush);
    warn_stat("graphics_bitmap.DrawString");
  }

  cx = (uint32_t)text_size.cx;
  cy = (uint32_t)text_size.cy;

  return true;
}

void TextRenderImpl::GetTextSize(int& height, int& width) {
  height = cy;
  width = cx;
}

void TextRenderImpl::CalculateTextSizes(const std::wstring wtext,
                                        int& height,
                                        int& width) {
  RectF box;
  SIZE size;

  CalculateTextSizes(box, size, wtext);

  height = (int)size.cy;
  width = (int)size.cx;
}

inline bool TextRenderImpl::SetFont(FontSetting s) {
  std::wstring new_face = string2wstring(s.face);
  if (new_face != face || s.face_size != face_size || s.bold != bold ||
      s.italic != italic || s.underline != underline ||
      s.strikeout != strikeout) {
    face = new_face;
    face_size = s.face_size;
    bold = s.bold;
    italic = s.italic;
    underline = s.underline;
    strikeout = s.strikeout;

    UpdateFont();
    return true;
  }
  return false;
}

bool TextRenderImpl::SetColor(ColorSetting s) {
  //   uint32_t new_color = rgb_to_bgr(s.color);
  if (s.color != color || s.opacity != opacity) {
    color = s.color;
    opacity = s.opacity;
    UpdateColor();
    return true;
  }
  return false;
}

TextRender* TextRender::Create() {
  return new TextRenderImpl();
}

/* ------------------------------------------------------------------------- */

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

LyricWord::~LyricWord(){};

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

  _text = wordTime.erase(0, rIndex + 1);
  return true;
}

std::string LyricWord::GetStream() {
  char text[56];
  sprintf(text, "<%llu,%llu,0>%s", _offset, _continue, _text.c_str());
  return text;
}

#define SPACE_WIDTH 10
void LyricWord::PraseBitmap(std::shared_ptr<TextRender> render, bool played) {
  wstring text = ConvertToWstr(_text);
  render->CalculateTextSizes(text, height, width);
}

LyricLine::LyricLine() = default;
LyricLine::LyricLine(const LyricLine& other) = default;
LyricLine::~LyricLine(){};

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
  _text.clear();
  for (auto it = _words.begin(); it != _words.end(); it++) {
    _text += it->_text;
  }

  return true;
}

std::string LyricLine::GetStream() {
  char text[56];
  sprintf(text, "[%llu,%llu]", _offset, _continue);
  std::string string = text;

  for (auto word = _words.begin(); word != _words.end(); word++) {
    string += word->GetStream();
  }

  return string;
}

void LyricLine::PraseBitmap(std::shared_ptr<TextRender> render, bool played) {
  wstring text = ConvertToWstr(_text);

  rtc::scoped_refptr<I420Buffer> &buffer = played ? buffer_played : buffer_noplay;

  render->RenderText(text, rgb_data_, rgb_data_size_);

  render->GetTextSize(height, width);

  if (!buffer || buffer->height() != height || buffer->width() != width) {
    buffer = I420Buffer::Create(width, height);
  }

  if (width > 0 && height > 0) {
    libyuv::ARGBToI420(rgb_data_.get(), width * 4, buffer->MutableDataY(),
                       buffer->StrideY(), buffer->MutableDataU(),
                       buffer->StrideU(), buffer->MutableDataV(),
                       buffer->StrideV(), width, height);
  }

  for (auto it = _words.begin(); it != _words.end(); it++) {
    it->PraseBitmap(render, played);
  }

#if 1
  uint8_t* data = rgb_data_.get();
  for (int h = 0; h < height; h++) {
    std::string line;
    for (int w = 0; w < width; w++) {
      uint64_t index = (h * width + w) * 4;
      bool show = data[index] != 0 || data[index + 1] != 0 ||
                  data[index + 2] != 0 || data[index + 3] != 0;
      line += show ? "#" : "*";
    }
    RTC_LOG(LS_INFO) << line;
  }
#endif
}

LyricPrase::LyricPrase() : _render(TextRender::Create()) {
  _playedColor.color = 0x00FF00;
  _notPlayColor.color = 0xFFFF00;
  _font.face_size = 36;
}
LyricPrase::~LyricPrase() {}

bool LyricPrase::Prase(const std::string& text) {
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

  _render->SetFont(_font);
  for (auto it = _lines.begin(); it != _lines.end(); it++) {
    _render->SetColor(_notPlayColor);
    it->PraseBitmap(_render, false);
    _render->SetColor(_playedColor);
    it->PraseBitmap(_render, true);
  }

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

#define SETRENDER_BEGIN(newvalue, oldvalue, methon) \
  oldvalue = newvalue;                              \
  if (!_render || !_render->methon(oldvalue))       \
    return;                                         \
  for (auto it = _lines.begin(); it != _lines.end(); it++)

void LyricPrase::SetPlayedColor(ColorSetting color) {
  SETRENDER_BEGIN(color, _playedColor, SetColor) {
    it->PraseBitmap(_render, true);
  }
}

void LyricPrase::SetNoplayColor(ColorSetting color) {
  SETRENDER_BEGIN(color, _notPlayColor, SetColor) {
    it->PraseBitmap(_render, false);
  }
}

void LyricPrase::SetFont(FontSetting font) {
  SETRENDER_BEGIN(font, _font, SetFont) {
    //   if (font == _font)
//     return;
//   _font = font;
//   if (!_render)
//     return;
//   _render->SetFont(_font);
//   for (auto it = _lines.begin(); it != _lines.end(); it++) {
    _render->SetColor(_notPlayColor);
    it->PraseBitmap(_render, false);
    _render->SetColor(_playedColor);
    it->PraseBitmap(_render, true);
  }
}

static ULONG_PTR gdip_token = 0;
void InitGdi() {
  if (gdip_token == 0) {
    const GdiplusStartupInput gdip_input;
    GdiplusStartup(&gdip_token, &gdip_input, nullptr);
  }
}

void UnInitGdi() {
  GdiplusShutdown(gdip_token);
  gdip_token = 0;
}

}  // namespace webrtc
