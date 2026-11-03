// d3dcompiler_47's preprocessor (FxcPreprocessor.h). What it does, measured on d3dcompiler_47
// 10.0.26100.9711 (tests/fxc runs both):
//
// The output.
//   - Every token is followed by one space. A line is indented by four spaces for each `{` open
//     before it (a `}` that begins a line counts as closed), a blank line too; the depth does not
//     go below zero.
//   - The first line is `#line <line> "<path>"`, with the line of the first output. Each source line
//     that ends in an active group writes a newline, but after two in a row without a token or a
//     directive between them the rest are dropped. When the next output belongs to another line
//     than the one being written, `#line <line>` comes first, on a line of its own.
//   - #line N without a file changes the numbering with two newlines; with a file it writes
//     `#line N-1 "<file>"` at once. #pragma pack_matrix(...) waits for the next token and comes on
//     a line of its own before it, outside the numbering, which `#line` mends at the next newline;
//     an unknown pragma does the same with nothing written. #pragma warning and #pragma def go to
//     the top of the output, before everything (defs first, warnings by number); #pragma message
//     goes to the messages.
//
// Tokens.
//   - Numbers are literals, not C's preprocessing numbers: `1.0.0` is `1.0` and `.0`, `0x` is `0`
//     and `x`, `08` is `0` and `8`, `1e` is `1` and `e`. Hexadecimal has a lower-case `0x`; floats
//     take one suffix of f, h or l (either case); integers u, l, ul or lu.
//   - Identifiers join across `::` (`a::b`, `::a`). Strings and character literals as in C, but an
//     unterminated string takes the newline in and does not count the line. No digraphs.
//   - A backslash before a newline continues a directive; in text it is a newline, and in a line
//     comment it continues the comment and writes the newline. Comments are space.
//
// Macros.
//   - Arguments are expanded before # and ## see them: `#x` stringizes the expanded argument, and
//     `##` joins the spellings of the expanded neighbours, which are then read as tokens again.
//     `##` at either end of a body and `#` in an object-like one are tokens; `#` before an
//     identifier that is no parameter stringizes the identifier.
//   - A macro that would expand within its own expansion is not expanded at all: the invocation is
//     written as it stands, from the outermost one in the text or, inside the argument of one, from
//     the outermost one in that argument.
//   - A function-like macro needs `(` after spaces or tabs on its line; a comment or a newline
//     between them makes the name plain text. An empty argument, too few or too many make an error
//     and leave the name alone. There are no parameterless and no variadic macros.
//
// Conditions.
//   - #if computes in 32-bit unsigned arithmetic with ?: || && | ^ & == != < > <= >= + - * / and
//     the unary ! - +, but no shifts, %, ~ or commas; integers must fit 32 bits, floats are errors.
//     An error in the expression drops the directive (its group is not opened); tokens after a
//     whole expression are an error, and its value counts.
//   - A conditional directive's newline is a directive's when a group is active after it, and a
//     blank line's when it ends an active group, but not as the first output.
//
// Errors are reported with d3dcompiler_47's wording and position, but processing goes on (and the
// call succeeds), except for #error and #include, which end it.

