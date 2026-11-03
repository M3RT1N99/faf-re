// GDI's fonts on FreeType (src/win32/GdiAndroid.cpp, GdiFonts.cpp) as the font renderer calls them
// (moho/render/d3d/CD3DFont.cpp): the registration, the face a name finds, the metrics, the
// advances, the kerning pairs, and the pixels of a glyph. The fonts are made here, boxes whose edges
// fall where the numbers can be worked out by hand; tests/gdifont checks the same code against the
// real GDI on Windows, with the game's fonts.

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "Check.h"

namespace
{
  constexpr int kScratch = 128; // CD3DFont's scratch bitmap
  constexpr UINT kMeasure = 0xC20u; // DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE, as CD3DFont passes it
  constexpr UINT kDraw = 0x920u;    // DT_NOCLIP | DT_NOPREFIX | DT_SINGLELINE
  constexpr UINT kBaseline = 0x18u; // TA_BASELINE

  /// A TrueType file in memory, table by table.
  class FontFile
  {
  public:
    void Table(const char (&tag)[5], std::vector<std::uint8_t> bytes)
    {
      mTables.push_back(Entry{tag, std::move(bytes)});
    }

    [[nodiscard]] std::vector<std::uint8_t> Bytes() const
    {
      std::vector<std::uint8_t> file;
      Put32(file, 0x00010000u);
      Put16(file, static_cast<std::uint16_t>(mTables.size()));
      Put16(file, 0);
      Put16(file, 0);
      Put16(file, 0);
      std::uint32_t offset = static_cast<std::uint32_t>(12 + 16 * mTables.size());
      for (const Entry& table : mTables) {
        file.insert(file.end(), table.tag.begin(), table.tag.end());
        Put32(file, 0); // FreeType reads no table's checksum
        Put32(file, offset);
        Put32(file, static_cast<std::uint32_t>(table.bytes.size()));
        offset += static_cast<std::uint32_t>((table.bytes.size() + 3) & ~std::size_t{3});
      }
      for (const Entry& table : mTables) {
        file.insert(file.end(), table.bytes.begin(), table.bytes.end());
        file.resize((file.size() + 3) & ~std::size_t{3}, 0);
      }
      return file;
    }

    static void Put16(std::vector<std::uint8_t>& out, const int value)
    {
      out.push_back(static_cast<std::uint8_t>(value >> 8));
      out.push_back(static_cast<std::uint8_t>(value));
    }

    static void Put32(std::vector<std::uint8_t>& out, const std::uint32_t value)
    {
      Put16(out, static_cast<int>(value >> 16));
      Put16(out, static_cast<int>(value & 0xFFFFu));
    }

  private:
    struct Entry
    {
      std::string tag;
      std::vector<std::uint8_t> bytes;
    };
    std::vector<Entry> mTables; // sorted by tag by whoever adds them
  };

  struct Box
  {
    int character; // 0: the missing glyph, the first
    int advance;
    int left;
    int bottom;
    int right;
    int top;
  };

  // The glyphs, in a font of 1024 units per em: 64 units are a pixel at 16 pixels per em. H stands
  // on the baseline, I hangs below it, J holds a single sample of the 16 of a grey pixel at 20 pixels
  // per em (a sample is 12.8 units), K two.
  constexpr std::array<Box, 6> kBoxes{{
    {0, 512, 0, 0, 0, 0},
    {' ', 256, 0, 0, 0, 0},
    {'H', 640, 64, 0, 576, 768},
    {'I', 640, 64, -256, 576, 0},
    {'J', 640, 4, 4, 9, 9},
    {'K', 640, 4, 4, 22, 9},
  }};

