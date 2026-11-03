#pragma once

// GDI's TrueType fonts on FreeType, for the Android build's GDI font functions
// (GdiAndroid.cpp): the font renderer (moho/render/d3d/CD3DFont.cpp) creates its fonts with
// CreateFontA, reads their metrics and kerning pairs, measures each character with DrawTextW and
// draws it into a 32-bit bitmap. This is the part of GDI behind those calls: the fonts registered
// from memory (AddFontMemResourceEx), CreateFont's choice of a face and its sizes, TEXTMETRICW and
// the kerning pairs as GDI computes them from the font's tables, the advance widths of its hinted
// glyphs, and the glyphs as GDI draws them: bi-level or with its 17 grey levels, as the font's
// 'gasp' table asks for the size.
//
// Nothing here needs Windows: the Windows compare tool (tests/gdifont) builds this file next to the
// real GDI and checks it against GDI.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace faf::android::gdi
{
  /// CreateFont's request, the LOGFONT fields that choose and size a font.
  struct LogicalFont
  {
    /// Below 0 the em height in pixels (the character height), above 0 the cell height, 0 GDI's
    /// default (20 pixels per em).
    std::int32_t height = 0;
    std::int32_t weight = 0; ///< FW_*; 0 for any (as FW_NORMAL)
    bool italic = false;
    bool underline = false;
    bool strikeOut = false;
    std::uint8_t charSet = 1; ///< DEFAULT_CHARSET
    std::uint8_t quality = 0; ///< *_QUALITY
    std::string faceName;     ///< CreateFontA's face name, in the ANSI code page (Windows-1252)
  };

  /// TEXTMETRICW's fields.
  struct TextMetrics
  {
    std::int32_t height = 0;
    std::int32_t ascent = 0;
    std::int32_t descent = 0;
    std::int32_t internalLeading = 0;
    std::int32_t externalLeading = 0;
    std::int32_t aveCharWidth = 0;
    std::int32_t maxCharWidth = 0;
    std::int32_t weight = 0;
    std::int32_t overhang = 0;
    std::int32_t digitizedAspectX = 0;
    std::int32_t digitizedAspectY = 0;
    std::uint16_t firstChar = 0;
    std::uint16_t lastChar = 0;
    std::uint16_t defaultChar = 0;
    std::uint16_t breakChar = 0;
    std::uint8_t italic = 0;
    std::uint8_t underlined = 0;
    std::uint8_t struckOut = 0;
    std::uint8_t pitchAndFamily = 0;
    std::uint8_t charSet = 0;
  };

  /// KERNINGPAIR: two UTF-16 characters and the kerning between them, in pixels.
  struct KerningPair
  {
    std::uint16_t first = 0;
    std::uint16_t second = 0;
    std::int32_t amount = 0;
  };

  /// A 32-bit bitmap: rows of blue, green, red and an unused byte, the top row first.
  struct Surface
  {
    std::uint8_t* bits = nullptr;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t pitch = 0;
  };

  /// Right and bottom exclusive, as GDI's RECT.
  struct Rect
  {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
  };

  /// FillRect's pixels: `rect`, as far as it is on the surface, in `colour` (COLORREF).
  void FillSurface(const Surface& surface, const Rect& rect, std::uint32_t colour);

  /// A font as GDI realizes one: a face at a size. Thread-safe.
  class Font
  {
  public:
    struct State;

    explicit Font(std::unique_ptr<State> state);
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    /// The face's full name and where its file came from, for reports.
    [[nodiscard]] const std::string& FaceName() const noexcept;
    [[nodiscard]] const std::string& Origin() const noexcept;
    [[nodiscard]] std::int32_t PixelsPerEm() const noexcept;
    /// Whether the glyphs are drawn with grey levels at this size (else bi-level).
    [[nodiscard]] bool Antialiased() const noexcept;

    [[nodiscard]] const TextMetrics& Metrics() const noexcept;
    /// GetKerningPairsW's pairs, in the order of the font's 'kern' table.
    [[nodiscard]] std::vector<KerningPair> KerningPairs() const;
    /// Whether the face has the character. GDI takes one it lacks from a linked font where the
    /// system links one; here it is the face's missing-glyph box.
    [[nodiscard]] bool HasCharacter(char32_t character) const;
    /// The advance width of a character in pixels, as GetTextExtentPoint32W measures it.
    [[nodiscard]] std::int32_t Advance(char32_t character) const;
    /// ExtTextOutW's glyphs: the text from the pen position `x` on the baseline `baseline`, inside
    /// `clip`, in `textColour` (COLORREF, 0x00BBGGRR). When `opaque`, the characters' cells are
    /// filled with `backColour` first, as in GDI's OPAQUE background mode. The font's underline and
    /// strike-out are in its metrics only: they are not drawn.
    void Draw(
      const Surface& surface, const Rect& clip, std::int32_t x, std::int32_t baseline, const char32_t* text,
      std::size_t length, std::uint32_t textColour, std::uint32_t backColour, bool opaque
    ) const;

  private:
    std::unique_ptr<State> mState;
  };

  /// Fonts registered from memory, as AddFontMemResourceEx registers them for a process, and
  /// CreateFont's choice among them. Thread-safe.
  class FontRegistry
  {
  public:
    struct Face;

    FontRegistry();
    ~FontRegistry();
    FontRegistry(const FontRegistry&) = delete;
    FontRegistry& operator=(const FontRegistry&) = delete;

    /// The faces of a TrueType or OpenType file or collection (the data is copied); the number of
    /// faces added, 0 when the data is no font.
    std::size_t Add(const void* data, std::size_t size, const std::string& origin);
    [[nodiscard]] bool Empty() const;
    /// The font CreateFont gives for `logical`: the face of that family name with the closest
    /// style, else GDI's fallback, Arial's (else the first face). Null when no face is registered.
    [[nodiscard]] std::shared_ptr<Font> Realize(const LogicalFont& logical) const;

  private:
    std::vector<std::shared_ptr<const Face>> mFaces;
  };
} // namespace faf::android::gdi
