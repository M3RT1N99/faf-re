#include "gpg/gal/fx/FxSource.h"

#include <utility>

namespace gpg::gal::fx {

  void SourceBuffer::Append(std::string name, const std::string_view text)
  {
    Part part;
    part.name = std::move(name);
    part.firstLine = 1;
    part.firstColumn = 1;
    for (const char c : mText) {
      if (c == '\n') {
        ++part.firstLine;
        part.firstColumn = 1;
      } else {
        ++part.firstColumn;
      }
    }
    mParts.push_back(std::move(part));
    mText.append(text.data(), text.size());
  }

  std::string SourceBuffer::Describe(const SourceLocation where) const
  {
    if (where.line == 0) {
      return "<built-in>";
    }
    const Part* owner = nullptr;
    for (const Part& part : mParts) {
      if (part.firstLine < where.line || (part.firstLine == where.line && part.firstColumn <= where.column)) {
        owner = &part;
      }
    }
    if (owner == nullptr) {
      return "(" + std::to_string(where.line) + "," + std::to_string(where.column) + ")";
    }
    const std::uint32_t localLine = where.line - owner->firstLine + 1;
    const std::uint32_t localColumn =
      (where.line == owner->firstLine) ? where.column - owner->firstColumn + 1 : where.column;
    std::string text = owner->name + "(" + std::to_string(localLine) + "," + std::to_string(localColumn) + ")";
    if (mParts.size() > 1) {
      // D3DX numbers lines in the merged buffer; print that too so its error
      // messages can be matched up.
      text += " [merged line " + std::to_string(where.line) + "]";
    }
    return text;
  }

  std::string FormatDiagnostics(const SourceBuffer& source, const std::vector<Diagnostic>& diagnostics)
  {
    std::string text;
    for (const Diagnostic& diagnostic : diagnostics) {
      text += source.Describe(diagnostic.where);
      text += (diagnostic.severity == Diagnostic::Severity::Error) ? ": error: " : ": warning: ";
      text += diagnostic.message;
      text += '\n';
    }
    return text;
  }

} // namespace gpg::gal::fx