  /// The font of these boxes with a family and full name, a weight and an average character width,
  /// what the tests tell the fonts apart by.
  [[nodiscard]] std::vector<std::uint8_t> MakeFont(
    const std::string& family, const std::string& fullName, const int weight, const int averageWidth
  )
  {
    using F = FontFile;
    std::vector<std::uint8_t> glyf;
    std::vector<std::uint8_t> loca;
    std::vector<std::uint8_t> hmtx;
    for (const Box& box : kBoxes) {
      F::Put32(loca, static_cast<std::uint32_t>(glyf.size()));
      F::Put16(hmtx, box.advance);
      F::Put16(hmtx, box.left);
      if (box.right == box.left) {
        continue;
      }
      F::Put16(glyf, 1); // one contour
      F::Put16(glyf, box.left);
      F::Put16(glyf, box.bottom);
      F::Put16(glyf, box.right);
      F::Put16(glyf, box.top);
      F::Put16(glyf, 3); // its last point
      F::Put16(glyf, 0); // no instructions
      glyf.insert(glyf.end(), 4, 0x01); // four points on the curve, coordinates as 16-bit deltas
      for (const int x : {box.left, 0, box.right - box.left, 0}) {
        F::Put16(glyf, x);
      }
      for (const int y : {box.bottom, box.top - box.bottom, 0, box.bottom - box.top}) {
        F::Put16(glyf, y);
      }
    }
    F::Put32(loca, static_cast<std::uint32_t>(glyf.size()));

    std::vector<std::uint8_t> head;
    F::Put32(head, 0x00010000u); // version
    F::Put32(head, 0x00010000u); // revision
    F::Put32(head, 0);           // checksum adjustment
    F::Put32(head, 0x5F0F3CF5u); // magic
    F::Put16(head, 0x0003);      // no bit 4: nothing can alter an advance, so the widths are scaled
    F::Put16(head, 1024);        // units per em
    head.resize(head.size() + 16, 0); // the dates
    for (const int value : {0, -256, 640, 768, weight >= 700 ? 1 : 0, 8, 2, 1, 0}) {
      F::Put16(head, value); // the box, macStyle, lowest size, direction, long loca, glyf format
    }

    std::vector<std::uint8_t> hhea;
    F::Put32(hhea, 0x00010000u);
    for (const int value : {896, -256, 128, 640, 0, 0, 640, 1, 0, 0, 0, 0, 0, 0, 0}) {
      F::Put16(hhea, value); // ascender, descender, line gap, widest advance, ... slope, metric format
    }
    F::Put16(hhea, static_cast<int>(kBoxes.size()));

    std::vector<std::uint8_t> maxp;
    F::Put32(maxp, 0x00010000u);
    F::Put16(maxp, static_cast<int>(kBoxes.size()));
    for (const int value : {4, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0}) {
      F::Put16(maxp, value); // four points in one contour, two zones, no instructions
    }

    std::vector<std::uint8_t> os2;
    F::Put16(os2, 1); // version
    F::Put16(os2, averageWidth);
    F::Put16(os2, weight);
    os2.resize(68, 0); // from the width class to the character range, which come next
    os2[63] = 0x40;    // fsSelection: regular
    os2[65] = ' ';     // usFirstCharIndex
    os2[67] = 'K';     // usLastCharIndex
    for (const int value : {768, -256, 0, 896, 256}) {
      F::Put16(os2, value); // the typographic extents and line gap, the Windows extents
    }
    F::Put32(os2, 1); // Latin 1
    F::Put32(os2, 0);

    // The characters in three segments: the space, H to K, and the end.
    std::vector<std::uint8_t> cmap;
    F::Put16(cmap, 0);
    F::Put16(cmap, 1);
    F::Put16(cmap, 3); // Windows
    F::Put16(cmap, 1); // Unicode
    F::Put32(cmap, 12);
    const std::array<int, 3> starts{' ', 'H', 0xFFFF};
    const std::array<int, 3> ends{' ', 'K', 0xFFFF};
    const std::array<int, 3> deltas{1 - ' ', 2 - 'H', 1};
    F::Put16(cmap, 4);
    F::Put16(cmap, 16 + 8 * 3);
    F::Put16(cmap, 0);
    for (const int value : {3 * 2, 0, 0, 0}) {
      F::Put16(cmap, value);
    }
    for (const int value : ends) {
      F::Put16(cmap, value);
    }
    F::Put16(cmap, 0);
    for (const int value : starts) {
      F::Put16(cmap, value);
    }
    for (const int value : deltas) {
      F::Put16(cmap, value);
    }
    for (int i = 0; i < 3; ++i) {
      F::Put16(cmap, 0);
    }

    // H then I come closer by 2 pixels at 16 pixels per em, I then H go apart by one.
    std::vector<std::uint8_t> kern;
    F::Put16(kern, 0);
    F::Put16(kern, 1);
    F::Put16(kern, 0);
    F::Put16(kern, 14 + 2 * 6);
    F::Put16(kern, 0x0001); // horizontal, format 0
    for (const int value : {2, 0, 0, 0, 2, 3, -128, 3, 2, 64}) {
      F::Put16(kern, value);
    }

    // The names in UTF-16: the family and the full name, in US English.
    std::vector<std::uint8_t> text;
    const auto wide = [&text](const std::string& name) {
      for (const char c : name) {
        FontFile::Put16(text, c);
      }
    };
    wide(family);
    wide(fullName);
    std::vector<std::uint8_t> name;
    F::Put16(name, 0);
    F::Put16(name, 2);
    F::Put16(name, 6 + 2 * 12);
    const int familyBytes = static_cast<int>(family.size()) * 2;
    const int fullBytes = static_cast<int>(fullName.size()) * 2;
    for (const int value : {3, 1, 0x409, 1, familyBytes, 0, 3, 1, 0x409, 4, fullBytes, familyBytes}) {
      F::Put16(name, value);
    }
    name.insert(name.end(), text.begin(), text.end());

    FontFile file;
    file.Table("OS/2", os2);
    file.Table("cmap", cmap);
    file.Table("glyf", glyf);
    file.Table("head", head);
    file.Table("hhea", hhea);
    file.Table("hmtx", hmtx);
    file.Table("kern", kern);
    file.Table("loca", loca);
    file.Table("maxp", maxp);
    file.Table("name", name);
    return file.Bytes();
  }

