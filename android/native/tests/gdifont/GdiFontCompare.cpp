// gdifont_compare (Windows): the Android build's GDI fonts on FreeType (src/win32/GdiFonts.cpp)
// against GDI, for the faces and sizes the game's UI asks for. README.md, "The fonts".
//
//   gdifont_compare <font folder> [<size> ...] [--face <name>] ... [--dump <folder>]
//
// The folder's *.ttf files (the game's fonts/ folder, copied) are registered with GDI as the game
// registers them (AddFontMemResourceEx) and with the Android build's registry. For each face name
// and size, as CD3DFont creates its fonts (CreateFontA with the negative size, ANTIALIASED_QUALITY):
//
//   - the face each one picks: GDI's GetTextFaceW and the file its font data comes from, which can
//     be a font Windows has installed, and the Android build's choice among the folder's fonts;
//   - on the font file GDI took: TEXTMETRICW, the advance of each character the font has (DrawTextW
//     with DT_CALCRECT), the kerning pairs, and the glyphs drawn as CD3DFont draws them (white on
//     black into a 32-bit bitmap, the baseline on the rectangle's top), pixel by pixel.
//
// A line per face and size, with the differences under it, and the totals at the end. A size below
// 0 asks for that cell height, as a positive height does in CreateFontA (CD3DFont gives a negative
// height, from the Lua's size), and 0 for GDI's default. --dump writes each font's glyphs as an image,
// GDI's over ours.

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../../src/win32/GdiFonts.h"

namespace
{
  namespace gdi = faf::android::gdi;

  constexpr int kScratch = 128; // CD3DFont's scratch bitmap

  // The face names the game's Lua asks for (lua.scd, lua.nx2), a few misspelt.
  const char* const kFaces[] = {
    "Arial", "Arial Bold", "Arial Black", "Arial Narrow", "Arial Rounded MT Bold", "Zeroes Three", "Butterbelly", "VDub",
    "Wintermute", "Arial Gras", "Andale Mono", "Courier New", "Times New Roman", "New Times Roman", "Microsoft Sans Serif",
    "Calibri", "Bank Gothic Medium BT", "Ariel"};

  struct FontFile
  {
    std::string name;
    std::vector<char> bytes;
  };

  struct Totals
  {
    int fonts = 0;
    int metricsDiffer = 0;
    long advances = 0;
    long advancesDiffer = 0;
    long kerningPairs = 0;
    long kerningDiffer = 0;
    long glyphs = 0;
    long glyphsEqual = 0;
    long pixels = 0;
    long pixelsDiffer = 0;
    long pixelsFarOff = 0;
  };