#include "FxcPreprocessor.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace faf::android::fxc
{
  namespace
  {
    enum class Kind : std::uint8_t
    {
      Identifier,
      Number,
      String,
      Char,
      Punct,
      Newline,
      End,
    };

    struct Token
    {
      Kind kind = Kind::End;
      std::string text;
      int line = 0;
      int column = 0;     // 1-based, of the first character
      int lastColumn = 0; // of the last
      std::size_t offset = 0;
      bool lineStart = false;     // the first token of its line
      bool commentBefore = false; // a comment separates it from the token before
      bool painted = false;       // written unexpanded after a recursion: never expanded
      bool isFloat = false;       // a number with a fraction or an exponent
      bool soft = false;          // a newline inside a block comment
    };

    [[nodiscard]] bool IsPunct(const Token& token, const std::string_view text)
    {
      return token.kind == Kind::Punct && token.text == text;
    }

    [[nodiscard]] bool IsDigit(const char c)
    {
      return c >= '0' && c <= '9';
    }

    [[nodiscard]] bool IsHexDigit(const char c)
    {
      return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    [[nodiscard]] bool IsIdentifierStart(const char c)
    {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    [[nodiscard]] bool IsIdentifierChar(const char c)
    {
      return IsIdentifierStart(c) || IsDigit(c);
    }

    /** `"` and `\` escaped, for a string literal. */
    [[nodiscard]] std::string Escape(const std::string_view text)
    {
      std::string escaped;
      escaped.reserve(text.size());
      for (const char c : text) {
        if (c == '\\' || c == '"') {
          escaped += '\\';
        }
        escaped += c;
      }
      return escaped;
    }

    // --- Messages --------------------------------------------------------------------------------
    class Messages
    {
    public:
      explicit Messages(const std::string_view path)
        : mPath(path)
      {}

      void Error(const int line, const int column, const int lastColumn, const std::string_view code, const std::string_view text)
      {
        Add(line, column, lastColumn, "error", code, text);
      }

      void Error(const Token& token, const std::string_view code, const std::string_view text)
      {
        Add(token.line, token.column, token.lastColumn, "error", code, text);
      }

      void Warning(const int line, const int column, const int lastColumn, const std::string_view code, const std::string_view text)
      {
        Add(line, column, lastColumn, "warning", code, text);
      }

      void Line(const std::string_view text)
      {
        mText += text;
        mText += '\n';
      }

      /** In a skipped group nothing is reported. */
      bool suppressed = false;

      [[nodiscard]] std::string& Text() noexcept
      {
        return mText;
      }

    private:
      void Add(
        const int line, const int column, const int lastColumn, const std::string_view kind, const std::string_view code,
        const std::string_view text
      )
      {
        if (suppressed) {
          return;
        }
        mText += mPath;
        mText += '(';
        mText += std::to_string(line);
        mText += ',';
        mText += std::to_string(column);
        if (lastColumn > column) {
          mText += '-';
          mText += std::to_string(lastColumn);
        }
        mText += "): ";
        mText += kind;
        if (!code.empty()) {
          mText += ' ';
          mText += code;
        }
        mText += ": ";
        mText += text;
        mText += '\n';
      }

      std::string mPath;
      std::string mText;
    };

    /** How the messages name a token: "token 'x'", "integer '5l'", "float '1.5'". */
    [[nodiscard]] std::string Describe(const Token& token);

    // --- Lexer -----------------------------------------------------------------------------------
    class Lexer
    {
    public:
      Lexer(const std::string_view text, Messages* const messages)
        : mText(text)
        , mMessages(messages)
      {}

      /** In a directive a backslash before a newline continues the line, and the newline ends it. */
      void SetDirective(const bool directive) noexcept
      {
        mDirective = directive;
      }

      /** The lines a directive went on past, with a backslash or inside a comment. */
      [[nodiscard]] std::vector<int> TakeContinuations()
      {
        return std::exchange(mContinuations, {});
      }

      /** In a macro's definition numbers are read as C reads them (ScanDefinitionNumber). */
      void SetDefinition(const bool definition) noexcept
      {
        mDefinition = definition;
      }

      [[nodiscard]] int Line() const noexcept
      {
        return mLine;
      }

      /** #line: the number of the line being read. */
      void SetLine(const int line) noexcept
      {
        mLine = line;
      }

      [[nodiscard]] int Column() const noexcept
      {
        return static_cast<int>(mPos - mLineStart) + 1;
      }

      [[nodiscard]] Token Next()
      {
        bool comment = false;
        for (;;) {
          if (!mQueuedNewlines.empty()) {
            Token newline;
            newline.kind = Kind::Newline;
            newline.line = mQueuedNewlines.front().line;
            newline.soft = mQueuedNewlines.front().soft;
            mQueuedNewlines.erase(mQueuedNewlines.begin());
            if (!newline.soft) {
              mLineHasToken = false;
            }
            return newline;
          }
          if (mPos >= mText.size()) {
            Token end;
            end.kind = Kind::End;
            end.line = mLine;
            end.column = Column();
            end.lastColumn = end.column;
            end.offset = mPos;
            return end;
          }
          const char c = mText[mPos];
          if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
            ++mPos;
            continue;
          }
          if (const std::size_t newline = NewlineAt(mPos); newline != 0U) {
            Token token = NewlineToken();
            mPos += newline;
            NextLine();
            return token;
          }
          if (c == '\r') {
            ++mPos;
            continue;
          }
          if (c == '\\') {
            if (const std::size_t newline = NewlineAt(mPos + 1U); newline != 0U) {
              if (mDirective) {
                if (BlankLineAt(mPos + 1U + newline)) {
                  // Continued into an empty line, the directive ends here, and the empty line is a
                  // blank one; at the end of the file it ends there.
                  Token token = NewlineToken();
                  mPos += 1U + newline;
                  NextLine();
                  return token;
                }
                mContinuations.push_back(mLine);
                mPos += 1U + newline;
                NextLine();
                continue;
              }
              // In text, a newline; the line goes on, so a `#` after it begins no directive.
              Token token = NewlineToken();
              mPos += 1U + newline;
              ++mLine;
              mLineStart = mPos;
              return token;
            }
          }
          if (c == '/' && Peek(1U) == '/') {
            SkipLineComment();
            comment = true;
            continue;
          }
          if (c == '/' && Peek(1U) == '*') {
            if (!SkipBlockComment()) {
              mPos = mText.size();
            }
            comment = true;
            continue;
          }
          Token token = Scan();
          token.commentBefore = comment;
          token.lineStart = !mLineHasToken;
          mLineHasToken = true;
          return token;
        }
      }

    private:
      [[nodiscard]] char Peek(const std::size_t ahead) const noexcept
      {
        return (mPos + ahead < mText.size()) ? mText[mPos + ahead] : '\0';
      }

      [[nodiscard]] std::size_t NewlineAt(const std::size_t position) const noexcept
      {
        if (position < mText.size() && mText[position] == '\n') {
          return 1U;
        }
        if (position + 1U < mText.size() && mText[position] == '\r' && mText[position + 1U] == '\n') {
          return 2U;
        }
        return 0U;
      }

      /** A line from `position` with nothing but spaces and tabs before its newline. */
      [[nodiscard]] bool BlankLineAt(std::size_t position) const noexcept
      {
        while (position < mText.size() && (mText[position] == ' ' || mText[position] == '\t')) {
          ++position;
        }
        return NewlineAt(position) != 0U;
      }

      [[nodiscard]] Token NewlineToken() const
      {
        Token token;
        token.kind = Kind::Newline;
        token.line = mLine;
        token.column = Column();
        token.lastColumn = token.column;
        token.offset = mPos;
        return token;
      }

      void NextLine() noexcept
      {
        ++mLine;
        mLineStart = mPos;
        mLineHasToken = false;
      }

      void SkipLineComment()
      {
        mPos += 2U;
        while (mPos < mText.size()) {
          if (mText[mPos] == '\\') {
            if (const std::size_t newline = NewlineAt(mPos + 1U); newline != 0U) {
              if (mDirective) {
                mContinuations.push_back(mLine);
              } else {
                mQueuedNewlines.push_back(QueuedNewline{mLine, false});
              }
              mPos += 1U + newline;
              ++mLine;
              mLineStart = mPos;
              continue;
            }
          }
          if (NewlineAt(mPos) != 0U) {
            return;
          }
          ++mPos;
        }
      }

      [[nodiscard]] bool SkipBlockComment()
      {
        mPos += 2U;
        for (;;) {
          if (mPos >= mText.size()) {
            if (mMessages != nullptr) {
              mMessages->Error(mLine, Column() + 1, 0, "X1001", "comment continues past end of file");
            }
            return false;
          }
          if (mText[mPos] == '*' && Peek(1U) == '/') {
            mPos += 2U;
            return true;
          }
          if (const std::size_t newline = NewlineAt(mPos); newline != 0U) {
            if (mDirective) {
              mContinuations.push_back(mLine);
            } else {
              mQueuedNewlines.push_back(QueuedNewline{mLine, true});
            }
            mPos += newline;
            ++mLine;
            mLineStart = mPos;
            continue;
          }
          ++mPos;
        }
      }

      [[nodiscard]] Token Scan()
      {
        Token token;
        token.line = mLine;
        token.column = Column();
        token.offset = mPos;
        const std::size_t start = mPos;
        const char c = mText[mPos];
        if (IsIdentifierStart(c) || (c == ':' && Peek(1U) == ':' && IsIdentifierStart(Peek(2U)))) {
          token.kind = Kind::Identifier;
          if (c == ':') {
            mPos += 2U;
          }
          ScanIdentifierChars();
          while (Peek(0U) == ':' && Peek(1U) == ':' && IsIdentifierStart(Peek(2U))) {
            mPos += 2U;
            ScanIdentifierChars();
          }
        } else if (IsDigit(c) || (c == '.' && IsDigit(Peek(1U)))) {
          if (mDefinition) {
            ScanDefinitionNumber(token);
          } else {
            ScanNumber(token);
          }
        } else if (c == '"') {
          ScanString(token);
        } else if (c == '\'' && IsCharLiteral()) {
          token.kind = Kind::Char;
          mPos += (Peek(1U) == '\\') ? 4U : 3U;
        } else {
          ScanPunct(token);
        }
        token.text.assign(mText.substr(start, mPos - start));
        token.lastColumn = Column() - 1;
        return token;
      }

      void ScanIdentifierChars() noexcept
      {
        while (IsIdentifierChar(Peek(0U))) {
          ++mPos;
        }
      }

      void ScanNumber(Token& token) noexcept
      {
        token.kind = Kind::Number;
        const std::size_t start = mPos;
        if (Peek(0U) == '0' && Peek(1U) == 'x' && IsHexDigit(Peek(2U))) {
          mPos += 2U;
          while (IsHexDigit(Peek(0U))) {
            ++mPos;
          }
          ScanIntegerSuffix();
          return;
        }
        while (IsDigit(Peek(0U))) {
          ++mPos;
        }
        const std::size_t digitsEnd = mPos;
        bool isFloat = false;
        if (Peek(0U) == '.') {
          ++mPos;
          while (IsDigit(Peek(0U))) {
            ++mPos;
          }
          isFloat = true;
        }
        if ((Peek(0U) == 'e' || Peek(0U) == 'E') &&
            (IsDigit(Peek(1U)) || ((Peek(1U) == '+' || Peek(1U) == '-') && IsDigit(Peek(2U))))) {
          mPos += IsDigit(Peek(1U)) ? 1U : 2U;
          while (IsDigit(Peek(0U))) {
            ++mPos;
          }
          isFloat = true;
        }
        if (isFloat) {
          const char suffix = Peek(0U);
          if (suffix == 'f' || suffix == 'F' || suffix == 'h' || suffix == 'H' || suffix == 'l' || suffix == 'L') {
            ++mPos;
          }
          token.isFloat = true;
          return;
        }
        // An integer with a leading 0 is octal and ends before its first digit that is not one.
        if (mText[start] == '0') {
          std::size_t octalEnd = start + 1U;
          while (octalEnd < digitsEnd && mText[octalEnd] <= '7') {
            ++octalEnd;
          }
          if (octalEnd < digitsEnd) {
            mPos = octalEnd;
            return;
          }
        }
        ScanIntegerSuffix();
      }

      /**
       * A number in a macro's definition, nearly a C preprocessing number: digits, letters and
       * underscores, one dot, and a sign after an exponent's e before a digit, but not in a
       * hexadecimal number (`08`, `1abc`, `0x` and `1e+2` stay whole, `1.0.0` is `1.0` and `.0`).
       */
      void ScanDefinitionNumber(Token& token) noexcept
      {
        token.kind = Kind::Number;
        const std::size_t start = mPos;
        const bool hexadecimal = Peek(0U) == '0' && (Peek(1U) == 'x' || Peek(1U) == 'X');
        bool dot = false;
        for (;;) {
          const char c = Peek(0U);
          if (IsIdentifierChar(c)) {
            ++mPos;
          } else if (c == '.' && !dot) {
            dot = true;
            ++mPos;
          } else if ((c == '+' || c == '-') && !hexadecimal && mPos > start && (mText[mPos - 1U] == 'e' || mText[mPos - 1U] == 'E') &&
                     IsDigit(Peek(1U))) {
            ++mPos;
          } else {
            break;
          }
        }
        const std::string_view text = mText.substr(start, mPos - start);
        token.isFloat = !hexadecimal && (dot || text.find_first_of("eE") != std::string_view::npos);
      }

      void ScanIntegerSuffix() noexcept
      {
        const char first = Peek(0U);
        if (first == 'u' || first == 'U') {
          ++mPos;
          if (Peek(0U) == 'l' || Peek(0U) == 'L') {
            ++mPos;
          }
        } else if (first == 'l' || first == 'L') {
          ++mPos;
          if (Peek(0U) == 'u' || Peek(0U) == 'U') {
            ++mPos;
          }
        }
      }

      void ScanString(Token& token)
      {
        token.kind = Kind::String;
        ++mPos;
        while (mPos < mText.size()) {
          const char c = mText[mPos];
          if (c == '"') {
            ++mPos;
            return;
          }
          if (c == '\\' && mPos + 1U < mText.size()) {
            mPos += 2U;
            continue;
          }
          ++mPos;
          if (c == '\n') {
            // d3dcompiler_47 ends the string after the newline, which it does not count.
            if (mMessages != nullptr) {
              mMessages->Error(token.line, token.column, 0, "X1005", "string continues past end of line");
            }
            return;
          }
        }
      }

      [[nodiscard]] bool IsCharLiteral() const noexcept
      {
        if (Peek(1U) == '\\') {
          return mPos + 3U < mText.size() && Peek(3U) == '\'';
        }
        return Peek(1U) != '\n' && Peek(1U) != '\r' && mPos + 2U < mText.size() && Peek(2U) == '\'';
      }

      void ScanPunct(Token& token) noexcept
      {
        static constexpr std::string_view kThree[] = {"<<=", ">>=", "..."};
        static constexpr std::string_view kTwo[] = {"<=", ">=", "==", "!=", "&&", "||", "++", "--", "+=", "-=", "*=",
                                                    "/=", "%=", "&=", "|=", "^=", "<<", ">>", "->", "##", "::"};
        token.kind = Kind::Punct;
        const std::string_view rest = mText.substr(mPos);
        for (const std::string_view punct : kThree) {
          if (rest.substr(0U, 3U) == punct) {
            mPos += 3U;
            return;
          }
        }
        for (const std::string_view punct : kTwo) {
          if (rest.substr(0U, 2U) == punct) {
            mPos += 2U;
            return;
          }
        }
        ++mPos;
      }

      std::string_view mText;
      Messages* mMessages;
      std::size_t mPos = 0U;
      std::size_t mLineStart = 0U;
      int mLine = 1;
      bool mDirective = false;
      bool mDefinition = false;
      bool mLineHasToken = false;
      // The newlines of a comment, given out before the token after it: a line comment's continued
      // by a backslash, and a block comment's, which are soft (Printer::SoftNewline).
      struct QueuedNewline
      {
        int line;
        bool soft;
      };
      std::vector<QueuedNewline> mQueuedNewlines;
      std::vector<int> mContinuations;
    };

    /**
     * `text` read as tokens on `line`: pasted tokens, and (`definition`) the definitions of the
     * macros defined before the source.
     */
    [[nodiscard]] std::vector<Token> Lex(const std::string_view text, const int line, const bool definition = false)
    {
      Lexer lexer(text, nullptr);
      lexer.SetDefinition(definition);
      std::vector<Token> tokens;
      for (Token token = lexer.Next(); token.kind != Kind::End; token = lexer.Next()) {
        if (token.kind == Kind::Newline) {
          continue;
        }
        token.line = line;
        token.lineStart = false;
        token.commentBefore = false;
        tokens.push_back(std::move(token));
      }
      return tokens;
    }

    [[nodiscard]] std::optional<std::uint32_t> IntegerValue(const Token& token)
    {
      std::string_view text = token.text;
      while (!text.empty() && (text.back() == 'u' || text.back() == 'U' || text.back() == 'l' || text.back() == 'L')) {
        text.remove_suffix(1U);
      }
      std::uint32_t base = 10U;
      if (text.size() > 2U && text[0] == '0' && text[1] == 'x') {
        base = 16U;
        text.remove_prefix(2U);
      } else if (text.size() > 1U && text[0] == '0') {
        base = 8U;
      }
      std::uint64_t value = 0U;
      for (const char c : text) {
        // A digit outside the base (a macro's `08`, `1abc`) makes no integer.
        const std::uint32_t digit = IsDigit(c) ? static_cast<std::uint32_t>(c - '0')
          : (c >= 'a' && c <= 'f')            ? static_cast<std::uint32_t>(c - 'a' + 10)
          : (c >= 'A' && c <= 'F')            ? static_cast<std::uint32_t>(c - 'A' + 10)
                                              : 16U;
        if (digit >= base) {
          return std::nullopt;
        }
        value = value * base + digit;
        if (value > 0xFFFFFFFFULL) {
          return std::nullopt;
        }
      }
      return static_cast<std::uint32_t>(value);
    }

    std::string Describe(const Token& token)
    {
      if (token.kind == Kind::Number) {
        if (token.isFloat) {
          return "float '" + token.text + "'";
        }
        // A valid integer by its value, as a long; one too large as it is written.
        const std::optional<std::uint32_t> value = IntegerValue(token);
        return value ? "integer '" + std::to_string(*value) + "l'" : "integer '" + token.text + "'";
      }
      if (token.kind == Kind::String) {
        return "string '" + token.text + "'";
      }
      return "token '" + token.text + "'";
    }

    // --- Printer ---------------------------------------------------------------------------------
    class Printer
    {
    public:
      explicit Printer(std::string file)
        : mFile(std::move(file))
      {}

      [[nodiscard]] bool Started() const noexcept
      {
        return mStarted;
      }

      void Write(const Token& token, const int line)
      {
        Flush();
        if (!mStarted) {
          Start(line);
        } else if (line != mCurrent) {
          Resync(line);
        }
        if (!mPragma.empty()) {
          if (!mAtLineStart) {
            mOut += '\n';
          }
          mOut += mPragma;
          mOut += '\n';
          mPragma.clear();
          mAtLineStart = true;
          mDirty = true;
        }
        const bool punct = token.kind == Kind::Punct;
        if (punct && token.text == "}" && mDepth > 0) {
          --mDepth;
        }
        if (mAtLineStart) {
          Indent();
          mAtLineStart = false;
        }
        mOut += token.text;
        mOut += ' ';
        if (punct && token.text == "{") {
          ++mDepth;
        }
        mRun = 0;
      }

      /**
       * The end of source line `line` in an active group. The first newline after a token or a
       * directive is written at once; a blank line after it waits for more to come, and the end
       * of the file drops it; more blank lines are dropped.
       */
      void Newline(const int line)
      {
        if (mRun == 0) {
          WriteNewline(line);
          mRun = 1;
        } else if (mRun == 1) {
          mPending = true;
          mRun = 2;
        }
      }

      /** A newline inside a block comment: it ends a line with tokens, and is nothing after that. */
      void SoftNewline(const int line)
      {
        if (mRun == 0) {
          WriteNewline(line);
          mRun = 1;
        }
      }

      /** A directive counts as content: the blank lines after it are written again. */
      void Directive()
      {
        Flush();
        mRun = 0;
      }

      /** #line N "file": the file and the line of the directive, at once. */
      void Announce(const int line, const std::string& file)
      {
        Flush();
        if (!mAtLineStart) {
          mOut += '\n';
        }
        mOut += "#line " + std::to_string(line) + ' ' + file + '\n';
        mStarted = true;
        mCurrent = line;
        mAtLineStart = true;
        mRun = 0;
      }

      /** #pragma pack_matrix: on a line of its own before the next token. */
      void Defer(std::string pragma)
      {
        mPragma = std::move(pragma);
      }

      /** An unknown pragma: the numbering is mended at the next newline. */
      void MarkDirty() noexcept
      {
        mDirty = true;
      }

      /** The text; a blank line still waiting is dropped, and a line begun inside braces is indented. */
      [[nodiscard]] std::string Finish()
      {
        if (mStarted && mAtLineStart) {
          Indent();
        }
        return std::move(mOut);
      }

    private:
      /** The blank line that waited: one more line, without a #line for it. */
      void Flush()
      {
        if (!mPending) {
          return;
        }
        mPending = false;
        if (mAtLineStart) {
          Indent();
        }
        mOut += '\n';
        ++mCurrent;
        mAtLineStart = true;
      }

      void WriteNewline(const int line)
      {
        if (!mStarted) {
          Start(line);
        } else if (mDirty) {
          mOut += '\n';
          mOut += "#line " + std::to_string(line) + '\n';
          mCurrent = line;
          mAtLineStart = true;
          mDirty = false;
        } else if (line != mCurrent) {
          Resync(line);
        }
        if (mAtLineStart) {
          Indent();
        }
        mOut += '\n';
        ++mCurrent;
        mAtLineStart = true;
      }

      void Start(const int line)
      {
        mOut += "#line " + std::to_string(line) + " \"" + mFile + "\"\n";
        mStarted = true;
        mCurrent = line;
        mAtLineStart = true;
      }

      void Resync(const int line)
      {
        if (!mAtLineStart) {
          mOut += '\n';
        }
        mOut += "#line " + std::to_string(line) + '\n';
        mCurrent = line;
        mAtLineStart = true;
      }

      void Indent()
      {
        mOut.append(static_cast<std::size_t>(mDepth) * 4U, ' ');
      }

      std::string mFile; // the escaped path of the first #line
      std::string mOut;
      std::string mPragma;
      bool mStarted = false;
      bool mAtLineStart = true;
      bool mDirty = false;
      int mCurrent = 0; // the source line of the output line being written
      int mDepth = 0;
      int mRun = 0;     // newlines since the last token or directive
      bool mPending = false; // a blank line waiting for more to come
    };

    // --- Token sources ---------------------------------------------------------------------------
    /** Where a macro invocation reads its arguments from. */
    class TokenSource
    {
    public:
      virtual ~TokenSource() = default;
      [[nodiscard]] virtual const Token& Peek(std::size_t ahead) = 0;
      virtual Token Take() = 0;
      /** `tokens` before what is left: what an expansion has after the argument list it ends. */
      virtual void PushFront(const std::vector<Token>& tokens) = 0;
      /** A place in what has been taken, and what was taken since: an invocation as written. */
      [[nodiscard]] virtual std::size_t Mark() const = 0;
      [[nodiscard]] virtual std::vector<Token> Since(std::size_t mark) const = 0;
    };

    const Token& EndToken()
    {
      static const Token sEnd;
      return sEnd;
    }

    /** The source text, with a lookahead, recording what is taken for an abandoned invocation. */
    class SourceTokens final : public TokenSource
    {
    public:
      explicit SourceTokens(Lexer& lexer)
        : mLexer(lexer)
      {}

      const Token& Peek(const std::size_t ahead) override
      {
        while (mBuffer.size() <= ahead) {
          mBuffer.push_back(mLexer.Next());
        }
        return mBuffer[ahead];
      }

      Token Take() override
      {
        Token token = Peek(0U);
        mBuffer.pop_front();
        if (mRecord != nullptr) {
          mRecord->push_back(token);
        }
        return token;
      }

      void PushFront(const std::vector<Token>& tokens) override
      {
        mBuffer.insert(mBuffer.begin(), tokens.begin(), tokens.end());
      }

      [[nodiscard]] std::size_t Mark() const override
      {
        return (mRecord != nullptr) ? mRecord->size() : 0U;
      }

      [[nodiscard]] std::vector<Token> Since(const std::size_t mark) const override
      {
        if (mRecord == nullptr || mark > mRecord->size()) {
          return {};
        }
        return std::vector<Token>(mRecord->begin() + static_cast<std::ptrdiff_t>(mark), mRecord->end());
      }

      /** While an invocation in the text is expanded: what it takes, should it be abandoned. */
      void Record(std::vector<Token>* const record) noexcept
      {
        mRecord = record;
      }

    private:
      Lexer& mLexer;
      std::deque<Token> mBuffer;
      std::vector<Token>* mRecord = nullptr;
    };

    /** A list of tokens being scanned; an invocation at its end reads on from `outer`. */
    class ListTokens final : public TokenSource
    {
    public:
      ListTokens(std::vector<Token> tokens, TokenSource* const outer)
        : mTokens(std::move(tokens))
        , mOuter(outer)
      {}

      const Token& Peek(const std::size_t ahead) override
      {
        if (mIndex + ahead < mTokens.size()) {
          return mTokens[mIndex + ahead];
        }
        return (mOuter != nullptr) ? mOuter->Peek(mIndex + ahead - mTokens.size()) : EndToken();
      }

      Token Take() override
      {
        if (mIndex < mTokens.size()) {
          return mTokens[mIndex++];
        }
        return (mOuter != nullptr) ? mOuter->Take() : Token{};
      }

      void PushFront(const std::vector<Token>& tokens) override
      {
        mTokens.insert(mTokens.begin() + static_cast<std::ptrdiff_t>(mIndex), tokens.begin(), tokens.end());
      }

      [[nodiscard]] std::size_t Mark() const override
      {
        return mIndex;
      }

      [[nodiscard]] std::vector<Token> Since(const std::size_t mark) const override
      {
        return std::vector<Token>(mTokens.begin() + static_cast<std::ptrdiff_t>(mark), mTokens.begin() + static_cast<std::ptrdiff_t>(mIndex));
      }

      /** The list's own next token; End after its last. */
      [[nodiscard]] Token TakeOwn()
      {
        return (mIndex < mTokens.size()) ? mTokens[mIndex++] : Token{};
      }

    private:
      std::vector<Token> mTokens;
      std::size_t mIndex = 0U;
      TokenSource* mOuter;
    };

    // --- The preprocessor ------------------------------------------------------------------------
    struct Recursion
    {
    };

    struct Definition
    {
      bool functionLike = false;
      std::vector<std::string> parameters;
      std::vector<Token> body;
    };

    [[nodiscard]] bool SameDefinition(const Definition& a, const Definition& b)
    {
      if (a.functionLike != b.functionLike || a.parameters != b.parameters || a.body.size() != b.body.size()) {
        return false;
      }
      for (std::size_t i = 0U; i < a.body.size(); ++i) {
        if (a.body[i].text != b.body[i].text) {
          return false;
        }
      }
      return true;
    }

    /** The macros being expanded. */
    class ActiveMacros
    {
    public:
      [[nodiscard]] bool Contains(const std::string& name) const
      {
        for (const std::string& active : mNames) {
          if (active == name) {
            return true;
          }
        }
        return false;
      }

      void Push(const std::string& name)
      {
        mNames.push_back(name);
      }

      void Pop()
      {
        mNames.pop_back();
      }

    private:
      std::vector<std::string> mNames;
    };

    class ActiveScope
    {
    public:
      ActiveScope(ActiveMacros& active, const std::string& name)
        : mActive(active)
      {
        mActive.Push(name);
      }

      ~ActiveScope()
      {
        mActive.Pop();
      }

      ActiveScope(const ActiveScope&) = delete;
      ActiveScope& operator=(const ActiveScope&) = delete;

    private:
      ActiveMacros& mActive;
    };

    struct Group
    {
      bool parentActive = true;
      bool active = true;
      bool taken = true; // a group of the conditional was active
      bool sawElse = false;
    };

    class Preprocessor
    {
    public:
      Preprocessor(const std::string_view source, const std::string_view path, const std::vector<Macro>& macros)
        : mMessages(path)
        , mLexer(source, &mMessages)
        , mSource(mLexer)
        , mPrinter(Escape(path))
        , mSourceText(source)
        , mFile("\"" + Escape(path) + "\"")
      {
        for (const Macro& macro : macros) {
          if (macro.name.empty()) {
            continue;
          }
          Definition definition;
          definition.body = Lex(macro.definition, 0, true);
          mMacros[macro.name] = std::move(definition);
        }
      }

      [[nodiscard]] Output Run()
      {
        Output output;
        for (;;) {
          Token token = mSource.Take();
          if (token.kind == Kind::End) {
            break;
          }
          if (token.kind == Kind::Newline) {
            if (Active()) {
              WriteNewline(token);
            }
            continue;
          }
          if (token.lineStart && IsPunct(token, "#")) {
            if (!Directive(token)) {
              output.failed = true;
              output.messages = std::move(mMessages.Text());
              return output;
            }
            continue;
          }
          if (!Active()) {
            continue;
          }
          if (!Expandable(token)) {
            mPrinter.Write(token, token.line);
            continue;
          }
          // A macro in the text: its expansion, or the invocation as written when it would recur.
          std::vector<Token> expansion;
          std::vector<Token> taken;
          mSource.Record(&taken);
          ActiveMacros active;
          Expand(token, mSource, active, expansion, true, true);
          mSource.Record(nullptr);
          for (const Token& written : expansion) {
            mPrinter.Write(written, token.line);
          }
          FlushNewlines();
        }
        if (!mGroups.empty()) {
          mMessages.suppressed = false;
          for (int i = 0; i < 2; ++i) {
            mMessages.Error(mLexer.Line(), mLexer.Column(), 0, "X1502", "unexpected end of file");
          }
        }

        for (const std::string& def : mPragmaDefs) {
          output.text += "#pragma def (" + def + ")\n";
        }
        for (const auto& [number, action] : mPragmaWarnings) {
          output.text += "#pragma warning (" + action + ':' + std::to_string(number) + ")\n";
        }
        output.text += mPrinter.Finish();
        output.messages = std::move(mMessages.Text());
        return output;
      }

    private:
      [[nodiscard]] bool Active() const noexcept
      {
        return mGroups.empty() || mGroups.back().active;
      }

      [[nodiscard]] bool Expandable(const Token& token) const
      {
        return token.kind == Kind::Identifier && !token.painted &&
          (token.text == "__FILE__" || token.text == "__LINE__" || mMacros.find(token.text) != mMacros.end());
      }

      void WriteNewline(const Token& newline)
      {
        if (newline.soft) {
          mPrinter.SoftNewline(newline.line);
        } else {
          mPrinter.Newline(newline.line);
        }
      }

      void FlushNewlines()
      {
        for (const Token& newline : mDeferredNewlines) {
          WriteNewline(newline);
        }
        mDeferredNewlines.clear();
      }

      /** The invocation as written, painted. Its newlines are printed after it anyway. */
      [[nodiscard]] static std::vector<Token> Abandoned(const Token& name, const std::vector<Token>& taken)
      {
        std::vector<Token> tokens;
        tokens.push_back(name);
        for (const Token& token : taken) {
          if (token.kind != Kind::Newline && token.kind != Kind::End) {
            tokens.push_back(token);
          }
        }
        for (Token& token : tokens) {
          token.painted = true;
        }
        return tokens;
      }

      // --- Expansion ---
      /**
       * Expands the macro `name` read from `source` into `out`. `fromText`: the name is in the
       * source text, where a comment before `(` keeps it from being an invocation. `catching`: the
       * name is in the text or in an argument of an invocation there, where a recursion abandons
       * the invocation; elsewhere the recursion goes on up to there.
       */
      void Expand(const Token& name, TokenSource& source, ActiveMacros& active, std::vector<Token>& out, const bool fromText, const bool catching)
      {
        if (!catching) {
          ExpandOne(name, source, active, out, fromText, false);
          return;
        }
        const std::size_t mark = source.Mark();
        const std::size_t size = out.size();
        try {
          ExpandOne(name, source, active, out, fromText, true);
        } catch (const Recursion&) {
          out.resize(size);
          for (Token& token : Abandoned(name, source.Since(mark))) {
            out.push_back(std::move(token));
          }
        }
      }

      void ExpandOne(const Token& name, TokenSource& source, ActiveMacros& active, std::vector<Token>& out, const bool fromText, const bool catching)
      {
        if (name.text == "__FILE__" || name.text == "__LINE__") {
          Token builtin = name;
          builtin.kind = (name.text == "__FILE__") ? Kind::String : Kind::Number;
          builtin.text = (name.text == "__FILE__") ? mFile : std::to_string(name.line);
          out.push_back(std::move(builtin));
          return;
        }
        const Definition& macro = mMacros.at(name.text);
        // A function-like macro's name without arguments is no invocation, and no recursion either.
        if (macro.functionLike && !OpensArguments(source, fromText)) {
          out.push_back(name);
          return;
        }
        if (active.Contains(name.text)) {
          throw Recursion{};
        }
        std::vector<std::vector<Token>> arguments;
        if (macro.functionLike) {
          source.Take(); // (
          const std::size_t newlines = mDeferredNewlines.size();
          if (!ReadArguments(source, arguments, active, fromText, catching)) {
            // The file ends among the arguments: the name stays, the rest goes.
            mDeferredNewlines.resize(newlines);
            mMessages.Error(name, "X1515", "unexpected end of file in macro expansion");
            out.push_back(name);
            return;
          }
          bool emptyArgument = false;
          for (const std::vector<Token>& argument : arguments) {
            emptyArgument = emptyArgument || argument.empty();
          }
          if (arguments.size() != macro.parameters.size() || emptyArgument) {
            mMessages.Error(name, "X1516", "not enough actual parameters for macro '" + name.text + "'");
            out.push_back(name);
            return;
          }
        }
        std::vector<Token> replaced = Substitute(macro, arguments, name.line);
        const ActiveScope scope(active, name.text);
        std::vector<Token> rescanned = Rescan(std::move(replaced), active, source);
        out.insert(out.end(), std::make_move_iterator(rescanned.begin()), std::make_move_iterator(rescanned.end()));
      }

      /** Whether `(` follows a function-like macro's name, on its line. */
      [[nodiscard]] static bool OpensArguments(TokenSource& source, const bool fromText)
      {
        const Token& next = source.Peek(0U);
        if (IsPunct(next, "(")) {
          return !(fromText && next.commentBefore);
        }
        // A name that ends an expansion looked on into the text: a newline there ends the line,
        // but one before the end of the file is lost.
        if (!fromText && next.kind == Kind::Newline && source.Peek(1U).kind == Kind::End) {
          source.Take();
        }
        return false;
      }

      /**
       * The arguments after `(`. d3dcompiler_47 expands their macros as it reads them, before it
       * looks for the commas and brackets: `(`, `[` and `{` open, `)`, `]` and `}` close, a comma
       * outside them separates, and a closing one outside them ends the list, `)` left out and
       * the others kept in the last argument. The newlines among them wait for after the
       * expansion. False when the file ends first.
       */
      [[nodiscard]] bool ReadArguments(
        TokenSource& source, std::vector<std::vector<Token>>& arguments, ActiveMacros& active, const bool fromText, const bool catching
      )
      {
        arguments.emplace_back();
        int depth = 0;
        for (;;) {
          Token token = source.Take();
          if (token.kind == Kind::End) {
            return false;
          }
          if (token.kind == Kind::Newline) {
            mDeferredNewlines.push_back(std::move(token));
            continue;
          }
          std::vector<Token> tokens;
          if (Expandable(token)) {
            Expand(token, source, active, tokens, fromText, catching);
          } else {
            tokens.push_back(std::move(token));
          }
          for (std::size_t i = 0U; i < tokens.size(); ++i) {
            Token& read = tokens[i];
            const bool opens = IsPunct(read, "(") || IsPunct(read, "[") || IsPunct(read, "{");
            const bool closes = IsPunct(read, ")") || IsPunct(read, "]") || IsPunct(read, "}");
            if (closes && depth == 0) {
              if (!IsPunct(read, ")")) {
                arguments.back().push_back(std::move(read));
              }
              std::vector<Token> rest(tokens.begin() + static_cast<std::ptrdiff_t>(i + 1U), tokens.end());
              if (!rest.empty()) {
                for (Token& left : rest) {
                  left.painted = true; // expanded already
                }
                source.PushFront(rest);
              }
              return true;
            }
            if (opens) {
              ++depth;
            } else if (closes) {
              --depth;
            } else if (IsPunct(read, ",") && depth == 0) {
              arguments.emplace_back();
              continue;
            }
            read.commentBefore = false;
            arguments.back().push_back(std::move(read));
          }
        }
      }

      /** The tokens of a #if, expanded. */
      [[nodiscard]] std::vector<Token> ExpandList(std::vector<Token> tokens)
      {
        ListTokens list(std::move(tokens), nullptr);
        ActiveMacros active;
        std::vector<Token> result;
        for (Token token = list.TakeOwn(); token.kind != Kind::End; token = list.TakeOwn()) {
          if (Expandable(token)) {
            Expand(token, list, active, result, false, true);
          } else {
            result.push_back(std::move(token));
          }
        }
        return result;
      }

      [[nodiscard]] std::vector<Token> Rescan(std::vector<Token> tokens, ActiveMacros& active, TokenSource& outer)
      {
        ListTokens list(std::move(tokens), &outer);
        std::vector<Token> result;
        for (Token token = list.TakeOwn(); token.kind != Kind::End; token = list.TakeOwn()) {
          if (Expandable(token)) {
            ExpandOne(token, list, active, result, false, false);
          } else {
            result.push_back(std::move(token));
          }
        }
        return result;
      }

      [[nodiscard]] static int ParameterIndex(const Definition& macro, const std::string& name)
      {
        for (std::size_t i = 0U; i < macro.parameters.size(); ++i) {
          if (macro.parameters[i] == name) {
            return static_cast<int>(i);
          }
        }
        return -1;
      }

      /** The body with the (expanded) arguments in place, and # and ## applied. */
      [[nodiscard]] static std::vector<Token> Substitute(const Definition& macro, const std::vector<std::vector<Token>>& arguments, const int line)
      {
        const auto operand = [&](const Token& token) -> std::vector<Token> {
          const int parameter = (token.kind == Kind::Identifier) ? ParameterIndex(macro, token.text) : -1;
          return (parameter >= 0) ? arguments[static_cast<std::size_t>(parameter)] : std::vector<Token>{token};
        };

        std::vector<Token> out;
        std::size_t operandStart = 0U; // where the last operand's tokens begin in `out`
        const std::vector<Token>& body = macro.body;
        for (std::size_t i = 0U; i < body.size(); ++i) {
          const Token& token = body[i];
          if (macro.functionLike && IsPunct(token, "#")) {
            // The next token stringized, an argument expanded; at the end of the body nothing.
            if (i + 1U < body.size()) {
              operandStart = out.size();
              out.push_back(Stringize(operand(body[i + 1U]), line));
              ++i;
            }
            continue;
          }
          if (macro.functionLike && IsPunct(token, "##") && i > 0U && i + 1U < body.size()) {
            std::vector<Token> right = operand(body[i + 1U]);
            ++i;
            if (out.size() == operandStart || right.empty()) {
              // An operand that expanded to nothing: the other stands alone.
              out.insert(out.end(), right.begin(), right.end());
              continue;
            }
            const std::string pasted = out.back().text + right.front().text;
            out.pop_back();
            const std::size_t start = out.size();
            for (Token& joined : Lex(pasted, line)) {
              out.push_back(std::move(joined));
            }
            out.insert(out.end(), right.begin() + 1, right.end());
            operandStart = start;
            continue;
          }
          operandStart = out.size();
          const std::vector<Token> tokens = operand(token);
          out.insert(out.end(), tokens.begin(), tokens.end());
        }
        for (Token& token : out) {
          token.line = line;
          token.lineStart = false;
          token.commentBefore = false;
        }
        return out;
      }

      [[nodiscard]] static Token Stringize(const std::vector<Token>& tokens, const int line)
      {
        std::string text = "\"";
        for (std::size_t i = 0U; i < tokens.size(); ++i) {
          if (i != 0U) {
            text += ' ';
          }
          const bool literal = tokens[i].kind == Kind::String || tokens[i].kind == Kind::Char;
          text += literal ? Escape(tokens[i].text) : tokens[i].text;
        }
        text += '"';
        Token token;
        token.kind = Kind::String;
        token.text = std::move(text);
        token.line = line;
        return token;
      }

      // --- Directives ---
      /** Returns false on #error and #include, which end the preprocessing. */
      [[nodiscard]] bool Directive(const Token& hash)
      {
        mLexer.SetDirective(true);
        std::vector<Token> tokens;
        Token end;
        for (;;) {
          Token token = mLexer.Next();
          if (token.kind == Kind::Newline || token.kind == Kind::End) {
            end = std::move(token);
            break;
          }
          if (tokens.empty() && token.kind == Kind::Identifier && token.text == "define") {
            mLexer.SetDefinition(true);
          }
          tokens.push_back(std::move(token));
        }
        mLexer.SetDirective(false);
        mLexer.SetDefinition(false);
        mContinuations = mLexer.TakeContinuations();
        const int line = end.line;
        const int endColumn = tokens.empty() ? hash.lastColumn + 1 : tokens.back().lastColumn + 1;

        const bool wasActive = Active();
        if (tokens.empty()) {
          if (wasActive) {
            mMessages.Error(hash.line, hash.lastColumn + 1, 0, "X1500", "syntax error : unexpected end of line");
            DirectiveNewline(line);
          }
          return true;
        }
        const Token& name = tokens.front();
        const std::string directive = (name.kind == Kind::Identifier) ? name.text : std::string();
        if (directive == "if" || directive == "ifdef" || directive == "ifndef" || directive == "elif" || directive == "else" ||
            directive == "endif") {
          Conditional(directive, tokens, line, endColumn, wasActive);
          mMessages.suppressed = !Active();
          return true;
        }
        if (!wasActive) {
          return true;
        }
        if (directive == "define") {
          Define(tokens, endColumn);
        } else if (directive == "undef") {
          Undefine(tokens, endColumn);
        } else if (directive == "line") {
          LineDirective(tokens, line, endColumn);
          return true;
        } else if (directive == "pragma") {
          Pragma(tokens);
        } else if (directive == "error") {
          std::string text;
          for (std::size_t i = 1U; i < tokens.size(); ++i) {
            text += (i > 1U) ? " " : "";
            text += tokens[i].text;
          }
          mMessages.Error(name, "", text);
          return false;
        } else if (directive == "include") {
          if (tokens.size() > 1U) {
            mMessages.Error(tokens[1], "X1505",
                            "No include handler specified, can't perform a #include. Use D3DX APIs or provide your own include handler.");
          } else {
            mMessages.Error(name.line, endColumn, 0, "X1505",
                            "No include handler specified, can't perform a #include. Use D3DX APIs or provide your own include handler.");
          }
          return false;
        } else if (name.kind == Kind::Identifier) {
          mMessages.Error(name, "X1504", "invalid preprocessor command '" + name.text + "'");
        } else {
          mMessages.Error(name, "X1500", "syntax error : unexpected " + Describe(name));
        }
        DirectiveNewline(line);
        return true;
      }

      /**
       * The newline of a directive in an active group. The lines it went on past before it are
       * blank lines, but none begins the output.
       */
      void DirectiveNewline(const int line)
      {
        ContinuationNewlines();
        mPrinter.Directive();
        mPrinter.Newline(line);
      }

      void ContinuationNewlines()
      {
        for (const int continued : mContinuations) {
          if (mPrinter.Started()) {
            mPrinter.Newline(continued);
          }
        }
        mContinuations.clear();
      }

      void Conditional(const std::string& directive, const std::vector<Token>& tokens, const int line, const int endColumn, const bool wasActive)
      {
        const Token& name = tokens.front();
        if (directive == "if" || directive == "ifdef" || directive == "ifndef") {
          if (!wasActive) {
            mGroups.push_back(Group{false, false, true, false});
            return;
          }
          std::optional<bool> condition;
          if (directive == "if") {
            condition = Evaluate(tokens, endColumn);
          } else {
            condition = Defined(tokens, endColumn, directive == "ifndef");
          }
          if (condition) {
            mGroups.push_back(Group{true, *condition, *condition, false});
          } // else dropped: its lines are part of the group around it
          ConditionalNewline(line, wasActive);
          return;
        }

        if (mGroups.empty()) {
          if (directive == "elif") {
            mMessages.Error(name.line, endColumn, 0, "X1508", "unexpected #elif");
          } else if (directive == "else") {
            mMessages.Error(name, "X1509", "unexpected #else");
          } else {
            mMessages.Error(name, "X1510", "unexpected #endif");
          }
          DirectiveNewline(line);
          return;
        }
        Group& group = mGroups.back();
        mMessages.suppressed = !group.parentActive;
        if (directive == "elif") {
          if (group.parentActive && !group.sawElse) {
            if (group.taken) {
              group.active = false;
            } else {
              const std::optional<bool> condition = Evaluate(tokens, endColumn);
              group.active = condition.value_or(false);
              group.taken = group.active;
            }
          }
        } else if (directive == "else") {
          if (group.sawElse) {
            mMessages.Error(name, "X1514", "unexpected #else following #else");
          } else {
            group.sawElse = true;
            if (group.parentActive) {
              group.active = !group.taken;
              group.taken = true;
            }
          }
        } else {
          if (tokens.size() > 1U) {
            mMessages.Error(tokens[1], "X1500", "syntax error : unexpected " + Describe(tokens[1]));
          }
          mGroups.pop_back();
        }
        ConditionalNewline(line, wasActive);
      }

      /**
       * The newline of a conditional directive: a directive's when a group is active after it; a
       * blank line's when it ends the active group (one that would begin the output is dropped);
       * none between skipped groups.
       */
      void ConditionalNewline(const int line, const bool wasActive)
      {
        if (Active()) {
          DirectiveNewline(line);
        } else if (wasActive && mPrinter.Started()) {
          ContinuationNewlines();
          mPrinter.Newline(line);
        }
      }

      /** #ifdef, #ifndef: none when the identifier is missing. */
      [[nodiscard]] std::optional<bool> Defined(const std::vector<Token>& tokens, const int endColumn, const bool negate)
      {
        if (tokens.size() < 2U) {
          mMessages.Error(tokens.front().line, endColumn, 0, "X1500", "syntax error : unexpected end of line");
          return std::nullopt;
        }
        const Token& name = tokens[1];
        if (name.kind != Kind::Identifier) {
          mMessages.Error(name, "X1500", "syntax error : unexpected " + Describe(name));
          return std::nullopt;
        }
        if (tokens.size() > 2U) {
          mMessages.Error(tokens[2], "X1500", "syntax error : unexpected " + Describe(tokens[2]));
        }
        const bool defined = name.text == "__FILE__" || name.text == "__LINE__" || mMacros.find(name.text) != mMacros.end();
        return defined != negate;
      }

      void Define(const std::vector<Token>& tokens, const int endColumn)
      {
        if (tokens.size() < 2U) {
          mMessages.Error(tokens.front().line, endColumn, 0, "X1500", "syntax error : unexpected end of line");
          return;
        }
        const Token& name = tokens[1];
        if (name.kind != Kind::Identifier || name.text == "defined") {
          mMessages.Error(name, "X1500", "syntax error : unexpected " + Describe(name));
          return;
        }
        Definition macro;
        std::size_t i = 2U;
        if (i < tokens.size() && IsPunct(tokens[i], "(") && tokens[i].line == name.line && tokens[i].column == name.lastColumn + 1 &&
            !tokens[i].commentBefore) {
          macro.functionLike = true;
          ++i;
          for (;;) {
            if (i >= tokens.size()) {
              mMessages.Error(name.line, endColumn, 0, "X1500", "syntax error : unexpected end of line");
              return;
            }
            const Token& parameter = tokens[i];
            if (parameter.kind != Kind::Identifier) {
              mMessages.Error(parameter, "X1500", "syntax error : unexpected " + Describe(parameter));
              return;
            }
            if (ParameterIndex(macro, parameter.text) >= 0) {
              mMessages.Error(parameter.line, parameter.column, parameter.lastColumn, "X1511",
                              "duplicate macro parameter '" + parameter.text + "'");
            }
            macro.parameters.push_back(parameter.text);
            ++i;
            if (i >= tokens.size()) {
              mMessages.Error(name.line, endColumn, 0, "X1500", "syntax error : unexpected end of line");
              return;
            }
            if (IsPunct(tokens[i], ")")) {
              ++i;
              break;
            }
            if (!IsPunct(tokens[i], ",")) {
              mMessages.Error(tokens[i], "X1500", "syntax error : unexpected " + Describe(tokens[i]));
              return;
            }
            ++i;
          }
        }
        macro.body.assign(tokens.begin() + static_cast<std::ptrdiff_t>(i), tokens.end());
        for (Token& token : macro.body) {
          token.lineStart = false;
          token.commentBefore = false;
        }
        const auto existing = mMacros.find(name.text);
        if (existing != mMacros.end() && !SameDefinition(existing->second, macro)) {
          mMessages.Warning(name.line, endColumn, 0, "X1519", "'" + name.text + "' : macro redefinition");
        }
        mMacros[name.text] = std::move(macro);
      }

      void Undefine(const std::vector<Token>& tokens, const int endColumn)
      {
        if (tokens.size() < 2U) {
          mMessages.Error(tokens.front().line, endColumn, 0, "X1500", "syntax error : unexpected end of line");
          return;
        }
        const Token& name = tokens[1];
        if (name.kind != Kind::Identifier) {
          mMessages.Error(name, "X1500", "syntax error : unexpected " + Describe(name));
          return;
        }
        if (tokens.size() > 2U) {
          mMessages.Error(tokens[2], "X1500", "syntax error : unexpected " + Describe(tokens[2]));
        }
        mMacros.erase(name.text);
      }

      void LineDirective(const std::vector<Token>& tokens, const int line, const int endColumn)
      {
        const std::optional<std::uint32_t> target =
          (tokens.size() > 1U && tokens[1].kind == Kind::Number && !tokens[1].isFloat) ? IntegerValue(tokens[1]) : std::nullopt;
        if (!target) {
          if (tokens.size() > 1U) {
            mMessages.Error(tokens[1], "X1500", "syntax error : unexpected " + Describe(tokens[1]));
          } else {
            mMessages.Error(tokens.front().line, endColumn, 0, "X1500", "syntax error : unexpected end of line");
          }
          DirectiveNewline(line);
          return;
        }
        const int next = static_cast<int>(*target);
        if (tokens.size() > 2U && tokens[2].kind == Kind::String) {
          if (mPrinter.Started()) {
            DirectiveNewline(line);
          }
          mPrinter.Announce(next - 1, tokens[2].text);
          mFile = tokens[2].text;
          mLexer.SetLine(next);
          mPrinter.Newline(next - 1);
          return;
        }
        if (next == line + 1) {
          DirectiveNewline(line);
          return;
        }
        DirectiveNewline(line);
        mPrinter.Newline(line + 1);
        mLexer.SetLine(next);
      }

      void Pragma(const std::vector<Token>& tokens)
      {
        if (tokens.size() < 2U) {
          return;
        }
        const Token& name = tokens[1];
        const bool parenthesized = tokens.size() > 3U && IsPunct(tokens[2], "(") && IsPunct(tokens.back(), ")");
        if (name.text == "pack_matrix" && parenthesized && tokens.size() == 5U && tokens[3].kind == Kind::Identifier) {
          mPrinter.Defer("#pragma pack_matrix(" + tokens[3].text + ")");
          return;
        }
        if (name.text == "warning" && parenthesized) {
          // (action : number number ...; action : ...)
          std::size_t i = 3U;
          while (i + 1U < tokens.size()) {
            const std::string action = tokens[i].text;
            if (!IsPunct(tokens[i + 1U], ":")) {
              break;
            }
            i += 2U;
            while (i + 1U < tokens.size() && tokens[i].kind == Kind::Number) {
              if (const std::optional<std::uint32_t> number = IntegerValue(tokens[i])) {
                mPragmaWarnings[*number] = action;
              }
              ++i;
            }
            if (IsPunct(tokens[i], ";")) {
              ++i;
            }
          }
          return;
        }
        if (name.text == "def" && parenthesized) {
          const std::size_t begin = tokens[2].offset + 1U;
          mPragmaDefs.emplace_back(mSourceText.substr(begin, tokens.back().offset - begin));
          return;
        }
        if (name.text == "message" && parenthesized && tokens.size() == 5U && tokens[3].kind == Kind::String) {
          const std::string& text = tokens[3].text;
          mMessages.Line(std::string_view(text).substr(1U, text.size() - 2U));
          return;
        }
        mMessages.Warning(name.line, name.column, name.lastColumn, "X3568", "'" + name.text + "' : unknown pragma ignored");
        mPrinter.MarkDirty();
      }

      // --- #if ---
      /** The condition of #if or #elif; none when the expression has an error. */
      [[nodiscard]] std::optional<bool> Evaluate(const std::vector<Token>& tokens, const int endColumn)
      {
        // defined X, defined ( X ): before the macros are expanded.
        std::vector<Token> replaced;
        for (std::size_t i = 1U; i < tokens.size(); ++i) {
          const Token& token = tokens[i];
          if (token.kind != Kind::Identifier || token.text != "defined") {
            replaced.push_back(token);
            continue;
          }
          const bool parenthesized = i + 1U < tokens.size() && IsPunct(tokens[i + 1U], "(");
          const std::size_t nameIndex = parenthesized ? i + 2U : i + 1U;
          if (nameIndex >= tokens.size() || tokens[nameIndex].kind != Kind::Identifier ||
              (parenthesized && (nameIndex + 1U >= tokens.size() || !IsPunct(tokens[nameIndex + 1U], ")")))) {
            const Token& bad = (nameIndex < tokens.size()) ? tokens[nameIndex] : tokens.back();
            mMessages.Error(bad, "X1500", "syntax error : unexpected " + Describe(bad));
            return std::nullopt;
          }
          const std::string& macro = tokens[nameIndex].text;
          Token value = token;
          value.kind = Kind::Number;
          value.text = (macro == "__FILE__" || macro == "__LINE__" || mMacros.find(macro) != mMacros.end()) ? "1" : "0";
          replaced.push_back(std::move(value));
          i = parenthesized ? nameIndex + 1U : nameIndex;
        }
        const std::vector<Token> expanded = ExpandList(std::move(replaced));
        Expression expression(expanded, mMessages, tokens.front().line, endColumn);
        return expression.Evaluate();
      }

      class Expression
      {
      public:
        Expression(const std::vector<Token>& tokens, Messages& messages, const int line, const int endColumn)
          : mTokens(tokens)
          , mMessages(messages)
          , mLine(line)
          , mEndColumn(endColumn)
        {}

        [[nodiscard]] std::optional<bool> Evaluate()
        {
          std::uint32_t value = 0U;
          try {
            value = Conditional(true);
          } catch (const Failure&) {
            return std::nullopt;
          }
          if (mIndex < mTokens.size()) {
            // A whole expression with more after it: an error, but its value counts.
            mMessages.Error(mTokens[mIndex], "X1500", "syntax error : unexpected " + Describe(mTokens[mIndex]));
          }
          return value != 0U;
        }

      private:
        struct Failure
        {
        };

        [[nodiscard]] const Token* Peek() const
        {
          return (mIndex < mTokens.size()) ? &mTokens[mIndex] : nullptr;
        }

        [[nodiscard]] bool Accept(const std::string_view punct)
        {
          if (const Token* const token = Peek(); token != nullptr && IsPunct(*token, punct)) {
            ++mIndex;
            return true;
          }
          return false;
        }

        [[noreturn]] void Unexpected()
        {
          if (const Token* const token = Peek(); token != nullptr) {
            mMessages.Error(*token, "X1500", "syntax error : unexpected " + Describe(*token));
          } else {
            mMessages.Error(mLine, mEndColumn, 0, "X1500", "syntax error : unexpected end of line");
          }
          throw Failure{};
        }

        [[nodiscard]] std::uint32_t Conditional(const bool evaluate)
        {
          const std::uint32_t condition = Or(evaluate);
          if (!Accept("?")) {
            return condition;
          }
          const std::uint32_t whenTrue = Conditional(evaluate && condition != 0U);
          if (!Accept(":")) {
            Unexpected();
          }
          const std::uint32_t whenFalse = Conditional(evaluate && condition == 0U);
          return (condition != 0U) ? whenTrue : whenFalse;
        }

        [[nodiscard]] std::uint32_t Or(const bool evaluate)
        {
          std::uint32_t value = And(evaluate);
          while (Accept("||")) {
            const std::uint32_t right = And(evaluate && value == 0U);
            value = (value != 0U || right != 0U) ? 1U : 0U;
          }
          return value;
        }

        [[nodiscard]] std::uint32_t And(const bool evaluate)
        {
          std::uint32_t value = BitOr(evaluate);
          while (Accept("&&")) {
            const std::uint32_t right = BitOr(evaluate && value != 0U);
            value = (value != 0U && right != 0U) ? 1U : 0U;
          }
          return value;
        }

        [[nodiscard]] std::uint32_t BitOr(const bool evaluate)
        {
          std::uint32_t value = BitXor(evaluate);
          while (Accept("|")) {
            value |= BitXor(evaluate);
          }
          return value;
        }

        [[nodiscard]] std::uint32_t BitXor(const bool evaluate)
        {
          std::uint32_t value = BitAnd(evaluate);
          while (Accept("^")) {
            value ^= BitAnd(evaluate);
          }
          return value;
        }

        [[nodiscard]] std::uint32_t BitAnd(const bool evaluate)
        {
          std::uint32_t value = Equality(evaluate);
          while (Accept("&")) {
            value &= Equality(evaluate);
          }
          return value;
        }

        [[nodiscard]] std::uint32_t Equality(const bool evaluate)
        {
          std::uint32_t value = Relational(evaluate);
          for (;;) {
            if (Accept("==")) {
              value = (value == Relational(evaluate)) ? 1U : 0U;
            } else if (Accept("!=")) {
              value = (value != Relational(evaluate)) ? 1U : 0U;
            } else {
              return value;
            }
          }
        }

        [[nodiscard]] std::uint32_t Relational(const bool evaluate)
        {
          std::uint32_t value = Additive(evaluate);
          for (;;) {
            if (Accept("<")) {
              value = (value < Additive(evaluate)) ? 1U : 0U;
            } else if (Accept(">")) {
              value = (value > Additive(evaluate)) ? 1U : 0U;
            } else if (Accept("<=")) {
              value = (value <= Additive(evaluate)) ? 1U : 0U;
            } else if (Accept(">=")) {
              value = (value >= Additive(evaluate)) ? 1U : 0U;
            } else {
              return value;
            }
          }
        }

        [[nodiscard]] std::uint32_t Additive(const bool evaluate)
        {
          std::uint32_t value = Multiplicative(evaluate);
          for (;;) {
            if (Accept("+")) {
              value += Multiplicative(evaluate);
            } else if (Accept("-")) {
              value -= Multiplicative(evaluate);
            } else {
              return value;
            }
          }
        }

        [[nodiscard]] std::uint32_t Multiplicative(const bool evaluate)
        {
          std::uint32_t value = Unary(evaluate);
          for (;;) {
            if (Accept("*")) {
              value *= Unary(evaluate);
            } else if (Accept("/")) {
              const Token* const divisorToken = Peek();
              const std::uint32_t divisor = Unary(evaluate);
              if (divisor == 0U) {
                if (evaluate) {
                  mMessages.Error(*divisorToken, "X1503", "division by zero in preprocessor expression");
                  throw Failure{};
                }
              } else {
                value /= divisor;
              }
            } else {
              return value;
            }
          }
        }

        [[nodiscard]] std::uint32_t Unary(const bool evaluate)
        {
          if (Accept("!")) {
            return (Unary(evaluate) == 0U) ? 1U : 0U;
          }
          if (Accept("-")) {
            return 0U - Unary(evaluate);
          }
          if (Accept("+")) {
            return Unary(evaluate);
          }
          return Primary(evaluate);
        }

        [[nodiscard]] std::uint32_t Primary(const bool evaluate)
        {
          const Token* const token = Peek();
          if (token == nullptr) {
            Unexpected();
          }
          if (Accept("(")) {
            const std::uint32_t value = Conditional(evaluate);
            if (!Accept(")")) {
              Unexpected();
            }
            return value;
          }
          if (token->kind == Kind::Number && !token->isFloat) {
            const std::optional<std::uint32_t> value = IntegerValue(*token);
            if (!value) {
              Unexpected();
            }
            ++mIndex;
            return *value;
          }
          if (token->kind == Kind::Char) {
            ++mIndex;
            return CharValue(token->text);
          }
          if (token->kind == Kind::Identifier) {
            ++mIndex;
            return 0U;
          }
          Unexpected();
        }

        [[nodiscard]] static std::uint32_t CharValue(const std::string& text)
        {
          if (text.size() == 4U && text[1] == '\\') {
            switch (text[2]) {
            case 'n':
              return '\n';
            case 't':
              return '\t';
            case 'r':
              return '\r';
            case '0':
              return 0U;
            case 'a':
              return '\a';
            case 'b':
              return '\b';
            case 'f':
              return '\f';
            case 'v':
              return '\v';
            default:
              return static_cast<unsigned char>(text[2]);
            }
          }
          return static_cast<unsigned char>(text[1]);
        }

        const std::vector<Token>& mTokens;
        Messages& mMessages;
        int mLine;
        int mEndColumn;
        std::size_t mIndex = 0U;
      };

      Messages mMessages;
      Lexer mLexer;
      SourceTokens mSource;
      Printer mPrinter;
      std::string_view mSourceText;
      std::string mFile; // __FILE__: the path as a string literal, or the file of the last #line
      std::unordered_map<std::string, Definition> mMacros;
      std::vector<Group> mGroups;
      std::vector<Token> mDeferredNewlines; // newlines among an invocation's arguments
      std::vector<int> mContinuations;      // the lines the current directive went on past
      std::vector<std::string> mPragmaDefs;
      std::map<std::uint32_t, std::string> mPragmaWarnings;
    };
  } // namespace

  Output Preprocess(const std::string_view source, const std::string_view path, const std::vector<Macro>& macros)
  {
    Preprocessor preprocessor(source, path, macros);
    return preprocessor.Run();
  }
} // namespace faf::android::fxc