  void Register(const std::vector<std::uint8_t>& font)
  {
    DWORD faces = 0;
    std::vector<std::uint8_t> copy = font; // GDI copies what it registers, so the buffer may go
    WXT_CHECK(::AddFontMemResourceEx(copy.data(), static_cast<DWORD>(copy.size()), nullptr, &faces) != nullptr);
    WXT_CHECK_EQ(faces, DWORD{1});
  }

  /// The fonts of these tests, registered once: the family of the tests, and an Arial in two
  /// styles for the fallback.
  void RegisterFonts()
  {
    static const bool sRegistered = [] {
      Register(MakeFont("Faf Test", "Faf Test", 400, 512));
      Register(MakeFont("Arial", "Arial", 400, 320));
      Register(MakeFont("Arial", "Arial Bold", 700, 400));
      return true;
    }();
    (void)sRegistered;
  }

  /// CD3DFont's objects for a font: a memory DC with the font and the 32-bit scratch bitmap.
  class Scratch
  {
  public:
    Scratch(const int height, const char* const face, const int weight = 0)
    {
      RegisterFonts();
      font = ::CreateFontA(height, 0, 0, 0, weight, 0, 0, 0, 1u, 0, 0, 4u, 0, face);
      dc = ::CreateCompatibleDC(nullptr);
      bitmap = ::CreateBitmap(kScratch, kScratch, 1u, 32u, nullptr);
      (void)::SelectObject(dc, bitmap);
      (void)::SelectObject(dc, font);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    ~Scratch()
    {
      (void)::DeleteObject(font);
      (void)::DeleteObject(bitmap);
      (void)::DeleteObject(dc);
    }

    [[nodiscard]] TEXTMETRICW Metrics() const
    {
      TEXTMETRICW metrics{};
      WXT_CHECK(::GetTextMetricsW(dc, &metrics));
      return metrics;
    }

    [[nodiscard]] int Advance(const wchar_t* const text, const int length = 1) const
    {
      RECT rect{};
      (void)::DrawTextW(dc, text, length, &rect, kMeasure);
      return rect.right - rect.left;
    }

    /// Clears the bitmap and draws the character white on black, its pen and baseline at (x, y).
    void Draw(const wchar_t character, const int x, const int y) const
    {
      RECT all{0, 0, kScratch, kScratch};
      WXT_CHECK(::FillRect(dc, &all, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH))));
      (void)::SetBkColor(dc, RGB(0, 0, 0));
      (void)::SetTextColor(dc, RGB(255, 255, 255));
      (void)::SetTextAlign(dc, kBaseline);
      RECT rect{x, y, x, y};
      (void)::DrawTextW(dc, &character, 1, &rect, kDraw);
    }

