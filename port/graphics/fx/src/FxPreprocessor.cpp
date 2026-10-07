#include "gpg/gal/fx/FxPreprocessor.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iterator>
#include <map>
#include <optional>
#include <utility>

namespace gpg::gal::fx {

  namespace {

    struct Macro
    {
      std::string name;
      bool functionLike = false;
      bool variadic = false;
      std::vector<std::string> params; // "__VA_ARGS__" last when variadic
      std::vector<Token> body;
      SourceLocation where;
    };

    bool InHideSet(const HideSet& hide, const std::string& name)
    {
      return hide && std::binary_search(hide->begin(), hide->end(), name);
    }

    HideSet HideSetAdd(const HideSet& hide, const std::string& name)
    {
      if (InHideSet(hide, name)) {
        return hide;
      }
      auto names = hide ? std::make_shared<std::vector<std::string>>(*hide) : std::make_shared<std::vector<std::string>>();
      names->insert(std::upper_bound(names->begin(), names->end(), name), name);
      return names;
    }

    HideSet HideSetUnion(const HideSet& a, const HideSet& b)
    {
      if (!b || b->empty()) {
        return a;
      }
      if (!a || a->empty()) {
        return b;
      }
      auto names = std::make_shared<std::vector<std::string>>();
      std::set_union(a->begin(), a->end(), b->begin(), b->end(), std::back_inserter(*names));
      return names;
    }

    HideSet HideSetIntersect(const HideSet& a, const HideSet& b)
    {
      if (!a || !b) {
        return nullptr;
      }
      auto names = std::make_shared<std::vector<std::string>>();
      std::set_intersection(a->begin(), a->end(), b->begin(), b->end(), std::back_inserter(*names));
      return names;
    }

