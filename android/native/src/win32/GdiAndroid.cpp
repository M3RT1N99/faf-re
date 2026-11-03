// The GDI font functions of include/win32/Windows.h, on FreeType (GdiFonts.h): what the font
// renderer (moho/render/d3d/CD3DFont.cpp) calls, which stays as it is. The game registers its own
// fonts (/fonts/*.ttf, CD3DDevice's REN_Init) with AddFontMemResourceEx; CD3DFont creates a font
// with CreateFontA, selects it and a 128 x 128 32-bit bitmap into a memory DC, reads the font's
// metrics and kerning pairs, and for each new character measures it with DrawTextW (DT_CALCRECT),
// draws it white on black with its baseline on the rectangle's top (TA_BASELINE) and reads the
// bitmap back (GetBitmapBits, and GetPixel to check it once).
//
// The handles are objects of this file: fonts, bitmaps, brushes and memory DCs, in one table under
// one lock, so a handle that is gone is refused, as GDI refuses it. A DC keeps what is selected
// into it, so deleting a selected object leaves the DC working (CD3DFont deletes its font and
// bitmap while they are selected). CreateFontA never fails for a face name: as GDI it gives the
// closest face it has, and when the game registered none, Android's own sans-serif font.
//
// Where CD3DFont asks for less than GDI can do, this does what it asks and says no to the rest:
// bitmaps have 32 bits per pixel, DrawTextW draws one line from the rectangle's corner without prefix
// characters, and the text aligns only vertically.

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "GdiFonts.h"
#include "gpg/core/utils/Logging.h"

namespace
{
  namespace gdi = faf::android::gdi;

  constexpr COLORREF kInvalidColour = 0xFFFFFFFFu; // CLR_INVALID
  constexpr UINT kGdiError = 0xFFFFFFFFu;          // GDI_ERROR

  // DrawText's format flags (winuser.h) that CD3DFont passes: DT_SINGLELINE and DT_NOPREFIX always.
  constexpr UINT kDtSingleLine = 0x0020u;
  constexpr UINT kDtNoClip = 0x0100u;
  constexpr UINT kDtCalcRect = 0x0400u;
  constexpr UINT kDtNoPrefix = 0x0800u;

  // The text alignment (wingdi.h): TA_LEFT, TA_TOP and TA_NOUPDATECP are 0.
  constexpr UINT kTaVertical = 0x0018u;
  constexpr UINT kTaBottom = 0x0008u;
  constexpr UINT kTaBaseline = 0x0018u;

  // A bitmap's largest side, to keep its size within memory.
  constexpr int kLargestBitmap = 0x4000;

  // The stock brushes GetStockObject gives (WHITE_BRUSH to NULL_BRUSH).
  constexpr int kStockBrushCount = 6;
  constexpr COLORREF kStockBrushColours[kStockBrushCount] = {
    RGB(255, 255, 255), RGB(192, 192, 192), RGB(128, 128, 128), RGB(64, 64, 64), RGB(0, 0, 0), RGB(0, 0, 0)};
  constexpr int kNullBrush = 5;

  // Android's sans-serif fonts, for a game without fonts of its own.
  constexpr const char* kSystemFonts[] = {
    "/system/fonts/Roboto-Regular.ttf", "/system/fonts/NotoSans-Regular.ttf", "/system/fonts/DroidSans.ttf"};

  enum class Kind
  {
    Font,
    Bitmap,
    Brush,
    Dc
  };

  struct Object
  {
    explicit Object(const Kind objectKind) noexcept : kind(objectKind) {}
    virtual ~Object() = default;
    const Kind kind;
    bool stock = false;
  };

  struct FontObject final : Object
  {
    FontObject() noexcept : Object(Kind::Font) {}
    gdi::LogicalFont logical;
    bool realizeTried = false;
    std::shared_ptr<gdi::Font> realized;
  };

  /// A device-dependent bitmap as CreateBitmap makes one: rows of `width` pixels, the top row first,
  /// each pixel blue, green, red and a byte GDI leaves 0. A bitmap without pixels is a new DC's.
  struct BitmapObject final : Object
  {
    BitmapObject() noexcept : Object(Kind::Bitmap) {}
    LONG width = 0;
    LONG height = 0;
    std::vector<std::uint8_t> bits;
  };

  struct BrushObject final : Object
  {
    BrushObject() noexcept : Object(Kind::Brush) {}
    COLORREF colour = 0;
    bool hollow = false;
  };