    [[nodiscard]] COLORREF Pixel(const int x, const int y) const
    {
      return ::GetPixel(dc, x, y);
    }

    /// Every pixel of the bitmap outside the rectangle (right and bottom exclusive) is black, and
    /// every pixel inside has the colour.
    void CheckBox(const RECT& box, const COLORREF colour) const
    {
      int wrong = 0;
      for (int y = 0; y < kScratch; ++y) {
        for (int x = 0; x < kScratch; ++x) {
          const bool inside = x >= box.left && x < box.right && y >= box.top && y < box.bottom;
          wrong += Pixel(x, y) != (inside ? colour : RGB(0, 0, 0)) ? 1 : 0;
        }
      }
      WXT_CHECK_EQ(wrong, 0);
    }

    HFONT font = nullptr;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
  };

  constexpr COLORREF Grey(const int value)
  {
    return RGB(value, value, value);
  }
} // namespace

WXT_TEST(GdiFontsWithNoFontRegisteredComeFromTheSystem)
{
  // The first of these tests, before the fonts above are registered: the game has none, so a face
  // is Android's own where the system has one (the emulator; not a Linux host).
  const HFONT font = ::CreateFontA(-16, 0, 0, 0, 0, 0, 0, 0, 1u, 0, 0, 4u, 0, "Arial");
  WXT_CHECK(font != nullptr);
  const HDC dc = ::CreateCompatibleDC(nullptr);
  (void)::SelectObject(dc, font);
  TEXTMETRICW metrics{};
  std::FILE* const system = std::fopen("/system/fonts/Roboto-Regular.ttf", "rb");
  WXT_CHECK_EQ(::GetTextMetricsW(dc, &metrics) != FALSE, system != nullptr);
  if (system != nullptr) {
    (void)std::fclose(system);
    WXT_CHECK_EQ(metrics.tmWeight, LONG{400});
  }
  (void)::DeleteObject(dc);
  (void)::DeleteObject(font);
}

WXT_TEST(GdiRegistersFontsFromMemory)
{
  std::array<std::uint8_t, 64> notAFont{};
  DWORD faces = 7;
  WXT_CHECK(::AddFontMemResourceEx(notAFont.data(), static_cast<DWORD>(notAFont.size()), nullptr, &faces) == nullptr);
  WXT_CHECK_EQ(faces, DWORD{0});
  RegisterFonts();
}

WXT_TEST(GdiFindsTheFamilyNameAloneAndElseArial)
{
  const auto averageWidth = [](const char* const face, const int weight = 0) { return Scratch(-16, face, weight).Metrics().tmAveCharWidth; };
  // 512, 320 and 400 units at 16 pixels per em, whatever the case of the name.
  WXT_CHECK_EQ(averageWidth("Faf Test"), LONG{8});
  WXT_CHECK_EQ(averageWidth("FAF TEST"), LONG{8});
  WXT_CHECK_EQ(averageWidth("Arial"), LONG{5});
  // A family's style by its weight, as CreateFont's arguments ask for it.
  WXT_CHECK_EQ(averageWidth("Arial", 700), LONG{6});
  WXT_CHECK_EQ(Scratch(-16, "Arial", 700).Metrics().tmWeight, LONG{700});
  // GDI does not know a full name, or a name it has no font of: Arial's regular style.
  WXT_CHECK_EQ(averageWidth("Arial Bold"), LONG{5});
  WXT_CHECK_EQ(averageWidth("Andale Mono"), LONG{5});
}

