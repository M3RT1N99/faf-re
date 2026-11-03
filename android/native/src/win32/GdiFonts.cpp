// GDI's TrueType fonts on FreeType (GdiFonts.h). Where GDI's behaviour is not documented, this
// follows Wine's GDI (dlls/win32u/freetype.c, dlls/win32u/dibdrv), whose numbers were measured
// against Windows: the sizes from the VDMX table, TEXTMETRIC's formulas, the 17 grey levels and their
// intensities. tests/gdifont checks the result against the real GDI, which decided the rest.

#include "GdiFonts.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include FT_BITMAP_H
#include FT_DRIVER_H
#include FT_GASP_H
#include FT_MODULE_H
#include FT_OUTLINE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H
#include FT_TRUETYPE_TABLES_H
#include FT_TRUETYPE_TAGS_H

#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace faf::android::gdi
{
  namespace
  {
    // LOGFONT and TEXTMETRIC values (wingdi.h).
    constexpr std::uint8_t kNonAntialiasedQuality = 3;
    constexpr std::uint8_t kAnsiCharSet = 0;
    constexpr std::uint8_t kDefaultCharSet = 1;
    constexpr std::uint8_t kSymbolCharSet = 2;
    constexpr std::uint8_t kPitchVariable = 0x01; // TMPF_FIXED_PITCH: set for variable pitch, as GDI has it
    constexpr std::uint8_t kPitchVector = 0x02;   // TMPF_VECTOR
    constexpr std::uint8_t kPitchTrueType = 0x04; // TMPF_TRUETYPE
    constexpr std::uint8_t kFamilyRoman = 0x10;   // FF_ROMAN
    constexpr std::uint8_t kFamilySwiss = 0x20;   // FF_SWISS
    constexpr std::uint8_t kFamilyModern = 0x30;  // FF_MODERN
    constexpr std::uint8_t kFamilyScript = 0x40;  // FF_SCRIPT
    constexpr std::uint8_t kFamilyDecorative = 0x50; // FF_DECORATIVE

    // The intensities of GDI's 17 grey levels for white text on black, measured on Windows by Wine
    // (dibdrv/graphics.c): a level's share of the text colour is not linear in its coverage. GDI
    // draws nothing for the lowest level (1 sample of 16), as tests/gdifont shows: the 0x4d is
    // Wine's, for blending the levels above it towards dark text.
    constexpr std::array<std::int32_t, 17> kGreyRamp{
      0x00, 0x4d, 0x68, 0x7c, 0x8c, 0x9a, 0xa7, 0xb2, 0xbd, 0xc7, 0xd0, 0xd9, 0xe1, 0xe9, 0xf0, 0xf8, 0xff};
    constexpr std::int32_t kGreyLevels = 16;
    constexpr std::uint8_t kFirstVisibleLevel = 2;
    // Grey glyphs are scan-converted at four times the size and each pixel counts its 4 x 4 samples
    // that fall inside the outline: GDI's GGO_GRAY4 levels, 0 to 16.
    constexpr std::int32_t kOverscale = 4;
    // FreeType's loader asks its scan converter for high precision below 24 pixels per em only; GDI's
    // pixels at 24 and above are FreeType's with it as well (tests/gdifont).
    constexpr int kScanPrecision = FT_OUTLINE_HIGH_PRECISION;
    // A grey glyph larger than this is not drawn, so that a wild font size cannot ask for gigabytes
    // of samples. CD3DFont's scratch bitmap has 128 x 128 pixels.
    constexpr std::int32_t kLargestGreyGlyph = 4096;

    /// Every FreeType call and every registry's list run under this one lock: FreeType's library
    /// and faces are not thread-safe, and the fonts share the library. Never destroyed: the font
    /// renderer releases its fonts from an atexit handler.
    std::mutex& Lock()
    {
      static std::mutex* const mutex = new std::mutex;
      return *mutex;
    }

    /// FreeType's library, made on first use (under Lock()); the process keeps it.
    FT_Library Library()
    {
      static const FT_Library library = [] {
        FT_Library made = nullptr;
        if (FT_Init_FreeType(&made) != 0) {
          return static_cast<FT_Library>(nullptr);
        }
        // The TrueType interpreter as Windows' rasteriser runs it without ClearType, for bi-level
        // and grey text alike. FreeType's default (version 40) hints as ClearType does.
        FT_UInt version = TT_INTERPRETER_VERSION_35;
        (void)FT_Property_Set(made, "truetype", "interpreter-version", &version);
        return made;
      }();
      return library;
    }

    /// Windows' MulDiv: rounded half away from zero, -1 on a zero divisor or an overflow.
    [[nodiscard]] std::int32_t MulDiv(const std::int64_t value, const std::int64_t numerator, const std::int64_t denominator) noexcept
    {
      if (denominator == 0) {
        return -1;
      }
      const bool negate = denominator < 0;
      const std::int64_t divisor = negate ? -denominator : denominator;
      const std::int64_t product = negate ? -(value * numerator) : value * numerator;
      const std::int64_t result = (product < 0) ? -((divisor / 2 - product) / divisor) : (product + divisor / 2) / divisor;
      return (result > INT32_MAX || result < INT32_MIN) ? -1 : static_cast<std::int32_t>(result);
    }

    /// A table of the font, as FT_Load_Sfnt_Table reads it; empty when the font has none.
    [[nodiscard]] std::vector<FT_Byte> LoadTable(FT_Face const face, const FT_ULong tag)
    {
      FT_ULong length = 0;
      if (FT_Load_Sfnt_Table(face, tag, 0, nullptr, &length) != 0 || length == 0) {
        return {};
      }
      std::vector<FT_Byte> table(length);
      if (FT_Load_Sfnt_Table(face, tag, 0, table.data(), &length) != 0) {
        return {};
      }
      return table;
    }

    /// Big-endian reads within a table; outside it they read 0.
    class TableReader
    {
    public:
      explicit TableReader(const std::vector<FT_Byte>& bytes) noexcept : mBytes(bytes) {}

      [[nodiscard]] bool Has(const std::size_t at, const std::size_t count) const noexcept
      {
        return at <= mBytes.size() && count <= mBytes.size() - at;
      }
      [[nodiscard]] std::uint8_t U8(const std::size_t at) const noexcept
      {
        return Has(at, 1) ? mBytes[at] : 0;
      }
      [[nodiscard]] std::uint16_t U16(const std::size_t at) const noexcept
      {
        return Has(at, 2) ? static_cast<std::uint16_t>((mBytes[at] << 8) | mBytes[at + 1]) : 0;
      }
      [[nodiscard]] std::int16_t S16(const std::size_t at) const noexcept
      {
        return static_cast<std::int16_t>(U16(at));
      }

    private:
      const std::vector<FT_Byte>& mBytes;
    };

    /// GDI compares face names without case.
    [[nodiscard]] std::u16string Fold(std::u16string text)
    {
      for (char16_t& c : text) {
        if ((c >= u'A' && c <= u'Z') || (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7)) {
          c = static_cast<char16_t>(c + 0x20);
        }
      }
      return text;
    }

    /// CreateFontA's name: Windows-1252.
    [[nodiscard]] std::u16string FromAnsi(const std::string& text)
    {
      static constexpr std::array<char16_t, 32> kHigh{
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
      std::u16string result;
      for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        result.push_back((byte >= 0x80 && byte < 0xA0) ? kHigh[byte - 0x80] : static_cast<char16_t>(byte));
      }
      return result;
    }

    [[nodiscard]] std::string ToUtf8(const std::u16string& text)
    {
      std::string result;
      for (const char16_t c : text) {
        if (c < 0x80) {
          result.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
          result.push_back(static_cast<char>(0xC0 | (c >> 6)));
          result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
          result.push_back(static_cast<char>(0xE0 | (c >> 12)));
          result.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
          result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
      }
      return result;
    }

    /// The names of a face: its family (name ID 1, the family of up to four styles, not the
    /// typographic family), which GDI knows it by, and its full name (name ID 4), for the log. The
    /// Windows names in US English, then any English one; a font without Windows names has its
    /// Macintosh English names read.
    struct FaceNames
    {
      std::u16string family;
      std::u16string fullName;
    };

    [[nodiscard]] FaceNames ReadNames(FT_Face const face)
    {
      std::array<int, 2> bestRank{INT_MAX, INT_MAX};
      FaceNames names;
      const FT_UInt count = FT_Get_Sfnt_Name_Count(face);
      for (FT_UInt i = 0; i < count; ++i) {
        FT_SfntName name{};
        if (FT_Get_Sfnt_Name(face, i, &name) != 0) {
          continue;
        }
        int slot = -1;
        if (name.name_id == TT_NAME_ID_FONT_FAMILY) {
          slot = 0;
        } else if (name.name_id == TT_NAME_ID_FULL_NAME) {
          slot = 1;
        } else {
          continue;
        }
        int rank = -1;
        std::u16string text;
        if (name.platform_id == TT_PLATFORM_MICROSOFT &&
            (name.encoding_id == TT_MS_ID_UNICODE_CS || name.encoding_id == TT_MS_ID_SYMBOL_CS)) {
          if (name.language_id == TT_MS_LANGID_ENGLISH_UNITED_STATES) {
            rank = 0;
          } else if ((name.language_id & 0x3FF) == 0x09) {
            rank = 1;
          }
          for (FT_UInt at = 0; at + 1 < name.string_len; at += 2) {
            text.push_back(static_cast<char16_t>((name.string[at] << 8) | name.string[at + 1]));
          }
        } else if (name.platform_id == TT_PLATFORM_MACINTOSH && name.encoding_id == TT_MAC_ID_ROMAN &&
                   name.language_id == TT_MAC_LANGID_ENGLISH) {
          rank = 2;
          for (FT_UInt at = 0; at < name.string_len; ++at) {
            text.push_back(name.string[at] < 0x80 ? static_cast<char16_t>(name.string[at]) : u'?');
          }
        }
        if (rank >= 0 && rank < bestRank[slot] && !text.empty()) {
          bestRank[slot] = rank;
          (slot == 0 ? names.family : names.fullName) = std::move(text);
        }
      }
      if (names.family.empty() && face->family_name != nullptr) {
        names.family = FromAnsi(face->family_name);
      }
      if (names.fullName.empty()) {
        names.fullName = names.family;
      }
      return names;
    }

    /// The VDMX table's pixel extents for a request (Wine's load_VDMX): GDI's ascent and descent
    /// at the sizes the table lists, and for a cell height the size whose extents fit it.
    struct VdmxEntry
    {
      bool found = false;
      std::int32_t ppem = 0;
      std::int32_t yMax = 0;
      std::int32_t yMin = 0;
    };

    [[nodiscard]] VdmxEntry LoadVdmx(FT_Face const face, const std::int32_t height)
    {
      const std::vector<FT_Byte> table = LoadTable(face, TTAG_VDMX);
      const TableReader read(table);
      if (!read.Has(0, 6)) {
        return {};
      }
      const std::uint16_t ratioCount = read.U16(4);
      std::size_t group = 0;
      for (std::uint16_t i = 0; i < ratioCount; ++i) {
        const std::size_t ratio = 6 + 4u * i;
        // Like Windows, Wine skips the ratios for all characters (bCharSet 0). The screen's
        // aspect ratio is 1:1.
        if (read.U8(ratio) == 0) {
          continue;
        }
        const std::uint8_t xRatio = read.U8(ratio + 1);
        const std::uint8_t yStart = read.U8(ratio + 2);
        const std::uint8_t yEnd = read.U8(ratio + 3);
        if ((xRatio == 0 && yStart == 0 && yEnd == 0) || (xRatio == 1 && yStart <= 1 && yEnd >= 1)) {
          group = read.U16(6 + 4u * ratioCount + 2u * i);
          break;
        }
      }
      if (group == 0 || !read.Has(group, 4)) {
        return {};
      }
      const std::uint16_t recordCount = read.U16(group);
      const std::uint8_t startSize = read.U8(group + 2);
      const std::uint8_t endSize = read.U8(group + 3);
      const auto record = [&](const std::int32_t index) {
        const std::size_t at = group + 4 + 6u * static_cast<std::size_t>(index);
        return VdmxEntry{read.Has(at, 6), read.U16(at), read.S16(at + 2), read.S16(at + 4)};
      };
      if (height > 0) {
        for (std::int32_t i = 0; i < recordCount; ++i) {
          const VdmxEntry entry = record(i);
          if (!entry.found) {
            return {};
          }
          if (entry.yMax - entry.yMin == height) {
            return entry;
          }
          if (entry.yMax - entry.yMin > height) {
            return (i > 0) ? record(i - 1) : VdmxEntry{};
          }
        }
        return {};
      }
      const std::int32_t ppem = -height;
      if (ppem < startSize || ppem > endSize) {
        return {};
      }
      for (std::int32_t i = 0; i < recordCount; ++i) {
        const VdmxEntry entry = record(i);
        if (!entry.found || entry.ppem > ppem) {
          return {};
        }
        if (entry.ppem == ppem) {
          return entry;
        }
      }
      return {};
    }

    /// One glyph as GDI draws it: levels 0 (none) to 16 (the text colour), `top` rows above the
    /// baseline and `left` columns right of the pen.
    struct GlyphImage
    {
      std::int32_t left = 0;
      std::int32_t top = 0;
      std::int32_t width = 0;
      std::int32_t height = 0;
      std::vector<std::uint8_t> levels;
    };

    /// A grey glyph: its hinted outline at four times the size, scan-converted bi-level without
    /// dropout control, each pixel the number of its 16 samples inside.
    [[nodiscard]] bool RenderOverscaled(FT_Outline& outline, GlyphImage& image)
    {
      FT_BBox box{};
      FT_Outline_Get_CBox(&outline, &box);
      const FT_Pos x0 = (box.xMin & -64) / 64;
      const FT_Pos y0 = (box.yMin & -64) / 64;
      const FT_Pos x1 = ((box.xMax + 63) & -64) / 64;
      const FT_Pos y1 = ((box.yMax + 63) & -64) / 64;
      image = GlyphImage{};
      if (x1 <= x0 || y1 <= y0 || outline.n_points == 0) {
        return true;
      }
      if (x1 - x0 > kLargestGreyGlyph || y1 - y0 > kLargestGreyGlyph) {
        return false;
      }
      const auto width = static_cast<std::int32_t>(x1 - x0);
      const auto height = static_cast<std::int32_t>(y1 - y0);
      FT_Outline_Translate(&outline, -x0 * 64, -y0 * 64);
      FT_Matrix scale{kOverscale * 0x10000L, 0, 0, kOverscale * 0x10000L};
      FT_Outline_Transform(&outline, &scale);
      outline.flags = (outline.flags & ~(FT_OUTLINE_SMART_DROPOUTS | FT_OUTLINE_INCLUDE_STUBS)) | FT_OUTLINE_IGNORE_DROPOUTS |
        kScanPrecision;

      FT_Bitmap samples;
      FT_Bitmap_Init(&samples);
      samples.width = static_cast<unsigned int>(width * kOverscale);
      samples.rows = static_cast<unsigned int>(height * kOverscale);
      samples.pitch = static_cast<int>((samples.width + 7) / 8);
      samples.pixel_mode = FT_PIXEL_MODE_MONO;
      samples.num_grays = 2;
      std::vector<unsigned char> buffer(static_cast<std::size_t>(samples.pitch) * samples.rows, 0);
      samples.buffer = buffer.data();
      // A bi-level target: FreeType's B/W rasteriser, which lights a sample whose centre is inside.
      if (FT_Outline_Get_Bitmap(Library(), &outline, &samples) != 0) {
        return false;
      }

      image.left = static_cast<std::int32_t>(x0);
      image.top = static_cast<std::int32_t>(y1);
      image.width = width;
      image.height = height;
      image.levels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
      for (std::int32_t y = 0; y < height; ++y) {
        for (std::int32_t x = 0; x < width; ++x) {
          std::int32_t count = 0;
          for (std::int32_t sy = 0; sy < kOverscale; ++sy) {
            const unsigned char* const row = buffer.data() + static_cast<std::size_t>(y * kOverscale + sy) * samples.pitch;
            for (std::int32_t sx = 0; sx < kOverscale; ++sx) {
              const std::int32_t column = x * kOverscale + sx;
              count += (row[column >> 3] >> (7 - (column & 7))) & 1;
            }
          }
          image.levels[static_cast<std::size_t>(y) * width + x] = static_cast<std::uint8_t>(count);
        }
      }
      return true;
    }

    /// A rendered bitmap (bi-level, or an embedded grey one) as levels.
    void ImageFromBitmap(const FT_GlyphSlot slot, GlyphImage& image)
    {
      const FT_Bitmap& bitmap = slot->bitmap;
      image.left = slot->bitmap_left;
      image.top = slot->bitmap_top;
      image.width = static_cast<std::int32_t>(bitmap.width);
      image.height = static_cast<std::int32_t>(bitmap.rows);
      image.levels.assign(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height), 0);
      const int pitch = std::abs(bitmap.pitch);
      for (std::int32_t y = 0; y < image.height; ++y) {
        const std::int32_t row = (bitmap.pitch >= 0) ? y : (image.height - 1 - y);
        const unsigned char* const line = bitmap.buffer + static_cast<std::size_t>(row) * pitch;
        for (std::int32_t x = 0; x < image.width; ++x) {
          std::uint8_t level = 0;
          if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
            level = ((line[x >> 3] >> (7 - (x & 7))) & 1) ? kGreyLevels : 0;
          } else if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY && bitmap.num_grays > 1) {
            level = static_cast<std::uint8_t>(line[x] * (kGreyLevels + 1) / bitmap.num_grays);
          }
          image.levels[static_cast<std::size_t>(y) * image.width + x] = level;
        }
      }
    }

    /// One colour channel of an antialiased pixel, as Wine's dibdrv blends it to match Windows.
    [[nodiscard]] std::uint8_t BlendChannel(const std::int32_t destination, const std::int32_t text, const std::int32_t level) noexcept
    {
      const std::int32_t low = (kGreyRamp[level] * text) / 0xff;
      const std::int32_t high = kGreyRamp[kGreyLevels - level] + ((0xff - kGreyRamp[kGreyLevels - level]) * text) / 0xff;
      if (destination == text) {
        return static_cast<std::uint8_t>(destination);
      }
      if (destination > text) {
        return static_cast<std::uint8_t>(text + ((destination - text) * (high - text)) / (0xff - text));
      }
      return static_cast<std::uint8_t>(text - ((text - destination) * (text - low)) / text);
    }

    [[nodiscard]] Rect Intersect(const Rect& a, const Rect& b) noexcept
    {
      return Rect{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
    }
  } // namespace

  void FillSurface(const Surface& surface, const Rect& rect, const std::uint32_t colour)
  {
    const Rect area = Intersect(rect, Rect{0, 0, surface.width, surface.height});
    for (std::int32_t y = area.top; y < area.bottom; ++y) {
      std::uint8_t* pixel = surface.bits + static_cast<std::ptrdiff_t>(y) * surface.pitch + static_cast<std::ptrdiff_t>(area.left) * 4;
      for (std::int32_t x = area.left; x < area.right; ++x, pixel += 4) {
        pixel[0] = static_cast<std::uint8_t>(colour >> 16);
        pixel[1] = static_cast<std::uint8_t>(colour >> 8);
        pixel[2] = static_cast<std::uint8_t>(colour);
        pixel[3] = 0;
      }
    }
  }

  struct FontRegistry::Face
  {
    std::shared_ptr<const std::vector<FT_Byte>> bytes;
    std::string origin;
    FT_Long index = 0;
    std::u16string family; // folded
    std::string displayName;
    std::int32_t weight = 400;
    bool italic = false;
  };

  struct Font::State
  {
    std::shared_ptr<const FontRegistry::Face> face;
    FT_Face ft = nullptr;
    std::int32_t ppem = 0;
    bool symbol = false;
    bool grey = false;
    bool hintedAdvances = true;            // head: instructions may alter the advance widths
    bool deviceWidths = false;             // hdmx has this size
    std::vector<FT_Byte> linearThresholds; // LTSH
    TextMetrics metrics;
    std::unordered_map<char32_t, std::int32_t> advances;

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    ~State()
    {
      if (ft != nullptr) {
        FT_Done_Face(ft);
      }
    }

    [[nodiscard]] bool Open(std::shared_ptr<const FontRegistry::Face> chosen, const LogicalFont& logical);
    void ComputeMetrics(const LogicalFont& logical, const VdmxEntry& vdmx);
    [[nodiscard]] FT_UInt GlyphIndex(char32_t character) const;
    [[nodiscard]] std::int32_t Advance(char32_t character);
    [[nodiscard]] bool Render(FT_UInt glyph, GlyphImage& image) const;
  };

  bool Font::State::Open(std::shared_ptr<const FontRegistry::Face> chosen, const LogicalFont& logical)
  {
    face = std::move(chosen);
    FT_Library const library = Library();
    if (library == nullptr ||
        FT_New_Memory_Face(library, face->bytes->data(), static_cast<FT_Long>(face->bytes->size()), face->index, &ft) != 0) {
      ft = nullptr;
      return false;
    }
    if (FT_Select_Charmap(ft, FT_ENCODING_UNICODE) != 0) {
      symbol = FT_Select_Charmap(ft, FT_ENCODING_MS_SYMBOL) == 0;
    }

    // The size (Wine's select_font): the VDMX table's for the request, else from the height: the
    // em height itself, or for a cell height the em that gives the font's ascent and descent that
    // height. Height 0, GDI's default, is 20 pixels per em (tests/gdifont).
    const std::int32_t height = (logical.height != 0) ? logical.height : -20;
    const VdmxEntry vdmx = LoadVdmx(ft, height);
    if (vdmx.found) {
      ppem = vdmx.ppem;
    } else {
      if (height > 0) {
        const auto* const os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(ft, FT_SFNT_OS2));
        const auto* const hhea = static_cast<const TT_HoriHeader*>(FT_Get_Sfnt_Table(ft, FT_SFNT_HHEA));
        const std::int32_t winHeight =
          (os2 != nullptr) ? os2->usWinAscent + std::abs(static_cast<std::int16_t>(os2->usWinDescent)) : 0;
        if (winHeight != 0) {
          ppem = MulDiv(ft->units_per_EM, height, winHeight);
        } else if (hhea != nullptr && hhea->Ascender - hhea->Descender != 0) {
          ppem = MulDiv(ft->units_per_EM, height, hhea->Ascender - hhea->Descender);
        } else {
          ppem = height;
        }
      } else {
        ppem = -height;
      }
    }
    if (ppem < 1 || ppem > 0xFFFF) {
      ppem = 1;
    }
    if (FT_Set_Pixel_Sizes(ft, 0, static_cast<FT_UInt>(ppem)) != 0) {
      return false;
    }

    // Grey levels unless the request says otherwise or the font's 'gasp' table leaves them out at
    // this size: GDI draws bi-level there even for ANTIALIASED_QUALITY (Arial from 9 to 17 pixels).
    // Without a 'gasp' table GDI takes the same sizes as for Arial's: grey up to 8 and from 18 on.
    const FT_Int gasp = FT_Get_Gasp(ft, static_cast<FT_UInt>(ppem));
    const bool greySize = (gasp == FT_GASP_NO_TABLE) ? (ppem <= 8 || ppem >= 18) : (gasp & FT_GASP_DO_GRAY) != 0;
    grey = logical.quality != kNonAntialiasedQuality && greySize;

    // What the advance widths come from (Advance).
    const auto* const head = static_cast<const TT_Header*>(FT_Get_Sfnt_Table(ft, FT_SFNT_HEAD));
    hintedAdvances = head == nullptr || (head->Flags & 0x0010) != 0;
    linearThresholds = LoadTable(ft, TTAG_LTSH);
    if (linearThresholds.size() < 4) {
      linearThresholds.clear();
    }
    const std::vector<FT_Byte> deviceMetrics = LoadTable(ft, TTAG_hdmx);
    const TableReader readDevice(deviceMetrics);
    const std::int16_t recordCount = readDevice.S16(2);
    const std::size_t recordSize = (static_cast<std::size_t>(readDevice.U16(4)) << 16) | readDevice.U16(6);
    for (std::int16_t i = 0; i < recordCount && recordSize != 0 && readDevice.Has(8 + i * recordSize, 1); ++i) {
      if (readDevice.U8(8 + i * recordSize) == ppem) {
        deviceWidths = true;
      }
    }

    ComputeMetrics(logical, vdmx);
    return true;
  }

  void Font::State::ComputeMetrics(const LogicalFont& logical, const VdmxEntry& vdmx)
  {
    const auto* const os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(ft, FT_SFNT_OS2));
    const auto* const hhea = static_cast<const TT_HoriHeader*>(FT_Get_Sfnt_Table(ft, FT_SFNT_HHEA));
    const FT_Long unitsPerEm = ft->units_per_EM;
    const FT_Fixed emScale = MulDiv(ppem, 1 << 16, unitsPerEm);
    const auto scale = [emScale](const FT_Long value) { return static_cast<std::int32_t>(FT_MulFix(value, emScale)); };

    const std::int32_t hheaAscent = (hhea != nullptr) ? hhea->Ascender : ft->ascender;
    const std::int32_t hheaDescent = (hhea != nullptr) ? hhea->Descender : ft->descender;
    const std::int32_t lineGap = (hhea != nullptr) ? hhea->Line_Gap : 0;
    const std::int32_t winAscent = (os2 != nullptr) ? os2->usWinAscent : hheaAscent;
    const std::int32_t winDescent = (os2 != nullptr) ? std::abs(static_cast<std::int16_t>(os2->usWinDescent)) : -hheaDescent;

    TextMetrics& m = metrics;
    if (vdmx.found) {
      m.ascent = vdmx.yMax;
      m.descent = -vdmx.yMin;
    } else {
      m.ascent = scale(winAscent);
      m.descent = scale(winDescent);
    }
    m.height = m.ascent + m.descent;
    m.internalLeading = m.height - ppem;
    // What MSDN gives: the line gap less what the Windows extents add to the typographic ones.
    m.externalLeading = std::max(0, scale(lineGap - ((winAscent + winDescent) - (hheaAscent - hheaDescent))));
    m.aveCharWidth = (os2 != nullptr) ? scale(os2->xAvgCharWidth) : 0;
    if (m.aveCharWidth == 0) {
      m.aveCharWidth = 1;
    }
    m.maxCharWidth = scale(ft->bbox.xMax - ft->bbox.xMin);
    m.weight = (os2 != nullptr && os2->usWeightClass > 0) ? os2->usWeightClass : ((ft->style_flags & FT_STYLE_FLAG_BOLD) ? 700 : 400);
    m.overhang = 0;
    m.digitizedAspectX = 96;
    m.digitizedAspectY = 96;
    m.italic = (ft->style_flags & FT_STYLE_FLAG_ITALIC) ? 255 : 0;
    m.underlined = logical.underline ? 1 : 0;
    m.struckOut = logical.strikeOut ? 1 : 0;

    // The first and last characters of the character map, the break character from the OS/2
    // table and the default one before it (Wine's tests of GDI).
    FT_UInt glyph = 0;
    FT_ULong first = FT_Get_First_Char(ft, &glyph);
    FT_ULong last = first;
    for (FT_ULong code = first; glyph != 0; code = FT_Get_Next_Char(ft, code, &glyph)) {
      if (code <= 0xFFFF) {
        last = code;
      }
    }
    const std::uint16_t os2First = (os2 != nullptr) ? os2->usFirstCharIndex : static_cast<std::uint16_t>(first);
    m.firstChar = symbol ? 0 : static_cast<std::uint16_t>(first);
    m.lastChar = symbol ? 0xF0FF : static_cast<std::uint16_t>(last);
    m.breakChar = static_cast<std::uint16_t>((os2First <= 1) ? os2First + 2 : (os2First > 0xFF ? 0x20 : os2First));
    m.defaultChar = static_cast<std::uint16_t>(m.breakChar - 1);

    // TMPF_FIXED_PITCH means variable pitch; the family from the PANOSE classification (Wine).
    const bool fixedWidth = FT_IS_FIXED_WIDTH(ft) || (os2 != nullptr && os2->version != 0xFFFF && os2->panose[3] == 9);
    std::uint8_t pitch = fixedWidth ? 0 : kPitchVariable;
    const std::uint8_t panoseFamily = (os2 != nullptr) ? os2->panose[0] : 0;
    const std::uint8_t panoseSerif = (os2 != nullptr) ? os2->panose[1] : 0;
    if (panoseFamily == 3) {
      pitch |= kFamilyScript;
    } else if (panoseFamily == 4) {
      pitch |= kFamilyDecorative;
    } else if (fixedWidth) {
      pitch = kFamilyModern;
    } else if (panoseSerif >= 2 && panoseSerif <= 10) {
      pitch |= kFamilyRoman;
    } else if (panoseSerif >= 11 && panoseSerif <= 15) {
      pitch |= kFamilySwiss;
    }
    m.pitchAndFamily = static_cast<std::uint8_t>(pitch | kPitchVector | kPitchTrueType);

    // DEFAULT_CHARSET becomes the system's, the ANSI character set on a Western Windows.
    m.charSet = symbol ? kSymbolCharSet : (logical.charSet == kDefaultCharSet ? kAnsiCharSet : logical.charSet);
  }

  FT_UInt Font::State::GlyphIndex(const char32_t character) const
  {
    if (symbol && character < 0x100) {
      // A symbol font's characters are at U+F020 to U+F0FF; GDI maps the low ones there.
      if (const FT_UInt glyph = FT_Get_Char_Index(ft, 0xF000u | character); glyph != 0) {
        return glyph;
      }
    }
    return FT_Get_Char_Index(ft, character);
  }

  std::int32_t Font::State::Advance(const char32_t character)
  {
    if (const auto known = advances.find(character); known != advances.end()) {
      return known->second;
    }
    // In whole pixels, as GDI lays out text whatever the glyphs are drawn with: the 'hdmx' table's
    // where it has the size (FreeType's hinted load gives it), else scaled from the font's units
    // where the font says its instructions cannot change the advance, at this size ('LTSH') or at
    // all ('head'), else the hinted advance. FreeType's hinted advance can differ from the scaled
    // one where GDI does not hint (composite glyphs). Without instructions FreeType's auto-hinter
    // stays out: GDI does not hint such a font.
    const FT_UInt glyph = GlyphIndex(character);
    const bool linearSize = !linearThresholds.empty() && 4 + static_cast<std::size_t>(glyph) < linearThresholds.size() &&
                            ppem >= linearThresholds[4 + static_cast<std::size_t>(glyph)];
    std::int32_t advance = 0;
    FT_Fixed units = 0;
    if (!deviceWidths && (!hintedAdvances || linearSize) && FT_Get_Advance(ft, glyph, FT_LOAD_NO_SCALE, &units) == 0) {
      advance = MulDiv(units, ppem, ft->units_per_EM);
    } else if (FT_Load_Glyph(ft, glyph, FT_LOAD_NO_AUTOHINT | FT_LOAD_TARGET_MONO) == 0) {
      advance = static_cast<std::int32_t>((ft->glyph->advance.x + 32) >> 6);
    }
    advances.emplace(character, advance);
    return advance;
  }

  bool Font::State::Render(const FT_UInt glyph, GlyphImage& image) const
  {
    const FT_Int32 flags = FT_LOAD_NO_AUTOHINT | (grey ? (FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_BITMAP) : FT_LOAD_TARGET_MONO);
    if (FT_Load_Glyph(ft, glyph, flags) != 0) {
      return false;
    }
    FT_GlyphSlot const slot = ft->glyph;
    if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
      if (grey) {
        return RenderOverscaled(slot->outline, image);
      }
      slot->outline.flags |= kScanPrecision;
      if (FT_Render_Glyph(slot, FT_RENDER_MODE_MONO) != 0) {
        return false;
      }
    }
    if (slot->format != FT_GLYPH_FORMAT_BITMAP) {
      return false;
    }
    ImageFromBitmap(slot, image);
    return true;
  }

  Font::Font(std::unique_ptr<State> state) : mState(std::move(state)) {}

  Font::~Font()
  {
    const std::lock_guard<std::mutex> lock(Lock());
    mState.reset();
  }

  const std::string& Font::FaceName() const noexcept
  {
    return mState->face->displayName;
  }

  const std::string& Font::Origin() const noexcept
  {
    return mState->face->origin;
  }

  std::int32_t Font::PixelsPerEm() const noexcept
  {
    return mState->ppem;
  }

  bool Font::Antialiased() const noexcept
  {
    return mState->grey;
  }

  const TextMetrics& Font::Metrics() const noexcept
  {
    return mState->metrics;
  }

  std::vector<KerningPair> Font::KerningPairs() const
  {
    const std::lock_guard<std::mutex> lock(Lock());
    const State& state = *mState;
    const std::vector<FT_Byte> table = LoadTable(state.ft, TTAG_kern);
    const TableReader read(table);
    // Only the 'kern' table of OpenType (version 0); Apple's (version 1) is not GDI's.
    if (!read.Has(0, 4) || read.U16(0) != 0) {
      return {};
    }

    // Each glyph's character: the last that maps to it (GDI has U+037E for the semicolon), but
    // the space and the hyphen are not U+00A0 and U+00AD, which GDI takes as aliases of them.
    std::vector<std::uint16_t> characters(static_cast<std::size_t>(std::max<FT_Long>(state.ft->num_glyphs, 0)), 0);
    FT_UInt glyph = 0;
    for (FT_ULong code = FT_Get_First_Char(state.ft, &glyph); glyph != 0; code = FT_Get_Next_Char(state.ft, code, &glyph)) {
      if (code <= 0xFFFF && glyph < characters.size() && !((code == 0x00A0 || code == 0x00AD) && characters[glyph] != 0)) {
        characters[glyph] = static_cast<std::uint16_t>(code);
      }
    }
    const auto character = [&characters](const std::uint16_t glyphIndex) -> std::uint16_t {
      return glyphIndex < characters.size() ? characters[glyphIndex] : 0;
    };

    const std::int32_t ppem = state.ppem;
    const std::int32_t unitsPerEm = state.ft->units_per_EM;
    std::vector<KerningPair> pairs;
    std::size_t offset = 4;
    const std::uint16_t tableCount = read.U16(2);
    for (std::uint16_t t = 0; t < tableCount && read.Has(offset, 6); ++t) {
      const std::uint16_t length = read.U16(offset + 2);
      const std::uint16_t coverage = read.U16(offset + 4);
      // Format 0, the one Windows reads.
      if ((coverage >> 8) == 0 && read.Has(offset + 6, 8)) {
        const std::uint16_t pairCount = read.U16(offset + 6);
        for (std::uint16_t i = 0; i < pairCount; ++i) {
          const std::size_t at = offset + 14 + 6u * i;
          if (!read.Has(at, 6)) {
            break;
          }
          pairs.push_back(KerningPair{character(read.U16(at)), character(read.U16(at + 2)), MulDiv(read.S16(at + 4), ppem, unitsPerEm)});
        }
      }
      if (length == 0) {
        break;
      }
      offset += length;
    }
    return pairs;
  }

  bool Font::HasCharacter(const char32_t character) const
  {
    const std::lock_guard<std::mutex> lock(Lock());
    return mState->GlyphIndex(character) != 0;
  }

  std::int32_t Font::Advance(const char32_t character) const
  {
    const std::lock_guard<std::mutex> lock(Lock());
    return mState->Advance(character);
  }

  void Font::Draw(
    const Surface& surface, const Rect& clip, const std::int32_t x, const std::int32_t baseline, const char32_t* const text,
    const std::size_t length, const std::uint32_t textColour, const std::uint32_t backColour, const bool opaque
  ) const
  {
    const std::lock_guard<std::mutex> lock(Lock());
    State& state = *mState;
    const Rect bounds = Intersect(clip, Rect{0, 0, surface.width, surface.height});
    if (surface.bits == nullptr || bounds.left >= bounds.right || bounds.top >= bounds.bottom || text == nullptr) {
      return;
    }
    if (opaque) {
      std::int32_t width = 0;
      for (std::size_t i = 0; i < length; ++i) {
        width += state.Advance(text[i]);
      }
      const Rect cells{x, baseline - state.metrics.ascent, x + width, baseline + state.metrics.descent};
      FillSurface(surface, Intersect(bounds, cells), backColour);
    }

    const std::int32_t textChannels[3] = {
      static_cast<std::int32_t>((textColour >> 16) & 0xFF), static_cast<std::int32_t>((textColour >> 8) & 0xFF),
      static_cast<std::int32_t>(textColour & 0xFF)};
    std::int32_t pen = x;
    GlyphImage image;
    for (std::size_t i = 0; i < length; ++i) {
      if (state.Render(state.GlyphIndex(text[i]), image)) {
        for (std::int32_t row = 0; row < image.height; ++row) {
          const std::int32_t py = baseline - image.top + row;
          if (py < bounds.top || py >= bounds.bottom) {
            continue;
          }
          for (std::int32_t column = 0; column < image.width; ++column) {
            const std::int32_t px = pen + image.left + column;
            const std::uint8_t level = image.levels[static_cast<std::size_t>(row) * image.width + column];
            if (level < kFirstVisibleLevel || px < bounds.left || px >= bounds.right) {
              continue;
            }
            std::uint8_t* const pixel = surface.bits + static_cast<std::ptrdiff_t>(py) * surface.pitch + static_cast<std::ptrdiff_t>(px) * 4;
            for (int channel = 0; channel < 3; ++channel) {
              pixel[channel] = BlendChannel(pixel[channel], textChannels[channel], level);
            }
            pixel[3] = 0;
          }
        }
      }
      pen += state.Advance(text[i]);
    }
  }

  FontRegistry::FontRegistry() = default;

  FontRegistry::~FontRegistry()
  {
    const std::lock_guard<std::mutex> lock(Lock());
    mFaces.clear();
  }

  std::size_t FontRegistry::Add(const void* const data, const std::size_t size, const std::string& origin)
  {
    if (data == nullptr || size == 0 || size > static_cast<std::size_t>(LONG_MAX)) {
      return 0;
    }
    const auto* const begin = static_cast<const FT_Byte*>(data);
    auto bytes = std::make_shared<const std::vector<FT_Byte>>(begin, begin + size);

    const std::lock_guard<std::mutex> lock(Lock());
    FT_Library const library = Library();
    if (library == nullptr) {
      return 0;
    }
    FT_Face probe = nullptr;
    if (FT_New_Memory_Face(library, bytes->data(), static_cast<FT_Long>(bytes->size()), -1, &probe) != 0) {
      return 0;
    }
    const FT_Long faceCount = probe->num_faces;
    FT_Done_Face(probe);

    std::size_t added = 0;
    for (FT_Long index = 0; index < faceCount; ++index) {
      FT_Face face = nullptr;
      if (FT_New_Memory_Face(library, bytes->data(), static_cast<FT_Long>(bytes->size()), index, &face) != 0) {
        continue;
      }
      if (FT_IS_SFNT(face) && FT_IS_SCALABLE(face)) {
        const FaceNames names = ReadNames(face);
        auto record = std::make_shared<Face>();
        record->bytes = bytes;
        record->origin = origin;
        record->index = index;
        record->family = Fold(names.family);
        record->displayName = ToUtf8(names.fullName);
        const auto* const os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
        record->weight = (os2 != nullptr && os2->usWeightClass > 0) ? os2->usWeightClass
                                                                     : ((face->style_flags & FT_STYLE_FLAG_BOLD) ? 700 : 400);
        record->italic = (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
        mFaces.push_back(std::move(record));
        ++added;
      }
      FT_Done_Face(face);
    }
    return added;
  }

  bool FontRegistry::Empty() const
  {
    const std::lock_guard<std::mutex> lock(Lock());
    return mFaces.empty();
  }

  std::shared_ptr<Font> FontRegistry::Realize(const LogicalFont& logical) const
  {
    const std::lock_guard<std::mutex> lock(Lock());
    if (mFaces.empty()) {
      return nullptr;
    }
    const std::u16string wanted = Fold(FromAnsi(logical.faceName)).substr(0, 31); // LF_FACESIZE
    const std::int32_t weight = (logical.weight > 0) ? logical.weight : 400;
    const auto byFamily = [&](const std::u16string& family) {
      std::shared_ptr<const Face> best;
      std::int32_t bestPenalty = INT32_MAX;
      for (const auto& face : mFaces) {
        if (face->family != family) {
          continue;
        }
        const std::int32_t penalty = (face->italic != logical.italic ? 100000 : 0) + std::abs(face->weight - weight);
        if (penalty < bestPenalty) {
          best = face;
          bestPenalty = penalty;
        }
      }
      return best;
    };

    // GDI goes by the family name alone: "Arial Bold", a full name, is unknown to it. For a name it
    // does not know it takes Arial at most sizes and the bitmap font MS Sans Serif where that has
    // the size exactly (16, 18, 24 pixels ...), which Android does not have.
    std::shared_ptr<const Face> chosen = byFamily(wanted);
    if (chosen == nullptr) {
      chosen = byFamily(u"arial");
    }
    if (chosen == nullptr) {
      chosen = mFaces.front();
    }
    auto state = std::make_unique<Font::State>();
    if (!state->Open(chosen, logical)) {
      return nullptr;
    }
    return std::make_shared<Font>(std::move(state));
  }
} // namespace faf::android::gdi