  struct DcObject final : Object
  {
    DcObject() noexcept : Object(Kind::Dc) {}
    std::shared_ptr<BitmapObject> bitmap;
    std::shared_ptr<FontObject> font;
    std::shared_ptr<BrushObject> brush;
    COLORREF textColour = RGB(0, 0, 0);
    COLORREF backColour = RGB(255, 255, 255);
    UINT textAlign = 0;
  };

  /// The handles, the stock objects, the game's fonts. Never destroyed: CD3DFont deletes its
  /// objects from an atexit handler.
  struct Gdi
  {
    std::recursive_mutex mutex; // recursive: a log target may draw text while a font logs its choice
    std::unordered_map<const void*, std::shared_ptr<Object>> objects;
    std::shared_ptr<BrushObject> stockBrushes[kStockBrushCount];
    std::shared_ptr<BitmapObject> stockBitmap; // a new DC's bitmap, where nothing can be drawn
    std::shared_ptr<FontObject> stockFont;     // a new DC's font, SYSTEM_FONT
    gdi::FontRegistry fonts;
    bool systemFontsTried = false;

    Gdi()
    {
      for (int i = 0; i < kStockBrushCount; ++i) {
        auto brush = std::make_shared<BrushObject>();
        brush->colour = kStockBrushColours[i];
        brush->hollow = (i == kNullBrush);
        stockBrushes[i] = Add(std::move(brush), true);
      }
      stockBitmap = Add(std::make_shared<BitmapObject>(), true);
      auto font = std::make_shared<FontObject>();
      font->logical.height = 16;
      font->logical.faceName = "System";
      stockFont = Add(std::move(font), true);
    }

    template <typename T>
    std::shared_ptr<T> Add(std::shared_ptr<T> object, const bool stock = false)
    {
      object->stock = stock;
      objects.emplace(object.get(), object);
      return object;
    }

    template <typename T>
    [[nodiscard]] std::shared_ptr<T> Find(const void* const handle, const Kind kind) const
    {
      const auto found = objects.find(handle);
      if (found == objects.end() || found->second->kind != kind) {
        return nullptr;
      }
      return std::static_pointer_cast<T>(found->second);
    }

    /// The face GDI chooses for the font, chosen once; Android's own font when the game registered
    /// none.
    [[nodiscard]] gdi::Font* Realize(FontObject& font)
    {
      if (!font.realizeTried) {
        font.realizeTried = true;
        if (fonts.Empty()) {
          AddSystemFonts();
        }
        font.realized = fonts.Realize(font.logical);
        if (font.realized != nullptr) {
          gpg::Logf(
            "GDI: font \"%s\" %d: %s, %d pixels per em, %s", font.logical.faceName.c_str(), static_cast<int>(font.logical.height),
            font.realized->FaceName().c_str(), static_cast<int>(font.realized->PixelsPerEm()),
            font.realized->Antialiased() ? "grey" : "bi-level"
          );
        } else {
          gpg::Warnf("GDI: no font for \"%s\" %d", font.logical.faceName.c_str(), static_cast<int>(font.logical.height));
        }
      }
      return font.realized.get();
    }

    void AddSystemFonts()
    {
      if (systemFontsTried) {
        return;
      }
      systemFontsTried = true;
      for (const char* const path : kSystemFonts) {
        std::ifstream file(path, std::ios::binary);
        const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        if (fonts.Add(bytes.data(), bytes.size(), path) > 0) {
          gpg::Warnf("GDI: the game registered no font; using %s", path);
          return;
        }
      }
    }
  };

  Gdi& State()
  {
    static Gdi* const state = new Gdi;
    return *state;
  }

  /// The bitmap selected into a DC, as the font engine draws into it.
  [[nodiscard]] gdi::Surface SurfaceOf(const DcObject& dc)
  {
    return gdi::Surface{dc.bitmap->bits.data(), dc.bitmap->width, dc.bitmap->height, dc.bitmap->width * 4};
  }
} // namespace

HFONT CreateFontA(
  const int height, const int width, const int escapement, const int orientation, const int weight, const DWORD italic,
  const DWORD underline, const DWORD strikeOut, const DWORD charSet, const DWORD outPrecision, const DWORD clipPrecision,
  const DWORD quality, const DWORD pitchAndFamily, const char* const faceName
)
{
  (void)width;
  (void)escapement;
  (void)orientation;
  (void)outPrecision;
  (void)clipPrecision;
  (void)pitchAndFamily;
  Gdi& state = State();
  const std::lock_guard<std::recursive_mutex> lock(state.mutex);
  auto font = std::make_shared<FontObject>();
  font->logical.height = height;
  font->logical.weight = weight;
  font->logical.italic = italic != 0;
  font->logical.underline = underline != 0;
  font->logical.strikeOut = strikeOut != 0;
  font->logical.charSet = static_cast<std::uint8_t>(charSet);
  font->logical.quality = static_cast<std::uint8_t>(quality);
  font->logical.faceName = (faceName != nullptr) ? faceName : "";
  return state.Add(std::move(font)).get();
}

