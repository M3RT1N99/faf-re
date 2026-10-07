#pragma once

// FxLexer: the effect source as preprocessing tokens (C translation phases
// 1-3). Line splices ("\" before a newline) are removed, comments become
// whitespace, and every logical end of line is a Newline token so the
// preprocessor can find its directives. Tokens carry their merged-buffer
// position (FxSource.h) for diagnostics.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gpg/gal/fx/FxSource.h"

namespace gpg::gal::fx {

  enum class TokenKind : std::uint8_t
  {
    Identifier,
    Number, // a C pp-number: 1, 0x7F, 1.0f, .5e-3, 1.5h
    String, // "..." including the quotes
    CharLiteral, // '...' including the quotes
    Punct, // operators and separators, longest match
    Newline, // end of a logical line (only from the lexer; the preprocessor drops them)
    Other, // any other single byte
    End
  };

  /// Names of macros a token must not be expanded through again (the hide set
  /// of Prosser's algorithm). Shared and immutable.
  using HideSet = std::shared_ptr<const std::vector<std::string>>;

  struct Token
  {
    TokenKind kind = TokenKind::End;
    std::string text;
    SourceLocation where;
    bool leadingSpace = false; // whitespace or a comment before it on the same line
    bool lineStart = false; // first token of a logical line
    HideSet hide;

    [[nodiscard]] bool Is(const TokenKind k, const std::string_view t) const { return kind == k && text == t; }
    [[nodiscard]] bool IsPunct(const std::string_view t) const { return kind == TokenKind::Punct && text == t; }
    [[nodiscard]] bool IsIdent(const std::string_view t) const { return kind == TokenKind::Identifier && text == t; }
  };

  /// Lexes `text` (normally SourceBuffer::Text()). The result ends with a
  /// Newline (if the text does not end in one) and an End token. Unterminated
  /// comments and strings are reported in `diagnostics` and closed at the end
  /// of the text.
  [[nodiscard]] std::vector<Token> LexText(std::string_view text, std::vector<Diagnostic>& diagnostics);

  /// Lexes a short snippet (a macro definition from the command line, a pasted
  /// token) without Newline/End tokens. All tokens get location `where`.
  [[nodiscard]] std::vector<Token> LexFragment(std::string_view text, SourceLocation where);

  /// Joins tokens with single spaces where the source had whitespace.
  [[nodiscard]] std::string SpellTokens(const std::vector<Token>& tokens, std::size_t begin, std::size_t end);

} // namespace gpg::gal::fx
