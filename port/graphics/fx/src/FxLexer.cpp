#include "gpg/gal/fx/FxLexer.h"

#include <array>
#include <cstring>

namespace gpg::gal::fx {

  namespace {

    // Phase 2 output: the text with line splices removed, plus the merged
    // position of every remaining byte.
    struct LogicalText
    {
      std::string chars;
      std::vector<SourceLocation> where;
    };

    LogicalText RemoveLineSplices(const std::string_view text)
    {
      LogicalText out;
      out.chars.reserve(text.size());
      out.where.reserve(text.size() + 1);
      std::uint32_t line = 1;
      std::uint32_t column = 1;
      std::size_t i = 0;
      while (i < text.size()) {
        const char c = text[i];
        if (c == '\\') {
          // "\" + LF or "\" + CR LF joins two physical lines.
          if (i + 1 < text.size() && text[i + 1] == '\n') {
            i += 2;
            ++line;
            column = 1;
            continue;
          }
          if (i + 2 < text.size() && text[i + 1] == '\r' && text[i + 2] == '\n') {
            i += 3;
            ++line;
            column = 1;
            continue;
          }
        }
        out.chars.push_back(c);
        out.where.push_back({line, column});
        if (c == '\n') {
          ++line;
          column = 1;
        } else {
          ++column;
        }
        ++i;
      }
      out.where.push_back({line, column});
      return out;
    }

    bool IsIdentStart(const char c)
    {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    bool IsDigit(const char c)
    {
      return c >= '0' && c <= '9';
    }

    bool IsIdentChar(const char c)
    {
      return IsIdentStart(c) || IsDigit(c);
    }

    // Longest match first.
    constexpr std::array<const char*, 31> kPunctuators = {
      "<<=", ">>=", "...", "##", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "++", "--", "+=", "-=",
      "*=",  "/=",  "%=",  "&=", "|=", "^=", "->", "::", "{",  "}",  "[",  "]",  "(",  ")",  ";",
    };
    constexpr const char* kSinglePunct = ":,.?~!+-*/%^&|=<>#";

    class Lexer
    {
    public:
      Lexer(const LogicalText& text, std::vector<Diagnostic>* diagnostics, const bool fragment)
        : mText(text)
        , mDiagnostics(diagnostics)
        , mFragment(fragment)
      {}

      std::vector<Token> Run()
      {
        std::vector<Token> tokens;
        bool lineStart = true;
        bool leadingSpace = false;
        const std::string& s = mText.chars;
        std::size_t i = 0;
        while (i < s.size()) {
          const char c = s[i];
          if (c == '\n') {
            if (!mFragment) {
              Token newline;
              newline.kind = TokenKind::Newline;
              newline.where = mText.where[i];
              tokens.push_back(std::move(newline));
            }
            lineStart = true;
            leadingSpace = false;
            ++i;
            continue;
          }
          if (c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f') {
            leadingSpace = true;
            ++i;
            continue;
          }
          if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n') {
              ++i;
            }
            leadingSpace = true;
            continue;
          }
          if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            const std::size_t open = i;
            i += 2;
            while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) {
              ++i;
            }
            if (i + 1 >= s.size()) {
              Report(mText.where[open], "unterminated comment");
              i = s.size();
            } else {
              i += 2;
            }
            // A block comment is one space, even across lines (C phase 3).
            leadingSpace = true;
            continue;
          }

          Token token;
          token.where = mText.where[i];
          token.leadingSpace = leadingSpace;
          token.lineStart = lineStart;
          const std::size_t start = i;
          if (IsIdentStart(c)) {
            while (i < s.size() && IsIdentChar(s[i])) {
              ++i;
            }
            token.kind = TokenKind::Identifier;
          } else if (IsDigit(c) || (c == '.' && i + 1 < s.size() && IsDigit(s[i + 1]))) {
            // pp-number: digit or .digit, then [0-9A-Za-z_.] and e+/e-/p+/p-.
            ++i;
            while (i < s.size()) {
              const char d = s[i];
              if ((d == '+' || d == '-') && (s[i - 1] == 'e' || s[i - 1] == 'E' || s[i - 1] == 'p' || s[i - 1] == 'P')) {
                ++i;
              } else if (IsIdentChar(d) || d == '.') {
                ++i;
              } else {
                break;
              }
            }
            token.kind = TokenKind::Number;
          } else if (c == '"' || c == '\'') {
            ++i;
            while (i < s.size() && s[i] != c && s[i] != '\n') {
              if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] != '\n') {
                ++i;
              }
              ++i;
            }
            if (i < s.size() && s[i] == c) {
              ++i;
            } else {
              Report(token.where, (c == '"') ? "unterminated string" : "unterminated character constant");
            }
            token.kind = (c == '"') ? TokenKind::String : TokenKind::CharLiteral;
          } else {
            std::size_t length = 0;
            for (const char* punct : kPunctuators) {
              const std::size_t n = std::strlen(punct);
              if (s.compare(i, n, punct) == 0) {
                length = n;
                break;
              }
            }
            if (length == 0 && std::strchr(kSinglePunct, c) != nullptr) {
              length = 1;
            }
            if (length > 0) {
              token.kind = TokenKind::Punct;
              i += length;
            } else {
              token.kind = TokenKind::Other;
              ++i;
            }
          }
          token.text.assign(s, start, i - start);
          tokens.push_back(std::move(token));
          lineStart = false;
          leadingSpace = false;
        }
        if (!mFragment) {
          if (tokens.empty() || tokens.back().kind != TokenKind::Newline) {
            Token newline;
            newline.kind = TokenKind::Newline;
            newline.where = mText.where.back();
            tokens.push_back(std::move(newline));
          }
          Token end;
          end.kind = TokenKind::End;
          end.where = mText.where.back();
          end.lineStart = true;
          tokens.push_back(std::move(end));
        }
        return tokens;
      }

    private:
      void Report(const SourceLocation where, std::string message)
      {
        if (mDiagnostics != nullptr) {
          mDiagnostics->push_back({Diagnostic::Severity::Error, where, std::move(message)});
        }
      }

      const LogicalText& mText;
      std::vector<Diagnostic>* mDiagnostics;
      bool mFragment;
    };

  } // namespace

  std::vector<Token> LexText(const std::string_view text, std::vector<Diagnostic>& diagnostics)
  {
    const LogicalText logical = RemoveLineSplices(text);
    return Lexer(logical, &diagnostics, false).Run();
  }

  std::vector<Token> LexFragment(const std::string_view text, const SourceLocation where)
  {
    LogicalText logical = RemoveLineSplices(text);
    for (SourceLocation& position : logical.where) {
      position = where;
    }
    return Lexer(logical, nullptr, true).Run();
  }

  std::string SpellTokens(const std::vector<Token>& tokens, const std::size_t begin, const std::size_t end)
  {
    std::string text;
    for (std::size_t i = begin; i < end && i < tokens.size(); ++i) {
      if (i > begin && tokens[i].leadingSpace) {
        text += ' ';
      }
      text += tokens[i].text;
    }
    return text;
  }

} // namespace gpg::gal::fx