BOOL DeleteObject(HGDIOBJ const object)
{
  Gdi& state = State();
  const std::lock_guard<std::recursive_mutex> lock(state.mutex);
  const auto found = state.objects.find(object);
  if (found == state.objects.end()) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  // Stock objects stay; the call succeeds, as GDI's does. A DC is deleted too, though GDI deletes
  // one with DeleteDC: CD3DFont gives its DC to DeleteObject, and nothing else would free it.
  if (!found->second->stock) {
    state.objects.erase(found);
  }
  return TRUE;
}

HDC CreateCompatibleDC(HDC)
{
  Gdi& state = State();
  const std::lock_guard<std::recursive_mutex> lock(state.mutex);
  auto dc = std::make_shared<DcObject>();
  dc->bitmap = state.stockBitmap;
  dc->font = state.stockFont;
  dc->brush = state.stockBrushes[0];
  return state.Add(std::move(dc)).get();
}

HBITMAP CreateBitmap(const int width, const int height, const UINT planes, const UINT bitsPerPixel, const void* const bits)
{
  if (width <= 0 || height <= 0 || width > kLargestBitmap || height > kLargestBitmap || planes != 1 || bitsPerPixel != 32) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  auto bitmap = std::make_shared<BitmapObject>();
  bitmap->width = width;
  bitmap->height = height;
  bitmap->bits.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
  if (bits != nullptr) {
    std::memcpy(bitmap->bits.data(), bits, bitmap->bits.size());
  }
  Gdi& state = State();
  const std::lock_guard<std::recursive_mutex> lock(state.mutex);
  return state.Add(std::move(bitmap)).get();
}

HGDIOBJ SelectObject(HDC const dc, HGDIOBJ const object)
{
  Gdi& state = State();
  const std::lock_guard<std::recursive_mutex> lock(state.mutex);
  const auto target = state.Find<DcObject>(dc, Kind::Dc);
  const auto found = state.objects.find(object);
  if (target == nullptr || found == state.objects.end()) {
    ::SetLastError(ERROR_INVALID_HANDLE);
    return nullptr;
  }
  HGDIOBJ previous = nullptr;
  switch (found->second->kind) {
    case Kind::Bitmap:
      previous = target->bitmap.get();
      target->bitmap = std::static_pointer_cast<BitmapObject>(found->second);
      break;
    case Kind::Font:
      previous = target->font.get();
      target->font = std::static_pointer_cast<FontObject>(found->second);
      break;
    case Kind::Brush:
      previous = target->brush.get();
      target->brush = std::static_pointer_cast<BrushObject>(found->second);
      break;
    case Kind::Dc:
      ::SetLastError(ERROR_INVALID_HANDLE);
      return nullptr;
  }
  return previous;
}

BOOL GetTextMetricsW(HDC const dc, TEXTMETRICW* const metrics)
{
  if (metrics == nullptr) {
    return FALSE;
  }
  *metrics = TEXTMETRICW{};
  Gdi& state = State();
  const std::lock_guard<std::recursive_mutex> lock(state.mutex);
  const auto target = state.Find<DcObject>(dc, Kind::Dc);
  gdi::Font* const font = (target != nullptr) ? state.Realize(*target->font) : nullptr;
  if (font == nullptr) {
    return FALSE;
  }
  const gdi::TextMetrics& m = font->Metrics();
  metrics->tmHeight = m.height;
  metrics->tmAscent = m.ascent;
  metrics->tmDescent = m.descent;
  metrics->tmInternalLeading = m.internalLeading;
  metrics->tmExternalLeading = m.externalLeading;
  metrics->tmAveCharWidth = m.aveCharWidth;
  metrics->tmMaxCharWidth = m.maxCharWidth;
  metrics->tmWeight = m.weight;
  metrics->tmOverhang = m.overhang;
  metrics->tmDigitizedAspectX = m.digitizedAspectX;
  metrics->tmDigitizedAspectY = m.digitizedAspectY;
  metrics->tmFirstChar = m.firstChar;
  metrics->tmLastChar = m.lastChar;
  metrics->tmDefaultChar = m.defaultChar;
  metrics->tmBreakChar = m.breakChar;
  metrics->tmItalic = m.italic;
  metrics->tmUnderlined = m.underlined;
  metrics->tmStruckOut = m.struckOut;
  metrics->tmPitchAndFamily = m.pitchAndFamily;
  metrics->tmCharSet = m.charSet;
  return TRUE;
}