WXT_TEST(GdiMetricsComeFromTheTablesAtTheEmSize)
{
  const Scratch scratch(-16, "Faf Test");
  const TEXTMETRICW metrics = scratch.Metrics();
  // At 16 pixels per em, 64 units are a pixel.
  WXT_CHECK_EQ(metrics.tmAscent, LONG{14});
  WXT_CHECK_EQ(metrics.tmDescent, LONG{4});
  WXT_CHECK_EQ(metrics.tmHeight, LONG{18});
  WXT_CHECK_EQ(metrics.tmInternalLeading, LONG{2});
  WXT_CHECK_EQ(metrics.tmExternalLeading, LONG{2});
  WXT_CHECK_EQ(metrics.tmAveCharWidth, LONG{8});
  WXT_CHECK_EQ(metrics.tmMaxCharWidth, LONG{10});
  WXT_CHECK_EQ(metrics.tmWeight, LONG{400});
  WXT_CHECK_EQ(metrics.tmOverhang, LONG{0});
  WXT_CHECK_EQ(metrics.tmDigitizedAspectY, LONG{96});
  WXT_CHECK_EQ(static_cast<int>(metrics.tmFirstChar), int{' '});
  WXT_CHECK_EQ(static_cast<int>(metrics.tmLastChar), int{'K'});
  WXT_CHECK_EQ(static_cast<int>(metrics.tmBreakChar), int{' '});
  WXT_CHECK_EQ(static_cast<int>(metrics.tmDefaultChar), int{' ' - 1});
  WXT_CHECK_EQ(int{metrics.tmItalic}, 0);
  WXT_CHECK_EQ(int{metrics.tmPitchAndFamily}, 0x07); // variable pitch, vector, TrueType
  WXT_CHECK_EQ(int{metrics.tmCharSet}, 0);
}

WXT_TEST(GdiPositiveHeightIsTheCellAndZeroItsDefault)
{
  // 18 pixels of a cell of 1152 units are 16 pixels per em.
  const TEXTMETRICW cell = Scratch(18, "Faf Test").Metrics();
  WXT_CHECK_EQ(cell.tmHeight, LONG{18});
  WXT_CHECK_EQ(cell.tmInternalLeading, LONG{2});
  // Height 0 is 20 pixels per em, with an ascent of 17.5, rounded up, and a descent of 5.
  const TEXTMETRICW normal = Scratch(0, "Faf Test").Metrics();
  WXT_CHECK_EQ(normal.tmAscent, LONG{18});
  WXT_CHECK_EQ(normal.tmDescent, LONG{5});
  WXT_CHECK_EQ(normal.tmInternalLeading, LONG{3});
}

WXT_TEST(GdiMeasuresScaledAdvances)
{
  const Scratch scratch(-16, "Faf Test");
  RECT rect{3, 5, 0, 0};
  const wchar_t h = L'H';
  WXT_CHECK_EQ(::DrawTextW(scratch.dc, &h, 1, &rect, kMeasure), 18);
  WXT_CHECK_EQ(rect.right, LONG{3 + 10});
  WXT_CHECK_EQ(rect.bottom, LONG{5 + 18});
  WXT_CHECK_EQ(scratch.Advance(L" "), 4);
  // A string, by its length or to its end; a character the font lacks is the missing glyph.
  WXT_CHECK_EQ(scratch.Advance(L"HI ", 3), 24);
  WXT_CHECK_EQ(scratch.Advance(L"HI ", -1), 24);
  WXT_CHECK_EQ(scratch.Advance(L"\x4E00"), 8);
  // 7.5 pixels round up.
  WXT_CHECK_EQ(Scratch(-12, "Faf Test").Advance(L"H"), 8);
}

WXT_TEST(GdiKerningPairsAreTheTablesScaled)
{
  const Scratch scratch(-16, "Faf Test");
  WXT_CHECK_EQ(::GetKerningPairsW(scratch.dc, 0, nullptr), DWORD{2});
  std::array<KERNINGPAIR, 2> pairs{};
  WXT_CHECK_EQ(::GetKerningPairsW(scratch.dc, 2, pairs.data()), DWORD{2});
  WXT_CHECK_EQ(int{pairs[0].wFirst}, int{'H'});
  WXT_CHECK_EQ(int{pairs[0].wSecond}, int{'I'});
  WXT_CHECK_EQ(pairs[0].iKernAmount, -2);
  WXT_CHECK_EQ(int{pairs[1].wFirst}, int{'I'});
  WXT_CHECK_EQ(int{pairs[1].wSecond}, int{'H'});
  WXT_CHECK_EQ(pairs[1].iKernAmount, 1);
}