  [[nodiscard]] std::string Narrow(const std::wstring& text)
  {
    std::string result;
    for (const wchar_t c : text) {
      result.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return result;
  }

  /// What CD3DFont does with a character: its advance and its glyph, drawn white on black at
  /// (aveCharWidth, ceil(ascent + 1)) with TA_BASELINE.
  struct Geometry
  {
    int baselineX = 0;
    int baselineY = 0;
  };

  [[nodiscard]] Geometry GeometryOf(const int ascent, const int aveCharWidth)
  {
    return Geometry{aveCharWidth, static_cast<int>(std::ceil(static_cast<float>(ascent) + 1.0f))};
  }

  struct GdiFont
  {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HFONT font = nullptr;
    TEXTMETRICW metrics{};
    std::wstring face;
    std::vector<char> data;

    explicit GdiFont(const std::string& faceName, const int size)
    {
      font = ::CreateFontA(-size, 0, 0, 0, 0, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, faceName.c_str());
      dc = ::CreateCompatibleDC(nullptr);
      bitmap = ::CreateBitmap(kScratch, kScratch, 1, 32, nullptr);
      ::SelectObject(dc, bitmap);
      ::SelectObject(dc, font);
      ::GetTextMetricsW(dc, &metrics);
      wchar_t name[LF_FACESIZE]{};
      ::GetTextFaceW(dc, LF_FACESIZE, name);
      face = name;
      const DWORD size32 = ::GetFontData(dc, 0, 0, nullptr, 0);
      if (size32 != GDI_ERROR && size32 > 0) {
        data.resize(size32);
        ::GetFontData(dc, 0, 0, data.data(), size32);
      }
    }
    GdiFont(const GdiFont&) = delete;
    GdiFont& operator=(const GdiFont&) = delete;
    ~GdiFont()
    {
      ::DeleteDC(dc);
      ::DeleteObject(bitmap);
      ::DeleteObject(font);
    }

    [[nodiscard]] int Advance(const wchar_t c) const
    {
      RECT rect{};
      ::DrawTextW(dc, &c, 1, &rect, DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
      return rect.right - rect.left;
    }

    [[nodiscard]] std::vector<KERNINGPAIR> KerningPairs() const
    {
      const DWORD count = ::GetKerningPairsW(dc, 0, nullptr);
      std::vector<KERNINGPAIR> pairs(count);
      if (count > 0) {
        pairs.resize(::GetKerningPairsW(dc, count, pairs.data()));
      }
      return pairs;
    }

    /// The red channel of the scratch bitmap after CD3DFont's DrawTextW.
    [[nodiscard]] std::vector<std::uint8_t> Glyph(const wchar_t c, const Geometry& at) const
    {
      RECT all{0, 0, kScratch, kScratch};
      ::FillRect(dc, &all, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
      ::SetBkColor(dc, RGB(0, 0, 0));
      ::SetTextColor(dc, RGB(255, 255, 255));
      ::SetTextAlign(dc, TA_BASELINE);
      RECT rect{at.baselineX, at.baselineY, at.baselineX, at.baselineY};
      ::DrawTextW(dc, &c, 1, &rect, DT_NOCLIP | DT_NOPREFIX | DT_SINGLELINE);
      ::GdiFlush();
      std::vector<std::uint32_t> bits(kScratch * kScratch);
      ::GetBitmapBits(bitmap, static_cast<LONG>(bits.size() * 4), bits.data());
      std::vector<std::uint8_t> red(bits.size());
      for (std::size_t i = 0; i < bits.size(); ++i) {
        red[i] = static_cast<std::uint8_t>(bits[i] >> 16);
      }
      return red;
    }
  };

  [[nodiscard]] std::vector<std::uint8_t> OurGlyph(const gdi::Font& font, const char32_t c, const Geometry& at)
  {
    std::vector<std::uint8_t> bits(kScratch * kScratch * 4, 0);
    gdi::Surface surface{bits.data(), kScratch, kScratch, kScratch * 4};
    font.Draw(surface, gdi::Rect{0, 0, kScratch, kScratch}, at.baselineX, at.baselineY, &c, 1, RGB(255, 255, 255), RGB(0, 0, 0), true);
    std::vector<std::uint8_t> red(kScratch * kScratch);
    for (std::size_t i = 0; i < red.size(); ++i) {
      red[i] = bits[i * 4 + 2];
    }
    return red;
  }

  void WritePgm(const std::filesystem::path& path, const std::vector<std::vector<std::uint8_t>>& rows, const int width, const int height)
  {
    std::ofstream out(path, std::ios::binary);
    out << "P5\n" << width << " " << height << "\n255\n";
    for (const auto& row : rows) {
      out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
  }

  void Compare(const std::string& face, const int size, const std::vector<FontFile>& files, const gdi::FontRegistry& registry,
               Totals& totals, const std::filesystem::path& dumps)
  {
    const GdiFont reference(face, size);
    std::string gdiFile = "a font Windows has";
    for (const FontFile& file : files) {
      if (file.bytes == reference.data) {
        gdiFile = file.name;
      }
    }

    gdi::LogicalFont logical;
    logical.height = -size;
    logical.charSet = DEFAULT_CHARSET;
    logical.quality = ANTIALIASED_QUALITY;
    logical.faceName = face;
    const std::shared_ptr<gdi::Font> ours = registry.Realize(logical);

    // The implementations on the same file: the one GDI took.
    gdi::FontRegistry same;
    same.Add(reference.data.data(), reference.data.size(), gdiFile);
    const std::shared_ptr<gdi::Font> font = same.Realize(logical);
    if (font == nullptr) {
      std::printf("%-22s %3d  GDI: %s (%s); FreeType cannot open its data\n", face.c_str(), size, Narrow(reference.face).c_str(), gdiFile.c_str());
      return;
    }
    ++totals.fonts;

    std::vector<std::string> notes;
    const TEXTMETRICW& g = reference.metrics;
    const gdi::TextMetrics& o = font->Metrics();
    const std::pair<const char*, std::pair<long, long>> fields[] = {
      {"tmHeight", {g.tmHeight, o.height}},
      {"tmAscent", {g.tmAscent, o.ascent}},
      {"tmDescent", {g.tmDescent, o.descent}},
      {"tmInternalLeading", {g.tmInternalLeading, o.internalLeading}},
      {"tmExternalLeading", {g.tmExternalLeading, o.externalLeading}},
      {"tmAveCharWidth", {g.tmAveCharWidth, o.aveCharWidth}},
      {"tmMaxCharWidth", {g.tmMaxCharWidth, o.maxCharWidth}},
      {"tmWeight", {g.tmWeight, o.weight}},
      {"tmOverhang", {g.tmOverhang, o.overhang}},
      {"tmDigitizedAspectX", {g.tmDigitizedAspectX, o.digitizedAspectX}},
      {"tmDigitizedAspectY", {g.tmDigitizedAspectY, o.digitizedAspectY}},
      {"tmFirstChar", {g.tmFirstChar, o.firstChar}},
      {"tmLastChar", {g.tmLastChar, o.lastChar}},
      {"tmDefaultChar", {g.tmDefaultChar, o.defaultChar}},
      {"tmBreakChar", {g.tmBreakChar, o.breakChar}},
      {"tmItalic", {g.tmItalic, o.italic}},
      {"tmUnderlined", {g.tmUnderlined, o.underlined}},
      {"tmStruckOut", {g.tmStruckOut, o.struckOut}},
      {"tmPitchAndFamily", {g.tmPitchAndFamily, o.pitchAndFamily}},
      {"tmCharSet", {g.tmCharSet, o.charSet}},
    };
    std::string metricNote;
    bool usedByEngine = false;
    for (const auto& [name, values] : fields) {
      if (values.first != values.second) {
        metricNote += std::string(" ") + name + " " + std::to_string(values.first) + "/" + std::to_string(values.second);
        const std::string field = name;
        usedByEngine = usedByEngine || field == "tmHeight" || field == "tmAscent" || field == "tmDescent" ||
                       field == "tmInternalLeading" || field == "tmExternalLeading" || field == "tmAveCharWidth" ||
                       field == "tmOverhang" || field == "tmPitchAndFamily";
      }
    }
    if (!metricNote.empty()) {
      ++totals.metricsDiffer;
      notes.push_back(std::string("metrics (GDI/ours)") + metricNote + (usedByEngine ? "  [CD3DFont reads one of these]" : ""));
    }

    // Advances of the characters the font has.
    std::vector<wchar_t> characters;
    for (wchar_t c = 0x20; c < 0x7F; ++c) {
      characters.push_back(c);
    }
    for (wchar_t c = 0xA0; c <= 0xFF; ++c) {
      characters.push_back(c);
    }
    for (const int c : {0x0152, 0x0153, 0x0160, 0x0161, 0x0178, 0x017D, 0x017E, 0x0192, 0x02C6, 0x02DC, 0x2013, 0x2014, 0x2018, 0x2019,
                        0x201A, 0x201C, 0x201D, 0x201E, 0x2020, 0x2021, 0x2022, 0x2026, 0x2030, 0x2039, 0x203A, 0x20AC, 0x2122}) {
      characters.push_back(static_cast<wchar_t>(c));
    }
    for (wchar_t c = 0x0391; c <= 0x03C9; ++c) {
      characters.push_back(c);
    }
    for (wchar_t c = 0x0410; c <= 0x044F; ++c) {
      characters.push_back(c);
    }
    // What the font itself has: for the rest GDI goes to a linked font, which Android does not have.
    characters.erase(
      std::remove_if(characters.begin(), characters.end(), [&](const wchar_t c) { return !font->HasCharacter(static_cast<char32_t>(c)); }),
      characters.end()
    );
    std::string advanceNote;
    int advanceDiffer = 0;
    for (const wchar_t c : characters) {
      const int expected = reference.Advance(c);
      const int got = font->Advance(static_cast<char32_t>(c));
      ++totals.advances;
      if (expected != got) {
        ++advanceDiffer;
        ++totals.advancesDiffer;
        if (advanceDiffer <= 12) {
          char text[48];
          std::snprintf(text, sizeof(text), " U+%04X %d/%d", static_cast<unsigned>(c), expected, got);
          advanceNote += text;
        }
      }
    }
    if (advanceDiffer > 0) {
      notes.push_back("advances (GDI/ours) " + std::to_string(advanceDiffer) + " of " + std::to_string(characters.size()) + ":" + advanceNote);
    }

    // Kerning pairs, as a set.
    const std::vector<KERNINGPAIR> gdiPairs = reference.KerningPairs();
    const std::vector<gdi::KerningPair> ourPairs = font->KerningPairs();
    std::map<std::pair<int, int>, int> expectedPairs;
    std::map<std::pair<int, int>, int> gotPairs;
    for (const KERNINGPAIR& pair : gdiPairs) {
      expectedPairs[{pair.wFirst, pair.wSecond}] = pair.iKernAmount;
    }
    for (const gdi::KerningPair& pair : ourPairs) {
      gotPairs[{pair.first, pair.second}] = pair.amount;
    }
    int kernMissing = 0;
    int kernExtra = 0;
    int kernAmount = 0;
    std::string kernNote;
    std::string kernExamples;
    for (const auto& [key, amount] : expectedPairs) {
      const auto found = gotPairs.find(key);
      if (found == gotPairs.end()) {
        if (++kernMissing <= 10) {
          char text[64];
          std::snprintf(text, sizeof(text), " -%04X,%04X", key.first, key.second);
          kernExamples += text;
        }
      } else if (found->second != amount) {
        if (++kernAmount <= 6) {
          char text[64];
          std::snprintf(text, sizeof(text), " %04X,%04X %d/%d", key.first, key.second, amount, found->second);
          kernNote += text;
        }
      }
    }
    for (const auto& [key, amount] : gotPairs) {
      (void)amount;
      if (expectedPairs.find(key) == expectedPairs.end()) {
        if (++kernExtra <= 10) {
          char text[64];
          std::snprintf(text, sizeof(text), " +%04X,%04X", key.first, key.second);
          kernExamples += text;
        }
      }
    }
    totals.kerningPairs += static_cast<long>(gdiPairs.size());
    totals.kerningDiffer += kernMissing + kernExtra + kernAmount;
    if (kernMissing + kernExtra + kernAmount > 0 || gdiPairs.size() != ourPairs.size()) {
      notes.push_back("kerning: GDI " + std::to_string(gdiPairs.size()) + " pairs, ours " + std::to_string(ourPairs.size()) + "; " +
                      std::to_string(kernMissing) + " missing, " + std::to_string(kernExtra) + " extra, " + std::to_string(kernAmount) +
                      " amounts (GDI/ours)" + kernNote + kernExamples);
    }

    // The glyphs, at CD3DFont's place for them.
    const Geometry at = GeometryOf(g.tmAscent, g.tmAveCharWidth);
    int equal = 0;
    int drawn = 0;
    long differ = 0;
    long farOff = 0;
    long greyGdi = 0;
    long greyOurs = 0;
    std::string glyphNote;
    const int cellHeight = std::min<int>(kScratch, at.baselineY + g.tmDescent + 4);
    std::vector<std::vector<std::uint8_t>> gdiRows(cellHeight);
    std::vector<std::vector<std::uint8_t>> ourRows(cellHeight);
    for (const wchar_t c : characters) {
      if (c == 0x20 || c == 0xA0) {
        continue;
      }
      const std::vector<std::uint8_t> expected = reference.Glyph(c, at);
      const std::vector<std::uint8_t> got = OurGlyph(*font, static_cast<char32_t>(c), at);
      ++drawn;
      long glyphDiffer = 0;
      for (std::size_t i = 0; i < expected.size(); ++i) {
        greyGdi += (expected[i] != 0 && expected[i] != 255) ? 1 : 0;
        greyOurs += (got[i] != 0 && got[i] != 255) ? 1 : 0;
        if (expected[i] != got[i]) {
          ++glyphDiffer;
          if (std::abs(expected[i] - got[i]) > 64) {
            ++farOff;
          }
        }
        totals.pixels += (expected[i] != 0 || got[i] != 0) ? 1 : 0;
      }
      differ += glyphDiffer;
      if (glyphDiffer == 0) {
        ++equal;
      } else if (glyphNote.size() < 120) {
        char text[32];
        std::snprintf(text, sizeof(text), " U+%04X:%ld", static_cast<unsigned>(c), glyphDiffer);
        glyphNote += text;
      }
      const int cellWidth = std::min(kScratch, at.baselineX * 2 + reference.Advance(c) + 2);
      for (int y = 0; y < cellHeight; ++y) {
        gdiRows[y].insert(gdiRows[y].end(), expected.begin() + y * kScratch, expected.begin() + y * kScratch + cellWidth);
        ourRows[y].insert(ourRows[y].end(), got.begin() + y * kScratch, got.begin() + y * kScratch + cellWidth);
      }
    }
    totals.glyphs += drawn;
    totals.glyphsEqual += equal;
    totals.pixelsDiffer += differ;
    totals.pixelsFarOff += farOff;
    if (!dumps.empty() && drawn > 0) {
      std::vector<std::vector<std::uint8_t>> rows = gdiRows;
      rows.insert(rows.end(), ourRows.begin(), ourRows.end());
      std::string name = face + "-" + std::to_string(size) + ".pgm";
      std::replace(name.begin(), name.end(), ' ', '_');
      WritePgm(dumps / name, rows, static_cast<int>(gdiRows[0].size()), static_cast<int>(rows.size()));
    }

    const std::string ourChoice = (ours != nullptr) ? ours->FaceName() + " (" + ours->Origin() + ")" : "none";
    const std::string gdiChoice =
      Narrow(reference.face) + " " + std::to_string(g.tmWeight) + " (" + gdiFile + ", " + std::to_string(reference.data.size()) + ")";
    std::printf(
      "%-22s %3d  GDI %-34s ours %-34s %s, glyphs %d/%d equal, %ld pixels differ (%ld by more than 64); grey pixels GDI %ld ours %ld\n",
      face.c_str(), size, gdiChoice.c_str(), ourChoice.c_str(), font->Antialiased() ? "grey" : "bi-level", equal, drawn, differ,
      farOff, greyGdi, greyOurs
    );
    if (!glyphNote.empty() && differ > 0) {
      notes.push_back("glyphs that differ (pixels):" + glyphNote);
    }
    for (const std::string& note : notes) {
      std::printf("    %s\n", note.c_str());
    }
  }
} // namespace

int main(int argc, char** argv)
{
  if (argc < 2) {
    std::fprintf(stderr, "usage: gdifont_compare <font folder> [<size> ...] [--face <name>] ... [--dump <folder>]\n");
    return 2;
  }
  std::vector<int> sizes;
  std::vector<std::string> faces;
  std::filesystem::path dumps;
  for (int i = 2; i < argc; ++i) {
    if (std::string(argv[i]) == "--dump" && i + 1 < argc) {
      dumps = argv[++i];
      std::filesystem::create_directories(dumps);
    } else if (std::string(argv[i]) == "--face" && i + 1 < argc) {
      faces.push_back(argv[++i]);
    } else {
      sizes.push_back(std::atoi(argv[i]));
    }
  }
  if (sizes.empty()) {
    for (int size = 8; size <= 32; ++size) {
      sizes.push_back(size);
    }
    sizes.push_back(36);
    sizes.push_back(48);
  }

  // GDI's default height (size 0) is 12 points on this screen; the Android build has 96 pixels per inch.
  const HDC screen = ::GetDC(nullptr);
  std::printf("%d pixels per inch\n", ::GetDeviceCaps(screen, LOGPIXELSY));
  ::ReleaseDC(nullptr, screen);

  std::vector<FontFile> files;
  gdi::FontRegistry registry;
  for (const auto& entry : std::filesystem::directory_iterator(argv[1])) {
    std::string extension = entry.path().extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](const char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".ttf") {
      continue;
    }
    std::ifstream in(entry.path(), std::ios::binary);
    FontFile file{entry.path().filename().string(), std::vector<char>(std::istreambuf_iterator<char>(in), {})};
    DWORD gdiFaces = 0;
    if (::AddFontMemResourceEx(file.bytes.data(), static_cast<DWORD>(file.bytes.size()), nullptr, &gdiFaces) == nullptr) {
      std::printf("GDI does not take %s\n", file.name.c_str());
    }
    const std::size_t ourFaces = registry.Add(file.bytes.data(), file.bytes.size(), file.name);
    if (ourFaces != gdiFaces) {
      std::printf("%s: GDI registers %lu faces, ours %zu\n", file.name.c_str(), gdiFaces, ourFaces);
    }
    files.push_back(std::move(file));
  }

  if (faces.empty()) {
    faces.assign(std::begin(kFaces), std::end(kFaces));
  }
  Totals totals;
  for (const std::string& face : faces) {
    for (const int size : sizes) {
      Compare(face, size, files, registry, totals, dumps);
    }
  }
  std::printf(
    "\n%d fonts: metrics differ in %d; advances %ld of %ld differ; kerning pairs %ld differences in %ld; glyphs %ld of %ld equal, "
    "%ld of %ld inked pixels differ (%ld by more than 64)\n",
    totals.fonts, totals.metricsDiffer, totals.advancesDiffer, totals.advances, totals.kerningDiffer, totals.kerningPairs,
    totals.glyphsEqual, totals.glyphs, totals.pixelsDiffer, totals.pixels, totals.pixelsFarOff
  );
  return 0;
}
