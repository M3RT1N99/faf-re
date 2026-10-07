#pragma once

// Source text and diagnostics for the effect front end.
//
// The engine compiles one buffer per effect: /effects/d3d9states.compat
// followed byte for byte by the .fx file (CD3DEffect::InitEffectFromFile,
// src/sdk/moho/render/d3d/CD3DEffectTechnique.cpp:459-476). D3DX therefore
// counts lines across both files and reports "(line,col)" in the merged
// buffer. SourceBuffer keeps that merged text and remembers where each input
// starts, so a diagnostic can name both the merged line (what D3DX prints) and
// the file and line a person would open.

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace gpg::gal::fx {

  /// A position in the merged buffer. Lines and columns start at 1; line 0
  /// means "no position" (built-in macros, the command line).
  struct SourceLocation
  {
    std::uint32_t line = 0;
    std::uint32_t column = 0;
  };

  class SourceBuffer
  {
  public:
    /// Appends one input verbatim. No separator is inserted: like the engine's
    /// memcpy, a file without a final newline runs into the next one.
    void Append(std::string name, std::string_view text);

    [[nodiscard]] const std::string& Text() const { return mText; }

    /// "name(line,col) [merged line N]" for a merged-buffer location.
    [[nodiscard]] std::string Describe(SourceLocation where) const;

  private:
    struct Part
    {
      std::string name;
      std::uint32_t firstLine = 1; // merged line on which this part starts
      std::uint32_t firstColumn = 1; // column of its first byte on that line
    };

    std::string mText;
    std::vector<Part> mParts;
  };

  struct Diagnostic
  {
    enum class Severity
    {
      Warning,
      Error
    };

    Severity severity = Severity::Error;
    SourceLocation where;
    std::string message;
  };

  /// Thrown inside the front end; FrontEnd entry points catch it and turn it
  /// into an error Diagnostic.
  class FxError : public std::runtime_error
  {
  public:
    FxError(SourceLocation where, const std::string& message)
      : std::runtime_error(message)
      , mWhere(where)
    {}

    [[nodiscard]] SourceLocation Where() const { return mWhere; }

  private:
    SourceLocation mWhere;
  };

  /// Formats diagnostics as "file(line,col): error: message" lines.
  [[nodiscard]] std::string FormatDiagnostics(const SourceBuffer& source, const std::vector<Diagnostic>& diagnostics);

} // namespace gpg::gal::fx