WXT_TEST(GdiDrawsBiLevelBetween9And17PixelsWithoutAGaspTable)
{
  const Scratch scratch(-16, "Faf Test");
  // H stands on the baseline: 512 by 768 units from 64 on, 8 by 12 pixels from 1 on.
  scratch.Draw(L'H', 10, 30);
  scratch.CheckBox(RECT{11, 18, 19, 30}, Grey(255));
  // I hangs below it, by 4 pixels.
  scratch.Draw(L'I', 10, 30);
  scratch.CheckBox(RECT{11, 30, 19, 34}, Grey(255));
  // The space draws nothing.
  scratch.Draw(L' ', 10, 30);
  scratch.CheckBox(RECT{}, Grey(255));
}

WXT_TEST(GdiDrawsGreyFromItsSamples)
{
  const Scratch scratch(-20, "Faf Test");
  // At 20 pixels per em H's edges are at 1.25 and 11.25 pixels: 3 of the 4 sample columns of its
  // first pixel are inside and 1 of its last, in all 4 rows of a sample. Levels 12 and 4 of 16.
  scratch.Draw(L'H', 10, 30);
  WXT_CHECK_EQ(scratch.Pixel(10, 25), Grey(0));
  WXT_CHECK_EQ(scratch.Pixel(11, 25), Grey(0xE1));
  WXT_CHECK_EQ(scratch.Pixel(12, 25), Grey(0xFF));
  WXT_CHECK_EQ(scratch.Pixel(20, 25), Grey(0xFF));
  WXT_CHECK_EQ(scratch.Pixel(21, 25), Grey(0x8C));
  WXT_CHECK_EQ(scratch.Pixel(22, 25), Grey(0));
  WXT_CHECK_EQ(scratch.Pixel(12, 15), Grey(0xFF)); // its top row, 15 pixels above the baseline
  WXT_CHECK_EQ(scratch.Pixel(12, 14), Grey(0));
  WXT_CHECK_EQ(scratch.Pixel(12, 29), Grey(0xFF));
  WXT_CHECK_EQ(scratch.Pixel(12, 30), Grey(0));

  // A single sample draws nothing; two are the first level GDI shows.
  scratch.Draw(L'J', 10, 30);
  scratch.CheckBox(RECT{}, Grey(255));
  scratch.Draw(L'K', 10, 30);
  scratch.CheckBox(RECT{10, 29, 11, 30}, Grey(0x68));
}

WXT_TEST(GdiFillsTheCellsWithTheBackgroundAndDrawsInTheTextColour)
{
  const Scratch scratch(-16, "Faf Test");
  scratch.Draw(L' ', 10, 30);
  (void)::SetBkColor(scratch.dc, RGB(0, 0, 255));
  (void)::SetTextColor(scratch.dc, RGB(255, 0, 0));
  RECT rect{10, 30, 10, 30};
  const wchar_t h = L'H';
  WXT_CHECK_EQ(::DrawTextW(scratch.dc, &h, 1, &rect, kDraw), 18);
  // The cell: from the pen over the advance, the ascent above the baseline, the descent below.
  WXT_CHECK_EQ(scratch.Pixel(10, 16), RGB(0, 0, 255));
  WXT_CHECK_EQ(scratch.Pixel(10, 15), RGB(0, 0, 0));
  WXT_CHECK_EQ(scratch.Pixel(19, 33), RGB(0, 0, 255));
  WXT_CHECK_EQ(scratch.Pixel(20, 33), RGB(0, 0, 0));
  WXT_CHECK_EQ(scratch.Pixel(19, 34), RGB(0, 0, 0));
  WXT_CHECK_EQ(scratch.Pixel(11, 20), RGB(255, 0, 0));
}