DWORD GetKerningPairsW(HDC const dc, const DWORD pairCount, KERNINGPAIR* const pairs)
{
  Gdi& state = State();
  const std::lock_guard<std::recursive_mutex> lock(state.mutex);
  const auto target = state.Find<DcObject>(dc, Kind::Dc);
  gdi::Font* const font = (target != nullptr) ? state.Realize(*target->font) : nullptr;
  if (font == nullptr) {
    return 0;
  }
  const std::vector<gdi::KerningPair> all = font->KerningPairs();
  if (pairs == nullptr || pairCount == 0) {
    return static_cast<DWORD>(all.size());
  }
  const std::size_t count = std::min<std::size_t>(pairCount, all.size());
  for (std::size_t i = 0; i < count; ++i) {
    pairs[i].wFirst = all[i].first;
    pairs[i].wSecond = all[i].second;
    pairs[i].iKernAmount = all[i].amount;
  }
  return static_cast<DWORD>(count);
}

int GetObjectW(HGDIOBJ const object, const int size, void* const out)
{
  Gdi& state = State();
  const std::lock_guard<std::recursive_mutex> lock(state.mutex);
  const auto bitmap = state.Find<BitmapObject>(object, Kind::Bitmap);
  if (bitmap == nullptr) {
    // Fonts and brushes would give a LOGFONT or a LOGBRUSH, which nothing here asks for.
    return 0;
  }
  if (out == nullptr) {
    return static_cast<int>(sizeof(BITMAP));
  }
  if (size < static_cast<int>(sizeof(BITMAP))) {
    return 0;
  }
  BITMAP info{};
  info.bmType = 0;
  info.bmWidth = bitmap->width;
  info.bmHeight = bitmap->height;
  info.bmWidthBytes = bitmap->width * 4;
  info.bmPlanes = 1;
  info.bmBitsPixel = 32;
  info.bmBits = nullptr; // a device-dependent bitmap, not a DIB section
  std::memcpy(out, &info, sizeof(info));
  return static_cast<int>(sizeof(BITMAP));
}

LONG GetBitmapBits(HBITMAP const bitmap, const LONG size, void* const bits)
{
  Gdi& state = State();
  const std::lock_guard<std::recursive_mutex> lock(state.mutex);
  const auto found = state.Find<BitmapObject>(bitmap, Kind::Bitmap);
  if (found == nullptr || found->bits.empty() || bits == nullptr || size <= 0) {
    return 0;
  }
  const std::size_t count = std::min<std::size_t>(static_cast<std::size_t>(size), found->bits.size());
  std::memcpy(bits, found->bits.data(), count);
  return static_cast<LONG>(count);
}

BOOL GdiFlush()
{
  // Everything is drawn when the call returns: nothing is batched.
  return TRUE;
}

UINT SetTextAlign(HDC const dc, const UINT align)
{
  Gdi& state = State();
  const std::lock_guard<std::recursive_mutex> lock(state.mutex);
  const auto target = state.Find<DcObject>(dc, Kind::Dc);
  if (target == nullptr || (align & ~kTaVertical) != 0) {
    return kGdiError;
  }
  const UINT previous = target->textAlign;
  target->textAlign = align;
  return previous;
}

COLORREF SetBkColor(HDC const dc, const COLORREF color)
{
  Gdi& state = State();
  const std::lock_guard<std::recursive_mutex> lock(state.mutex);
  const auto target = state.Find<DcObject>(dc, Kind::Dc);
  if (target == nullptr) {
    return kInvalidColour;
  }
  const COLORREF previous = target->backColour;
  target->backColour = color & 0x00FFFFFFu;
  return previous;
}

COLORREF SetTextColor(HDC const dc, const COLORREF color)
{
  Gdi& state = State();
  const std::lock_guard<std::recursive_mutex> lock(state.mutex);
  const auto target = state.Find<DcObject>(dc, Kind::Dc);
  if (target == nullptr) {
    return kInvalidColour;
  }
  const COLORREF previous = target->textColour;
  target->textColour = color & 0x00FFFFFFu;
  return previous;
}