    std::string Stringize(const std::vector<Token>& tokens)
    {
      std::string text = "\"";
      for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0 && tokens[i].leadingSpace) {
          text += ' ';
        }
        const Token& token = tokens[i];
        if (token.kind == TokenKind::String || token.kind == TokenKind::CharLiteral) {
          for (const char c : token.text) {
            if (c == '"' || c == '\\') {
              text += '\\';
            }
            text += c;
          }
        } else {
          text += token.text;
        }
      }
      text += '"';
      return text;
    }

    // ---- #if expressions -------------------------------------------------

    class IfExpression
    {
    public:
      IfExpression(const std::vector<Token>& tokens, const SourceLocation where)
        : mTokens(tokens)
        , mWhere(where)
      {}

      std::int64_t Evaluate()
      {
        const std::int64_t value = Conditional();
        if (mPos != mTokens.size()) {
          throw FxError(mTokens[mPos].where, "unexpected '" + mTokens[mPos].text + "' in #if expression");
        }
        return value;
      }

    private:
      const Token* Peek() const { return (mPos < mTokens.size()) ? &mTokens[mPos] : nullptr; }

      bool Accept(const char* punct)
      {
        if (mPos < mTokens.size() && mTokens[mPos].IsPunct(punct)) {
          ++mPos;
          return true;
        }
        return false;
      }

      std::int64_t Conditional()
      {
        const std::int64_t condition = Binary(0);
        if (Accept("?")) {
          const std::int64_t whenTrue = Conditional();
          if (!Accept(":")) {
            throw FxError(mWhere, "missing ':' in #if expression");
          }
          const std::int64_t whenFalse = Conditional();
          return condition != 0 ? whenTrue : whenFalse;
        }
        return condition;
      }

      static int Precedence(const std::string& op)
      {
        static const std::map<std::string, int> table = {
          {"||", 1}, {"&&", 2}, {"|", 3},  {"^", 4},  {"&", 5},  {"==", 6}, {"!=", 6}, {"<", 7},  {">", 7},
          {"<=", 7}, {">=", 7}, {"<<", 8}, {">>", 8}, {"+", 9},  {"-", 9},  {"*", 10}, {"/", 10}, {"%", 10},
        };
        const auto it = table.find(op);
        return (it == table.end()) ? -1 : it->second;
      }

      std::int64_t Binary(const int minPrecedence)
      {
        std::int64_t left = Unary();
        for (;;) {
          const Token* token = Peek();
          if (token == nullptr || token->kind != TokenKind::Punct) {
            return left;
          }
          const int precedence = Precedence(token->text);
          if (precedence < 0 || precedence < minPrecedence) {
            return left;
          }
          const std::string op = token->text;
          const SourceLocation where = token->where;
          ++mPos;
          const std::int64_t right = Binary(precedence + 1);
          left = Apply(op, left, right, where);
        }
      }

      static std::int64_t Apply(const std::string& op, const std::int64_t l, const std::int64_t r, SourceLocation where)
      {
        if (op == "||") return (l != 0 || r != 0) ? 1 : 0;
        if (op == "&&") return (l != 0 && r != 0) ? 1 : 0;
        if (op == "|") return l | r;
        if (op == "^") return l ^ r;
        if (op == "&") return l & r;
        if (op == "==") return l == r ? 1 : 0;
        if (op == "!=") return l != r ? 1 : 0;
        if (op == "<") return l < r ? 1 : 0;
        if (op == ">") return l > r ? 1 : 0;
        if (op == "<=") return l <= r ? 1 : 0;
        if (op == ">=") return l >= r ? 1 : 0;
        if (op == "<<") return l << (r & 63);
        if (op == ">>") return l >> (r & 63);
        if (op == "+") return l + r;
        if (op == "-") return l - r;
        if (op == "*") return l * r;
        if (op == "/" || op == "%") {
          if (r == 0) {
            throw FxError(where, "division by zero in #if expression");
          }
          return (op == "/") ? l / r : l % r;
        }
        throw FxError(where, "unsupported operator '" + op + "' in #if expression");
      }

      std::int64_t Unary()
      {
        const Token* token = Peek();
        if (token == nullptr) {
          throw FxError(mWhere, "#if expression ends early");
        }
        if (token->kind == TokenKind::Punct) {
          const std::string op = token->text;
          if (op == "!" || op == "-" || op == "+" || op == "~") {
            ++mPos;
            const std::int64_t value = Unary();
            if (op == "!") return value == 0 ? 1 : 0;
            if (op == "-") return -value;
            if (op == "~") return ~value;
            return value;
          }
          if (op == "(") {
            ++mPos;
            const std::int64_t value = Conditional();
            if (!Accept(")")) {
              throw FxError(token->where, "missing ')' in #if expression");
            }
            return value;
          }
        }
        ++mPos;
        if (token->kind == TokenKind::Number) {
          return ParseInteger(*token);
        }
        if (token->kind == TokenKind::Identifier) {
          // C++ treats true/false as literals in #if; anything else left after
          // macro expansion is 0.
          return token->text == "true" ? 1 : 0;
        }
        if (token->kind == TokenKind::CharLiteral && token->text.size() >= 3) {
          return static_cast<unsigned char>(token->text[1]);
        }
        throw FxError(token->where, "unexpected '" + token->text + "' in #if expression");
      }

      static std::int64_t ParseInteger(const Token& token)
      {
        const std::string& text = token.text;
        std::size_t i = 0;
        int base = 10;
        if (text.size() > 1 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
          base = 16;
          i = 2;
        } else if (text.size() > 1 && text[0] == '0') {
          base = 8;
          i = 1;
        }
        std::uint64_t value = 0;
        for (; i < text.size(); ++i) {
          const char c = text[i];
          int digit = -1;
          if (c >= '0' && c <= '9') {
            digit = c - '0';
          } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
          } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
          }
          if (digit < 0 || digit >= base) {
            break;
          }
          value = value * static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(digit);
        }
        for (; i < text.size(); ++i) {
          const char c = text[i];
          if (c != 'u' && c != 'U' && c != 'l' && c != 'L') {
            throw FxError(token.where, "invalid integer '" + text + "' in #if expression");
          }
        }
        return static_cast<std::int64_t>(value);
      }

      const std::vector<Token>& mTokens;
      SourceLocation mWhere;
      std::size_t mPos = 0;
    };

    // ---- the preprocessor --------------------------------------------------

    struct Conditional
    {
      bool active = false; // this branch's text is emitted
      bool taken = false; // some branch of this #if has been taken
      bool parentActive = true;
      bool seenElse = false;
      SourceLocation where;
    };

    class Preprocessor
    {
    public:
      explicit Preprocessor(std::vector<Diagnostic>& diagnostics)
        : mDiagnostics(diagnostics)
      {}

      void DefineFromCaller(const MacroDefinition& definition)
      {
        Macro macro;
        macro.name = definition.name;
        macro.body = LexFragment(definition.value, {});
        if (!macro.body.empty()) {
          macro.body.front().leadingSpace = false;
        }
        mMacros[macro.name] = std::move(macro);
      }

      std::vector<Token> Run(const std::vector<Token>& lexed)
      {
        std::vector<Token> output;
        std::vector<Token> segment; // non-directive text since the last directive
        std::size_t i = 0;
        while (i < lexed.size() && lexed[i].kind != TokenKind::End) {
          // Collect one logical line.
          const std::size_t lineBegin = i;
          while (lexed[i].kind != TokenKind::Newline && lexed[i].kind != TokenKind::End) {
            ++i;
          }
          const std::size_t lineEnd = i;
          if (lexed[i].kind == TokenKind::Newline) {
            ++i;
          }
          if (lineBegin == lineEnd) {
            continue;
          }
          if (lexed[lineBegin].IsPunct("#")) {
            FlushSegment(segment, output);
            try {
              Directive(lexed, lineBegin + 1, lineEnd);
            } catch (const FxError& error) {
              Error(error.Where(), error.what());
            }
            continue;
          }
          if (Skipping()) {
            continue;
          }
          segment.insert(segment.end(), lexed.begin() + static_cast<std::ptrdiff_t>(lineBegin),
                         lexed.begin() + static_cast<std::ptrdiff_t>(lineEnd));
        }
        FlushSegment(segment, output);
        for (const Conditional& open : mConditionals) {
          Error(open.where, "#if without #endif");
        }
        Token end;
        end.kind = TokenKind::End;
        end.where = (i < lexed.size()) ? lexed[i].where : SourceLocation{};
        output.push_back(std::move(end));
        return output;
      }

    private:
      bool Skipping() const { return !mConditionals.empty() && !mConditionals.back().active; }

      void Error(const SourceLocation where, std::string message)
      {
        mDiagnostics.push_back({Diagnostic::Severity::Error, where, std::move(message)});
      }

      void Warning(const SourceLocation where, std::string message)
      {
        mDiagnostics.push_back({Diagnostic::Severity::Warning, where, std::move(message)});
      }

      void FlushSegment(std::vector<Token>& segment, std::vector<Token>& output)
      {
        if (segment.empty()) {
          return;
        }
        std::deque<Token> input(segment.begin(), segment.end());
        segment.clear();
        try {
          Expand(input, output);
        } catch (const FxError& error) {
          Error(error.Where(), error.what());
        }
      }

      // Prosser's expand(): rescans replacements in place by pushing them
      // back onto the front of the input.
      void Expand(std::deque<Token>& input, std::vector<Token>& output)
      {
        while (!input.empty()) {
          Token token = std::move(input.front());
          input.pop_front();
          if (token.kind == TokenKind::Identifier && !InHideSet(token.hide, token.text)) {
            const auto found = mMacros.find(token.text);
            if (found != mMacros.end()) {
              const Macro& macro = found->second;
              if (!macro.functionLike) {
                std::vector<Token> replacement =
                  Substitute(macro, {}, HideSetAdd(token.hide, macro.name), token.where);
                PushFront(input, std::move(replacement), token);
                continue;
              }
              if (!input.empty() && input.front().IsPunct("(")) {
                Token closing;
                std::vector<std::vector<Token>> args = CollectArguments(input, macro, token.where, closing);
                const HideSet hide = HideSetAdd(HideSetIntersect(token.hide, closing.hide), macro.name);
                std::vector<Token> replacement = Substitute(macro, args, hide, token.where);
                PushFront(input, std::move(replacement), token);
                continue;
              }
            }
          }
          output.push_back(std::move(token));
        }
      }

      static void PushFront(std::deque<Token>& input, std::vector<Token> replacement, const Token& invocation)
      {
        if (!replacement.empty()) {
          replacement.front().leadingSpace = invocation.leadingSpace;
        }
        input.insert(input.begin(), std::make_move_iterator(replacement.begin()),
                     std::make_move_iterator(replacement.end()));
      }

      std::vector<std::vector<Token>> CollectArguments(
        std::deque<Token>& input, const Macro& macro, const SourceLocation where, Token& closing
      )
      {
        input.pop_front(); // '('
        std::vector<std::vector<Token>> args(1);
        int depth = 0;
        for (;;) {
          if (input.empty()) {
            throw FxError(where, "unterminated invocation of macro '" + macro.name + "'");
          }
          Token token = std::move(input.front());
          input.pop_front();
          if (token.IsPunct("(")) {
            ++depth;
          } else if (token.IsPunct(")")) {
            if (depth == 0) {
              closing = std::move(token);
              break;
            }
            --depth;
          } else if (token.IsPunct(",") && depth == 0 &&
                     !(macro.variadic && args.size() >= macro.params.size())) {
            args.emplace_back();
            continue;
          }
          args.back().push_back(std::move(token));
        }
        // "M()" passes one empty argument; a macro without parameters takes none.
        if (macro.params.empty() && args.size() == 1 && args[0].empty()) {
          args.clear();
        }
        if (macro.variadic && args.size() + 1 == macro.params.size()) {
          args.emplace_back();
        }
        if (args.size() != macro.params.size()) {
          throw FxError(where, "macro '" + macro.name + "' expects " + std::to_string(macro.params.size()) +
                                 " arguments, got " + std::to_string(args.size()));
        }
        return args;
      }

      static int ParamIndex(const Macro& macro, const Token& token)
      {
        if (token.kind != TokenKind::Identifier) {
          return -1;
        }
        for (std::size_t i = 0; i < macro.params.size(); ++i) {
          if (macro.params[i] == token.text) {
            return static_cast<int>(i);
          }
        }
        return -1;
      }

      // Appends `token` to the last output token (##).
      void Paste(std::vector<Token>& output, const Token& token)
      {
        if (output.empty()) {
          output.push_back(token);
          return;
        }
        Token& left = output.back();
        const std::string text = left.text + token.text;
        std::vector<Token> pasted = LexFragment(text, left.where);
        if (pasted.size() != 1) {
          Warning(left.where, "pasting '" + left.text + "' and '" + token.text + "' does not give a valid token");
        }
        if (pasted.empty()) {
          output.pop_back();
          return;
        }
        const bool leadingSpace = left.leadingSpace;
        const HideSet hide = left.hide;
        const SourceLocation where = left.where;
        output.pop_back();
        for (std::size_t i = 0; i < pasted.size(); ++i) {
          pasted[i].hide = hide;
          pasted[i].where = where;
          pasted[i].leadingSpace = (i == 0) ? leadingSpace : true;
          output.push_back(std::move(pasted[i]));
        }
      }

      // Prosser's subst(): parameter replacement, # and ##, then the hide
      // set of the invocation added to every result token.
      std::vector<Token> Substitute(
        const Macro& macro, const std::vector<std::vector<Token>>& args, const HideSet& hide, const SourceLocation where
      )
      {
        std::vector<Token> output;
        const std::vector<Token>& body = macro.body;
        std::size_t i = 0;
        while (i < body.size()) {
          const Token& token = body[i];
          if (macro.functionLike && token.IsPunct("#") && i + 1 < body.size()) {
            const int param = ParamIndex(macro, body[i + 1]);
            if (param >= 0) {
              Token string;
              string.kind = TokenKind::String;
              string.text = Stringize(args[static_cast<std::size_t>(param)]);
              string.where = where;
              string.leadingSpace = token.leadingSpace;
              output.push_back(std::move(string));
              i += 2;
              continue;
            }
          }
          if (token.IsPunct("##") && i + 1 < body.size()) {
            const Token& next = body[i + 1];
            const int param = ParamIndex(macro, next);
            if (param >= 0) {
              const std::vector<Token>& actual = args[static_cast<std::size_t>(param)];
              if (!actual.empty()) {
                Paste(output, actual.front());
                output.insert(output.end(), actual.begin() + 1, actual.end());
              }
            } else {
              Token copy = next;
              copy.where = where;
              Paste(output, copy);
            }
            i += 2;
            continue;
          }
          const int param = ParamIndex(macro, token);
          if (param >= 0) {
            const std::vector<Token>& actual = args[static_cast<std::size_t>(param)];
            const bool pastedRight = (i + 1 < body.size()) && body[i + 1].IsPunct("##");
            if (pastedRight) {
              // An operand of ## is not macro-expanded.
              if (actual.empty()) {
                // Nothing to paste onto: the right operand stands alone.
                if (i + 2 < body.size()) {
                  const int right = ParamIndex(macro, body[i + 2]);
                  if (right >= 0) {
                    const std::vector<Token>& rightActual = args[static_cast<std::size_t>(right)];
                    output.insert(output.end(), rightActual.begin(), rightActual.end());
                  } else {
                    Token copy = body[i + 2];
                    copy.where = where;
                    output.push_back(std::move(copy));
                  }
                  i += 3;
                } else {
                  i += 2;
                }
                continue;
              }
              std::size_t first = output.size();
              output.insert(output.end(), actual.begin(), actual.end());
              if (first < output.size()) {
                output[first].leadingSpace = token.leadingSpace;
              }
              ++i;
              continue;
            }
            std::deque<Token> argument(actual.begin(), actual.end());
            std::vector<Token> expanded;
            Expand(argument, expanded);
            if (!expanded.empty()) {
              expanded.front().leadingSpace = token.leadingSpace;
            }
            output.insert(output.end(), std::make_move_iterator(expanded.begin()),
                          std::make_move_iterator(expanded.end()));
            ++i;
            continue;
          }
          Token copy = token;
          copy.where = where;
          output.push_back(std::move(copy));
          ++i;
        }
        for (Token& token : output) {
          token.hide = HideSetUnion(token.hide, hide);
          token.lineStart = false;
        }
        return output;
      }

      static std::optional<std::string> DirectiveName(const std::vector<Token>& line, std::size_t begin,
                                                      std::size_t end)
      {
        if (begin >= end) {
          return std::nullopt; // the null directive: a lone '#'
        }
        return line[begin].text;
      }

      void Directive(const std::vector<Token>& line, const std::size_t begin, const std::size_t end)
      {
        const std::optional<std::string> name = DirectiveName(line, begin, end);
        if (!name) {
          return;
        }
        const SourceLocation where = line[begin].where;
        const std::size_t argsBegin = begin + 1;

        if (*name == "if" || *name == "ifdef" || *name == "ifndef") {
          Conditional frame;
          frame.where = where;
          frame.parentActive = !Skipping();
          if (frame.parentActive) {
            bool value = false;
            if (*name == "if") {
              value = EvaluateIf(line, argsBegin, end, where);
            } else {
              if (argsBegin >= end || line[argsBegin].kind != TokenKind::Identifier) {
                throw FxError(where, "#" + *name + " needs a macro name");
              }
              const bool defined = mMacros.count(line[argsBegin].text) != 0;
              value = (*name == "ifdef") ? defined : !defined;
            }
            frame.active = value;
            frame.taken = value;
          }
          mConditionals.push_back(frame);
          return;
        }
        if (*name == "elif") {
          if (mConditionals.empty()) {
            throw FxError(where, "#elif without #if");
          }
          Conditional& frame = mConditionals.back();
          if (frame.seenElse) {
            throw FxError(where, "#elif after #else");
          }
          if (!frame.parentActive || frame.taken) {
            frame.active = false;
            return;
          }
          frame.active = EvaluateIf(line, argsBegin, end, where);
          frame.taken = frame.active;
          return;
        }
        if (*name == "else") {
          if (mConditionals.empty()) {
            throw FxError(where, "#else without #if");
          }
          Conditional& frame = mConditionals.back();
          if (frame.seenElse) {
            throw FxError(where, "#else after #else");
          }
          frame.seenElse = true;
          frame.active = frame.parentActive && !frame.taken;
          frame.taken = true;
          return;
        }
        if (*name == "endif") {
          if (mConditionals.empty()) {
            throw FxError(where, "#endif without #if");
          }
          mConditionals.pop_back();
          return;
        }
        if (Skipping()) {
          return;
        }
        if (*name == "define") {
          Define(line, argsBegin, end, where);
          return;
        }
        if (*name == "undef") {
          if (argsBegin >= end || line[argsBegin].kind != TokenKind::Identifier) {
            throw FxError(where, "#undef needs a macro name");
          }
          mMacros.erase(line[argsBegin].text);
          return;
        }
        if (*name == "include") {
          throw FxError(where, "#include is not supported: the engine compiles effects without an include handler "
                               "(D3D9Interfaces.cpp:1557)");
        }
        if (*name == "error") {
          throw FxError(where, "#error " + SpellTokens(line, argsBegin, end));
        }
        if (*name == "pragma" || *name == "line") {
          Warning(where, "#" + *name + " ignored");
          return;
        }
        throw FxError(where, "unknown preprocessor directive '#" + *name + "'");
      }

      void Define(const std::vector<Token>& line, std::size_t i, const std::size_t end, const SourceLocation where)
      {
        if (i >= end || line[i].kind != TokenKind::Identifier) {
          throw FxError(where, "#define needs a macro name");
        }
        Macro macro;
        macro.name = line[i].text;
        macro.where = line[i].where;
        ++i;
        if (i < end && line[i].IsPunct("(") && !line[i].leadingSpace) {
          macro.functionLike = true;
          ++i;
          bool expectName = true;
          for (;;) {
            if (i >= end) {
              throw FxError(where, "unterminated parameter list of macro '" + macro.name + "'");
            }
            const Token& token = line[i++];
            if (token.IsPunct(")")) {
              if (expectName && !macro.params.empty()) {
                throw FxError(token.where, "missing parameter name in macro '" + macro.name + "'");
              }
              break;
            }
            if (expectName && token.kind == TokenKind::Identifier) {
              macro.params.push_back(token.text);
              expectName = false;
            } else if (expectName && token.IsPunct("...")) {
              macro.params.push_back("__VA_ARGS__");
              macro.variadic = true;
              expectName = false;
            } else if (!expectName && token.IsPunct(",") && !macro.variadic) {
              expectName = true;
            } else {
              throw FxError(token.where, "unexpected '" + token.text + "' in parameters of macro '" + macro.name + "'");
            }
          }
        }
        macro.body.assign(line.begin() + static_cast<std::ptrdiff_t>(i), line.begin() + static_cast<std::ptrdiff_t>(end));
        if (!macro.body.empty()) {
          macro.body.front().leadingSpace = false;
        }
        for (Token& token : macro.body) {
          token.lineStart = false;
        }
        mMacros[macro.name] = std::move(macro);
      }

      bool EvaluateIf(const std::vector<Token>& line, std::size_t i, const std::size_t end, const SourceLocation where)
      {
        // `defined` is resolved before expansion.
        std::deque<Token> input;
        while (i < end) {
          const Token& token = line[i];
          if (token.IsIdent("defined")) {
            ++i;
            bool parenthesized = false;
            if (i < end && line[i].IsPunct("(")) {
              parenthesized = true;
              ++i;
            }
            if (i >= end || line[i].kind != TokenKind::Identifier) {
              throw FxError(where, "'defined' needs a macro name");
            }
            Token value;
            value.kind = TokenKind::Number;
            value.text = mMacros.count(line[i].text) != 0 ? "1" : "0";
            value.where = line[i].where;
            ++i;
            if (parenthesized) {
              if (i >= end || !line[i].IsPunct(")")) {
                throw FxError(where, "missing ')' after 'defined'");
              }
              ++i;
            }
            input.push_back(std::move(value));
            continue;
          }
          input.push_back(token);
          ++i;
        }
        std::vector<Token> expanded;
        Expand(input, expanded);
        if (expanded.empty()) {
          throw FxError(where, "#if with no expression");
        }
        return IfExpression(expanded, where).Evaluate() != 0;
      }

      std::vector<Diagnostic>& mDiagnostics;
      std::map<std::string, Macro> mMacros;
      std::vector<Conditional> mConditionals;
    };

  } // namespace

  std::vector<Token> Preprocess(
    const std::vector<Token>& lexed, const std::vector<MacroDefinition>& macros, std::vector<Diagnostic>& diagnostics
  )
  {
    Preprocessor preprocessor(diagnostics);
    for (const MacroDefinition& macro : macros) {
      preprocessor.DefineFromCaller(macro);
    }
    return preprocessor.Run(lexed);
  }

  std::string FormatPreprocessed(const std::vector<Token>& tokens)
  {
    std::string text;
    std::uint32_t line = 0;
    bool first = true;
    for (const Token& token : tokens) {
      if (token.kind == TokenKind::End) {
        break;
      }
      if (first || token.where.line != line) {
        if (!first) {
          text += '\n';
        }
        first = false;
        line = token.where.line;
      } else if (token.leadingSpace) {
        text += ' ';
      }
      text += token.text;
    }
    text += '\n';
    return text;
  }

} // namespace gpg::gal::fx