WXT_TEST(GdiBitmapReadsBackAsCD3DFontReadsIt)
{
  const Scratch scratch(-20, "Faf Test");
  scratch.Draw(L'H', 10, 30);
  BITMAP info{};
  WXT_CHECK_EQ(::GetObjectW(scratch.bitmap, sizeof(info), &info), static_cast<int>(sizeof(info)));
  WXT_CHECK_EQ(info.bmWidth, LONG{kScratch});
  WXT_CHECK_EQ(info.bmHeight, LONG{kScratch});
  WXT_CHECK_EQ(info.bmWidthBytes, LONG{kScratch * 4});
  WXT_CHECK_EQ(int{info.bmBitsPixel}, 32);
  std::vector<std::uint32_t> bits(kScratch * kScratch);
  const LONG bytes = static_cast<LONG>(bits.size() * 4);
  WXT_CHECK(::GdiFlush());
  WXT_CHECK_EQ(::GetBitmapBits(scratch.bitmap, bytes, bits.data()), bytes);
  int different = 0;
  for (int y = 0; y < kScratch; ++y) {
    for (int x = 0; x < kScratch; ++x) {
      // Bytes blue, green, red, and one that stays 0.
      const std::uint32_t bgrx = bits[static_cast<std::size_t>(y) * kScratch + x];
      different += (RGB((bgrx >> 16) & 0xFFu, (bgrx >> 8) & 0xFFu, bgrx & 0xFFu) != scratch.Pixel(x, y) || (bgrx >> 24) != 0) ? 1 : 0;
    }
  }
  WXT_CHECK_EQ(different, 0);
  WXT_CHECK_EQ(scratch.Pixel(kScratch, 0), COLORREF{0xFFFFFFFFu}); // CLR_INVALID: outside
}

WXT_TEST(GdiKeepsWhatIsSelectedUntilTheDCGoes)
{
  // CD3DFont's destructor: the font and the bitmap deleted while selected, then the DC.
  RegisterFonts();
  const HFONT font = ::CreateFontA(-16, 0, 0, 0, 0, 0, 0, 0, 1u, 0, 0, 4u, 0, "Faf Test");
  const HDC dc = ::CreateCompatibleDC(nullptr);
  const HBITMAP bitmap = ::CreateBitmap(kScratch, kScratch, 1u, 32u, nullptr);
  WXT_CHECK(::SelectObject(dc, bitmap) != nullptr);
  WXT_CHECK(::SelectObject(dc, font) != nullptr);
  WXT_CHECK(::DeleteObject(font));
  WXT_CHECK(::DeleteObject(bitmap));
  WXT_CHECK(!::DeleteObject(font));
  std::uint32_t bits = 0;
  WXT_CHECK_EQ(::GetBitmapBits(bitmap, sizeof(bits), &bits), LONG{0});
  // The DC goes on with them.
  TEXTMETRICW metrics{};
  WXT_CHECK(::GetTextMetricsW(dc, &metrics));
  WXT_CHECK_EQ(metrics.tmHeight, LONG{18});
  (void)::SetTextColor(dc, RGB(255, 255, 255));
  (void)::SetTextAlign(dc, kBaseline);
  RECT rect{10, 30, 10, 30};
  const wchar_t h = L'H';
  WXT_CHECK_EQ(::DrawTextW(dc, &h, 1, &rect, kDraw), 18);
  WXT_CHECK_EQ(::GetPixel(dc, 11, 25), RGB(255, 255, 255));
  // After it nothing takes the handle.
  WXT_CHECK(::DeleteObject(dc));
  WXT_CHECK_EQ(::SetTextAlign(dc, kBaseline), UINT{0xFFFFFFFFu});
  WXT_CHECK_EQ(::GetPixel(dc, 11, 25), COLORREF{0xFFFFFFFFu});
  // Stock objects stay.
  const HGDIOBJ black = ::GetStockObject(BLACK_BRUSH);
  WXT_CHECK(::DeleteObject(black));
  WXT_CHECK(::GetStockObject(BLACK_BRUSH) == black);
}

WXT_TEST(GdiSaysNoToWhatItDoesNotDraw)
{
  const Scratch scratch(-16, "Faf Test");
  RECT rect{};
  const wchar_t h = L'H';
  ::SetLastError(0);
  WXT_CHECK_EQ(::DrawTextW(scratch.dc, &h, 1, &rect, 0x921u), 0); // DT_CENTER
  WXT_CHECK_EQ(::GetLastError(), DWORD{ERROR_NOT_SUPPORTED});
  WXT_CHECK_EQ(::DrawTextW(scratch.dc, &h, 1, &rect, 0x100u), 0); // lines and prefix characters
  WXT_CHECK_EQ(::SetTextAlign(scratch.dc, 0x06u), UINT{0xFFFFFFFFu}); // TA_CENTER
  WXT_CHECK(::CreateBitmap(0, 8, 1u, 32u, nullptr) == nullptr);
  WXT_CHECK(::CreateBitmap(8, 8, 1u, 1u, nullptr) == nullptr);
}