COLORREF GetPixel(HDC const dc, const int x, const int y)
{
  Gdi& state = State();
  const std::lock_guard<std::recursive_mutex> lock(state.mutex);
  const auto target = state.Find<DcObject>(dc, Kind::Dc);
  if (target == nullptr || x < 0 || y < 0 || x >= target->bitmap->width || y >= target->bitmap->height) {
    return kInvalidColour;
  }
  const std::uint8_t* const pixel = target->bitmap->bits.data() + (static_cast<std::size_t>(y) * target->bitmap->width + x) * 4;
  return RGB(pixel[2], pixel[1], pixel[0]);
}

int DrawTextW(HDC const dc, const wchar_t* const text, const int length, RECT* const rect, const UINT format)
{
  if ((format & (kDtSingleLine | kDtNoPrefix)) != (kDtSingleLine | kDtNoPrefix) ||
      (format & ~(kDtSingleLine | kDtNoPrefix | kDtNoClip | kDtCalcRect)) != 0) {
    ::SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
  }
  Gdi& state = State();
  const std::lock_guard<std::recursive_mutex> lock(state.mutex);
  const auto target = state.Find<DcObject>(dc, Kind::Dc);
  if (target == nullptr || text == nullptr || rect == nullptr) {
    return 0;
  }
  gdi::Font* const font = state.Realize(*target->font);
  if (font == nullptr) {
    return 0;
  }

  // wchar_t holds whole code points here. A length of -1 is a NUL-terminated string.
  std::u32string characters;
  for (int i = 0; length < 0 ? text[i] != L'\0' : i < length; ++i) {
    characters.push_back(static_cast<char32_t>(text[i]));
  }

  const gdi::TextMetrics& metrics = font->Metrics();
  if ((format & kDtCalcRect) != 0) {
    std::int32_t width = 0;
    for (const char32_t c : characters) {
      width += font->Advance(c);
    }
    rect->right = rect->left + width;
    rect->bottom = rect->top + metrics.height;
    return metrics.height;
  }

  // The DC's alignment of the text at the rectangle's corner, as ExtTextOutW takes it: with
  // TA_BASELINE the corner is on the baseline.
  const gdi::Surface surface = SurfaceOf(*target);
  const UINT vertical = target->textAlign & kTaVertical;
  const std::int32_t baseline =
    (vertical == kTaBaseline) ? rect->top : ((vertical == kTaBottom) ? rect->top - metrics.descent : rect->top + metrics.ascent);
  const gdi::Rect clip = ((format & kDtNoClip) != 0) ? gdi::Rect{0, 0, surface.width, surface.height}
                                                     : gdi::Rect{rect->left, rect->top, rect->right, rect->bottom};
  font->Draw(surface, clip, rect->left, baseline, characters.data(), characters.size(), target->textColour, target->backColour, true);
  return metrics.height;
}

HGDIOBJ GetStockObject(const int object)
{
  if (object < 0 || object >= kStockBrushCount) {
    // The stock pens, fonts and palette: nothing here asks for them.
    return nullptr;
  }
  Gdi& state = State();
  const std::lock_guard<std::recursive_mutex> lock(state.mutex);
  return state.stockBrushes[object].get();
}

int FillRect(HDC const dc, const RECT* const rect, HBRUSH const brush)
{
  Gdi& state = State();
  const std::lock_guard<std::recursive_mutex> lock(state.mutex);
  const auto target = state.Find<DcObject>(dc, Kind::Dc);
  const auto fill = state.Find<BrushObject>(brush, Kind::Brush);
  if (target == nullptr || fill == nullptr || rect == nullptr) {
    return 0;
  }
  if (!fill->hollow) {
    gdi::FillSurface(SurfaceOf(*target), gdi::Rect{rect->left, rect->top, rect->right, rect->bottom}, fill->colour);
  }
  return 1;
}

HANDLE AddFontMemResourceEx(void* const fontData, const DWORD size, void*, DWORD* const fontCount)
{
  if (fontCount != nullptr) {
    *fontCount = 0;
  }
  Gdi& state = State();
  const std::lock_guard<std::recursive_mutex> lock(state.mutex);
  const std::size_t added = state.fonts.Add(fontData, size, "memory");
  if (added == 0) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  if (fontCount != nullptr) {
    *fontCount = static_cast<DWORD>(added);
  }
  // Fonts stay for the process: there is no RemoveFontMemResourceEx here, so the handle is only
  // not null.
  return &state.fonts;
}
