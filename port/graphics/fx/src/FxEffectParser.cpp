#include "gpg/gal/fx/FxEffectParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <optional>
#include <utility>

namespace gpg::gal::fx {

  namespace {

    std::string Lower(std::string text)
    {
      std::transform(text.begin(), text.end(), text.begin(),
                     [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return text;
    }

    bool EqualsNoCase(const std::string& a, const char* b)
    {
      return Lower(a) == b;
    }

    std::optional<ScalarType> ScalarFromName(const std::string& name)
    {
      if (name == "void") return ScalarType::Void;
      if (name == "bool") return ScalarType::Bool;
      if (name == "int") return ScalarType::Int;
      if (name == "uint" || name == "dword" || name == "DWORD") return ScalarType::UInt;
      if (name == "half") return ScalarType::Half;
      if (name == "float") return ScalarType::Float;
      if (name == "double") return ScalarType::Double;
      return std::nullopt;
    }

    // float, float3, float4x4, half2, bool4 ... (dimensions 1-4).
    std::optional<TypeSpec> NumericTypeFromName(const std::string& word)
    {
      std::size_t end = 0;
      while (end < word.size() && std::isalpha(static_cast<unsigned char>(word[end]))) {
        ++end;
      }
      const std::optional<ScalarType> scalar = ScalarFromName(word.substr(0, end));
      if (!scalar) {
        return std::nullopt;
      }
      TypeSpec type;
      type.scalar = *scalar;
      type.spelling = word;
      const std::string rest = word.substr(end);
      if (rest.empty()) {
        type.shape = (*scalar == ScalarType::Void) ? TypeSpec::Shape::Void : TypeSpec::Shape::Scalar;
        return type;
      }
      if (*scalar == ScalarType::Void) {
        return std::nullopt;
      }
      auto digit = [](const char c) { return c >= '1' && c <= '4'; };
      if (rest.size() == 1 && digit(rest[0])) {
        type.shape = TypeSpec::Shape::Vector;
        type.rows = 1;
        type.columns = static_cast<std::uint8_t>(rest[0] - '0');
        return type;
      }
      if (rest.size() == 3 && digit(rest[0]) && rest[1] == 'x' && digit(rest[2])) {
        type.shape = TypeSpec::Shape::Matrix;
        type.rows = static_cast<std::uint8_t>(rest[0] - '0');
        type.columns = static_cast<std::uint8_t>(rest[2] - '0');
        return type;
      }
      return std::nullopt;
    }

    // The effect object types. fx_2_0 accepts these case-insensitively
    // ("Texture", "PixelShader").
    std::optional<ObjectType> ObjectTypeFromName(const std::string& word)
    {
      static const std::map<std::string, ObjectType> table = {
        {"string", ObjectType::String},           {"texture", ObjectType::Texture},
        {"texture1d", ObjectType::Texture1D},     {"texture2d", ObjectType::Texture2D},
        {"texture3d", ObjectType::Texture3D},     {"texturecube", ObjectType::TextureCube},
        {"sampler", ObjectType::Sampler},         {"sampler1d", ObjectType::Sampler1D},
        {"sampler2d", ObjectType::Sampler2D},     {"sampler3d", ObjectType::Sampler3D},
        {"samplercube", ObjectType::SamplerCube}, {"pixelshader", ObjectType::PixelShader},
        {"vertexshader", ObjectType::VertexShader},
      };
      const auto it = table.find(Lower(word));
      return (it == table.end()) ? std::nullopt : std::optional<ObjectType>(it->second);
    }

    std::string Unquote(const Token& token)
    {
      std::string text;
      const std::string& raw = token.text;
      for (std::size_t i = 1; i + 1 < raw.size(); ++i) {
        char c = raw[i];
        if (c == '\\' && i + 2 < raw.size()) {
          const char e = raw[++i];
          switch (e) {
          case 'n': c = '\n'; break;
          case 't': c = '\t'; break;
          case 'r': c = '\r'; break;
          case '0': c = '\0'; break;
          default: c = e; break;
          }
        }
        text += c;
      }
      return text;
    }

    class Parser
    {
    public:
      Parser(const std::vector<Token>& tokens, EffectAst& ast)
        : mTokens(tokens)
        , mAst(ast)
      {}

      void Run()
      {
        while (!AtEnd()) {
          if (AcceptPunct(";")) {
            continue;
          }
          const Token& token = Peek();
          if (token.kind == TokenKind::Identifier && EqualsNoCase(token.text, "technique")) {
            ParseTechnique();
            continue;
          }
          if (token.kind == TokenKind::Identifier &&
              (EqualsNoCase(token.text, "technique10") || EqualsNoCase(token.text, "technique11"))) {
            throw FxError(token.where, "'" + token.text + "' is not part of the D3D9 effect dialect");
          }
          if (token.IsIdent("typedef")) {
            ParseTypedef();
            continue;
          }
          ParseDeclaration();
        }
      }

    private:
      // ---- token cursor ------------------------------------------------------

      const Token& Peek(const std::size_t ahead = 0) const
      {
        const std::size_t index = std::min(mPos + ahead, mTokens.size() - 1);
        return mTokens[index];
      }

      bool AtEnd() const { return Peek().kind == TokenKind::End; }

      const Token& Next()
      {
        const Token& token = Peek();
        if (mPos + 1 < mTokens.size()) {
          ++mPos;
        }
        return token;
      }

      bool AcceptPunct(const char* text)
      {
        if (Peek().IsPunct(text)) {
          Next();
          return true;
        }
        return false;
      }

      void ExpectPunct(const char* text, const char* context)
      {
        if (!AcceptPunct(text)) {
          throw Unexpected(std::string("'") + text + "' " + context);
        }
      }

      FxError Unexpected(const std::string& expected) const
      {
        const Token& token = Peek();
        const std::string found = (token.kind == TokenKind::End) ? "end of file" : "'" + token.text + "'";
        return FxError(token.where, "syntax error: unexpected " + found + ", expected " + expected);
      }

      std::string ExpectIdentifier(const char* context)
      {
        if (Peek().kind != TokenKind::Identifier) {
          throw Unexpected(std::string("an identifier ") + context);
        }
        return Next().text;
      }

      // Skips a balanced (), [] or {} group starting at the opening token and
      // returns its contents as written.
      std::string SkipGroup(const char* open, const char* close)
      {
        const SourceLocation where = Peek().where;
        ExpectPunct(open, "");
        const std::size_t begin = mPos;
        int depth = 1;
        while (depth > 0) {
          if (AtEnd()) {
            throw FxError(where, std::string("unmatched '") + open + "'");
          }
          const Token& token = Next();
          if (token.IsPunct(open)) {
            ++depth;
          } else if (token.IsPunct(close)) {
            --depth;
          }
        }
        return SpellTokens(mTokens, begin, mPos - 1);
      }

      // ---- types ---------------------------------------------------------------

      bool IsTypeStart(const std::size_t ahead = 0) const
      {
        const Token& token = Peek(ahead);
        if (token.kind != TokenKind::Identifier) {
          return false;
        }
        if (token.text == "struct" || token.text == "vector" || token.text == "matrix") {
          return true;
        }
        if (NumericTypeFromName(token.text) || ObjectTypeFromName(token.text)) {
          return true;
        }
        return mTypedefs.count(token.text) != 0 || mStructs.count(token.text) != 0;
      }

      int ParseSmallInt(const char* context)
      {
        const Token& token = Peek();
        if (token.kind != TokenKind::Number) {
          throw Unexpected(std::string("a number ") + context);
        }
        Next();
        return std::atoi(token.text.c_str());
      }

      TypeSpec ParseType()
      {
        const Token& token = Peek();
        if (token.kind != TokenKind::Identifier) {
          throw Unexpected("a type");
        }
        if (token.text == "struct") {
          Next();
          const StructDef* def = ParseStructBody();
          TypeSpec type;
          type.shape = TypeSpec::Shape::Struct;
          type.structDef = def;
          type.spelling = def->name.empty() ? "struct" : def->name;
          return type;
        }
        if (token.text == "vector" || token.text == "matrix") {
          const bool isMatrix = token.text == "matrix";
          Next();
          TypeSpec type;
          type.scalar = ScalarType::Float;
          type.shape = isMatrix ? TypeSpec::Shape::Matrix : TypeSpec::Shape::Vector;
          type.rows = isMatrix ? 4 : 1;
          type.columns = 4;
          type.spelling = isMatrix ? "matrix" : "vector";
          if (AcceptPunct("<")) {
            const std::string scalarName = ExpectIdentifier("in a vector/matrix template");
            const std::optional<ScalarType> scalar = ScalarFromName(scalarName);
            if (!scalar || *scalar == ScalarType::Void) {
              throw FxError(token.where, "invalid component type '" + scalarName + "'");
            }
            type.scalar = *scalar;
            ExpectPunct(",", "in a vector/matrix template");
            if (isMatrix) {
              type.rows = static_cast<std::uint8_t>(ParseSmallInt("(matrix rows)"));
              ExpectPunct(",", "in a matrix template");
            }
            type.columns = static_cast<std::uint8_t>(ParseSmallInt("(vector size)"));
            ExpectPunct(">", "to close a vector/matrix template");
            type.spelling += "<...>";
          }
          return type;
        }
        if (std::optional<TypeSpec> numeric = NumericTypeFromName(token.text)) {
          Next();
          return *numeric;
        }
        if (const std::optional<ObjectType> object = ObjectTypeFromName(token.text)) {
          Next();
          TypeSpec type;
          type.shape = TypeSpec::Shape::Object;
          type.object = *object;
          type.spelling = token.text;
          return type;
        }
        if (const auto alias = mTypedefs.find(token.text); alias != mTypedefs.end()) {
          Next();
          return alias->second;
        }
        if (const auto def = mStructs.find(token.text); def != mStructs.end()) {
          Next();
          TypeSpec type;
          type.shape = TypeSpec::Shape::Struct;
          type.structDef = def->second;
          type.spelling = token.text;
          return type;
        }
        throw FxError(token.where, "unknown type '" + token.text + "'");
      }

      // After `struct`: [name] [{ members }]. A bare `struct Name` refers to an
      // already declared struct.
      const StructDef* ParseStructBody()
      {
        std::string name;
        const SourceLocation where = Peek().where;
        if (Peek().kind == TokenKind::Identifier) {
          name = Next().text;
        }
        if (!Peek().IsPunct("{")) {
          const auto found = mStructs.find(name);
          if (found == mStructs.end()) {
            throw FxError(where, "unknown struct '" + name + "'");
          }
          return found->second;
        }
        Next();
        auto def = std::make_unique<StructDef>();
        def->name = name;
        def->where = where;
        while (!AcceptPunct("}")) {
          if (AtEnd()) {
            throw FxError(where, "unterminated struct '" + name + "'");
          }
          SkipInterpolationModifiers();
          const TypeSpec memberType = ParseType();
          for (;;) {
            StructMember member;
            member.where = Peek().where;
            member.type = memberType;
            member.name = ExpectIdentifier("(struct member name)");
            member.dims = ParseArrayDims();
            if (AcceptPunct(":")) {
              member.semantic = ExpectIdentifier("(semantic)");
            }
            def->members.push_back(std::move(member));
            if (AcceptPunct(",")) {
              continue;
            }
            ExpectPunct(";", "after a struct member");
            break;
          }
        }
        const StructDef* result = def.get();
        if (!name.empty()) {
          mStructs[name] = result;
        }
        mAst.structs.push_back(std::move(def));
        return result;
      }

      void SkipInterpolationModifiers()
      {
        static const char* const kModifiers[] = {"linear", "centroid", "nointerpolation", "noperspective", "sample",
                                                 "const", "row_major", "column_major", "uniform", "in", "out", "inout"};
        for (;;) {
          const Token& token = Peek();
          bool skipped = false;
          for (const char* modifier : kModifiers) {
            if (token.IsIdent(modifier)) {
              Next();
              skipped = true;
              break;
            }
          }
          if (!skipped) {
            return;
          }
        }
      }

      std::vector<ArrayDim> ParseArrayDims()
      {
        std::vector<ArrayDim> dims;
        while (AcceptPunct("[")) {
          ArrayDim dim;
          if (!Peek().IsPunct("]")) {
            dim.size = ParseExpression(false);
          }
          ExpectPunct("]", "to close an array size");
          dims.push_back(std::move(dim));
        }
        return dims;
      }

      void ParseTypedef()
      {
        Next(); // typedef
        while (Peek().IsIdent("const") || Peek().IsIdent("row_major") || Peek().IsIdent("column_major")) {
          Next();
        }
        const TypeSpec type = ParseType();
        for (;;) {
          const Token& nameToken = Peek();
          const std::string name = ExpectIdentifier("(typedef name)");
          if (Peek().IsPunct("[")) {
            throw FxError(nameToken.where, "array typedefs are not supported");
          }
          TypeSpec alias = type;
          mTypedefs[name] = alias;
          if (AcceptPunct(",")) {
            continue;
          }
          ExpectPunct(";", "after a typedef");
          return;
        }
      }

      // ---- declarations ----------------------------------------------------------

      void ParseDeclaration()
      {
        VariableDecl modifiers;
        modifiers.where = Peek().where;
        for (;;) {
          const Token& token = Peek();
          if (token.kind != TokenKind::Identifier) {
            break;
          }
          if (token.text == "static") {
            modifiers.isStatic = true;
          } else if (token.text == "uniform") {
            modifiers.isUniform = true;
          } else if (token.text == "extern") {
            modifiers.isExtern = true;
          } else if (token.text == "shared") {
            modifiers.isShared = true;
          } else if (token.text == "const") {
            modifiers.isConst = true;
          } else if (token.text == "volatile") {
            modifiers.isVolatile = true;
          } else if (token.text == "row_major") {
            modifiers.rowMajor = true;
          } else if (token.text == "column_major") {
            modifiers.columnMajor = true;
          } else if (token.text == "inline") {
            // functions only; harmless to accept here
          } else {
            break;
          }
          Next();
        }
        const bool namedStructDefinition =
          Peek().IsIdent("struct") && (Peek(1).IsPunct("{") || Peek(2).IsPunct("{"));
        const TypeSpec type = ParseType();
        if (namedStructDefinition && AcceptPunct(";")) {
          return; // struct S { ... };
        }
        const SourceLocation nameWhere = Peek().where;
        const std::string name = ExpectIdentifier("(declaration name)");
        if (Peek().IsPunct("(")) {
          ParseFunctionRest(type, name, nameWhere);
          return;
        }
        std::string currentName = name;
        SourceLocation currentWhere = nameWhere;
        for (;;) {
          VariableDecl decl;
          decl.isStatic = modifiers.isStatic;
          decl.isUniform = modifiers.isUniform;
          decl.isExtern = modifiers.isExtern;
          decl.isShared = modifiers.isShared;
          decl.isConst = modifiers.isConst;
          decl.isVolatile = modifiers.isVolatile;
          decl.rowMajor = modifiers.rowMajor;
          decl.columnMajor = modifiers.columnMajor;
          decl.type = type;
          decl.name = currentName;
          decl.where = currentWhere;
          ParseDeclaratorRest(decl);
          mAst.globals.push_back(std::move(decl));
          if (AcceptPunct(",")) {
            currentWhere = Peek().where;
            currentName = ExpectIdentifier("(declaration name)");
            continue;
          }
          ExpectPunct(";", "after a declaration");
          return;
        }
      }

      void ParseFunctionRest(const TypeSpec& returnType, const std::string& name, const SourceLocation where)
      {
        FunctionDecl function;
        function.name = name;
        function.returnType = returnType;
        function.where = where;
        function.parameters = SkipGroup("(", ")");
        if (AcceptPunct(":")) {
          function.semantic = ExpectIdentifier("(function semantic)");
        }
        if (Peek().IsPunct("{")) {
          SkipGroup("{", "}");
          function.hasBody = true;
        } else {
          ExpectPunct(";", "after a function declaration");
        }
        mAst.functions.push_back(std::move(function));
      }

      void ParseDeclaratorRest(VariableDecl& decl)
      {
        decl.dims = ParseArrayDims();
        for (;;) {
          if (AcceptPunct(":")) {
            const Token& token = Peek();
            if (token.IsIdent("register") || token.IsIdent("packoffset")) {
              Next();
              decl.registerBinding = token.text + "(" + SkipGroup("(", ")") + ")";
            } else {
              decl.semantic = ExpectIdentifier("(semantic)");
            }
            continue;
          }
          if (Peek().IsPunct("<")) {
            decl.annotations = ParseAnnotations();
            continue;
          }
          break;
        }
        if (!AcceptPunct("=")) {
          return;
        }
        const Token& token = Peek();
        if (token.kind == TokenKind::Identifier && EqualsNoCase(token.text, "sampler_state")) {
          Next();
          decl.hasSamplerState = true;
          ExpectPunct("{", "after sampler_state");
          while (!AcceptPunct("}")) {
            if (AtEnd()) {
              throw FxError(token.where, "unterminated sampler_state");
            }
            decl.samplerStates.push_back(ParseStateAssignment());
          }
          return;
        }
        decl.init = ParseInitializer();
      }

      ExprPtr ParseInitializer()
      {
        if (Peek().IsPunct("{")) {
          return ParseInitList();
        }
        return ParseExpression(false);
      }

      ExprPtr ParseInitList()
      {
        auto list = std::make_unique<Expr>();
        list->kind = ExprKind::InitList;
        list->where = Peek().where;
        const std::size_t begin = mPos;
        ExpectPunct("{", "");
        while (!AcceptPunct("}")) {
          list->operands.push_back(ParseInitializer());
          if (AcceptPunct(",")) {
            continue;
          }
          ExpectPunct("}", "to close an initializer list");
          break;
        }
        list->spelling = SpellTokens(mTokens, begin, mPos);
        return list;
      }

      std::vector<Annotation> ParseAnnotations()
      {
        std::vector<Annotation> annotations;
        const SourceLocation where = Peek().where;
        ExpectPunct("<", "");
        while (!AcceptPunct(">")) {
          if (AtEnd()) {
            throw FxError(where, "unterminated annotation list");
          }
          Annotation annotation;
          annotation.where = Peek().where;
          while (Peek().IsIdent("const") || Peek().IsIdent("static") || Peek().IsIdent("uniform")) {
            Next();
          }
          annotation.type = ParseType();
          annotation.name = ExpectIdentifier("(annotation name)");
          annotation.dims = ParseArrayDims();
          ExpectPunct("=", "in an annotation");
          annotation.value = ParseInitializer();
          ExpectPunct(";", "after an annotation");
          annotations.push_back(std::move(annotation));
        }
        return annotations;
      }

      // ---- techniques ------------------------------------------------------------

      void ParseTechnique()
      {
        TechniqueDecl technique;
        technique.where = Next().where; // technique
        if (Peek().kind == TokenKind::Identifier) {
          technique.name = Next().text;
          technique.named = true;
        }
        if (Peek().IsPunct("<")) {
          technique.annotations = ParseAnnotations();
        }
        ExpectPunct("{", "to open a technique");
        while (!AcceptPunct("}")) {
          const Token& token = Peek();
          if (!(token.kind == TokenKind::Identifier && EqualsNoCase(token.text, "pass"))) {
            throw Unexpected("'pass' or '}'");
          }
          technique.passes.push_back(ParsePass());
        }
        mAst.techniques.push_back(std::move(technique));
      }

      PassDecl ParsePass()
      {
        PassDecl pass;
        pass.where = Next().where; // pass
        if (Peek().kind == TokenKind::Identifier) {
          pass.name = Next().text;
          pass.named = true;
        }
        if (Peek().IsPunct("<")) {
          pass.annotations = ParseAnnotations();
        }
        ExpectPunct("{", "to open a pass");
        while (!AcceptPunct("}")) {
          if (AtEnd()) {
            throw FxError(pass.where, "unterminated pass");
          }
          pass.states.push_back(ParseStateAssignment());
        }
        return pass;
      }

      StateAssignment ParseStateAssignment()
      {
        StateAssignment state;
        state.where = Peek().where;
        state.name = ExpectIdentifier("(state name)");
        if (AcceptPunct("[")) {
          state.index = ParseExpression(false);
          ExpectPunct("]", "after a state index");
        }
        ExpectPunct("=", "in a state assignment");
        state.value = ParseStateValue();
        ExpectPunct(";", "after a state assignment");
        return state;
      }

      ExprPtr ParseStateValue()
      {
        if (Peek().IsPunct("{")) {
          return ParseInitList();
        }
        return ParseExpression(true);
      }

      // compile <profile> <entry>(<args>), in pass states and in shader
      // parameter initializers (`VertexShader vs = compile vs_1_1 VS();`).
      ExprPtr ParseCompile()
      {
        auto compile = std::make_unique<Expr>();
        compile->kind = ExprKind::Compile;
        const std::size_t begin = mPos;
        compile->where = Next().where;
        compile->text = ExpectIdentifier("(shader profile)");
        compile->entry = ExpectIdentifier("(shader entry point)");
        ExpectPunct("(", "after the entry point");
        while (!AcceptPunct(")")) {
          compile->operands.push_back(ParseExpression(false));
          if (AcceptPunct(",")) {
            continue;
          }
          ExpectPunct(")", "to close the entry point arguments");
          break;
        }
        compile->spelling = SpellTokens(mTokens, begin, mPos);
        return compile;
      }

      // ---- expressions -----------------------------------------------------------

      ExprPtr MakeExpr(const ExprKind kind, const SourceLocation where, std::string text = {})
      {
        auto expr = std::make_unique<Expr>();
        expr->kind = kind;
        expr->where = where;
        expr->text = std::move(text);
        return expr;
      }

      // `stateValue` allows <name> references and the null literal, as in
      // `Texture = <T>;` and `VertexShader = null;`.
      ExprPtr ParseExpression(const bool stateValue)
      {
        const std::size_t begin = mPos;
        ExprPtr expr = ParseConditional(stateValue);
        expr->spelling = SpellTokens(mTokens, begin, mPos);
        return expr;
      }

      ExprPtr ParseConditional(const bool stateValue)
      {
        ExprPtr condition = ParseBinary(0, stateValue);
        if (Peek().IsPunct("?")) {
          const SourceLocation where = Next().where;
          ExprPtr whenTrue = ParseConditional(stateValue);
          ExpectPunct(":", "in a conditional expression");
          ExprPtr whenFalse = ParseConditional(stateValue);
          ExprPtr expr = MakeExpr(ExprKind::Ternary, where);
          expr->operands.push_back(std::move(condition));
          expr->operands.push_back(std::move(whenTrue));
          expr->operands.push_back(std::move(whenFalse));
          return expr;
        }
        return condition;
      }

      static int BinaryPrecedence(const Token& token)
      {
        if (token.kind != TokenKind::Punct) {
          return -1;
        }
        static const std::map<std::string, int> table = {
          {"||", 1}, {"&&", 2}, {"|", 3},  {"^", 4},  {"&", 5},  {"==", 6}, {"!=", 6}, {"<", 7},  {">", 7},
          {"<=", 7}, {">=", 7}, {"<<", 8}, {">>", 8}, {"+", 9},  {"-", 9},  {"*", 10}, {"/", 10}, {"%", 10},
        };
        const auto it = table.find(token.text);
        return (it == table.end()) ? -1 : it->second;
      }

      ExprPtr ParseBinary(const int minPrecedence, const bool stateValue)
      {
        ExprPtr left = ParseUnary(stateValue);
        for (;;) {
          const Token& token = Peek();
          const int precedence = BinaryPrecedence(token);
          if (precedence < 0 || precedence < minPrecedence) {
            return left;
          }
          const std::string op = token.text;
          const SourceLocation where = Next().where;
          ExprPtr right = ParseBinary(precedence + 1, stateValue);
          ExprPtr expr = MakeExpr(ExprKind::Binary, where, op);
          expr->operands.push_back(std::move(left));
          expr->operands.push_back(std::move(right));
          left = std::move(expr);
        }
      }

      ExprPtr ParseUnary(const bool stateValue)
      {
        const Token& token = Peek();
        if (token.kind == TokenKind::Punct &&
            (token.text == "-" || token.text == "+" || token.text == "!" || token.text == "~")) {
          const std::string op = token.text;
          const SourceLocation where = Next().where;
          ExprPtr operand = ParseUnary(stateValue);
          ExprPtr expr = MakeExpr(ExprKind::Unary, where, op);
          expr->operands.push_back(std::move(operand));
          return expr;
        }
        if (token.IsPunct("(") && IsTypeStart(1)) {
          // A cast if the type is followed by ')'.
          const std::size_t save = mPos;
          const SourceLocation where = Next().where;
          TypeSpec type = ParseType();
          if (AcceptPunct(")")) {
            ExprPtr operand = ParseUnary(stateValue);
            ExprPtr expr = MakeExpr(ExprKind::Cast, where);
            expr->type = std::move(type);
            expr->operands.push_back(std::move(operand));
            return expr;
          }
          mPos = save;
        }
        return ParsePostfix(stateValue);
      }

      ExprPtr ParsePostfix(const bool stateValue)
      {
        ExprPtr expr = ParsePrimary(stateValue);
        for (;;) {
          if (Peek().IsPunct(".")) {
            const SourceLocation where = Next().where;
            ExprPtr member = MakeExpr(ExprKind::Member, where, ExpectIdentifier("(member name)"));
            member->operands.push_back(std::move(expr));
            expr = std::move(member);
            continue;
          }
          if (Peek().IsPunct("[")) {
            const SourceLocation where = Next().where;
            ExprPtr index = MakeExpr(ExprKind::Index, where);
            index->operands.push_back(std::move(expr));
            index->operands.push_back(ParseExpression(false));
            ExpectPunct("]", "after an index");
            expr = std::move(index);
            continue;
          }
          return expr;
        }
      }

      void ParseArguments(Expr& call)
      {
        ExpectPunct("(", "");
        while (!AcceptPunct(")")) {
          call.operands.push_back(ParseExpression(false));
          if (AcceptPunct(",")) {
            continue;
          }
          ExpectPunct(")", "to close an argument list");
          break;
        }
      }

      ExprPtr ParsePrimary(const bool stateValue)
      {
        const Token& token = Peek();
        switch (token.kind) {
        case TokenKind::Number:
          Next();
          return MakeExpr(ExprKind::Number, token.where, token.text);
        case TokenKind::String: {
          // Adjacent string literals concatenate.
          std::string text;
          const SourceLocation where = token.where;
          while (Peek().kind == TokenKind::String) {
            text += Unquote(Next());
          }
          return MakeExpr(ExprKind::String, where, text);
        }
        case TokenKind::Identifier:
          break;
        case TokenKind::Punct:
          if (token.IsPunct("(")) {
            Next();
            ExprPtr inner = ParseConditional(stateValue);
            ExpectPunct(")", "to close a parenthesis");
            return inner;
          }
          if (token.IsPunct("{")) {
            return ParseInitList();
          }
          if (stateValue && token.IsPunct("<")) {
            const SourceLocation where = Next().where;
            ExprPtr ref = MakeExpr(ExprKind::AngleRef, where, ExpectIdentifier("(parameter name)"));
            ExpectPunct(">", "after a parameter reference");
            return ref;
          }
          throw Unexpected("an expression");
        default:
          throw Unexpected("an expression");
        }

        if (token.text == "true" || token.text == "false") {
          Next();
          return MakeExpr(ExprKind::Bool, token.where, token.text);
        }
        if (token.text == "null" || token.text == "NULL") {
          Next();
          return MakeExpr(ExprKind::Null, token.where);
        }
        if (token.text == "compile") {
          return ParseCompile();
        }
        if (EqualsNoCase(token.text, "asm")) {
          ExprPtr block = MakeExpr(ExprKind::Asm, Next().where);
          block->text = SkipGroup("{", "}");
          return block;
        }
        const bool templated = (token.text == "vector" || token.text == "matrix") && Peek(1).IsPunct("<");
        if (IsTypeStart() && !mStructs.count(token.text) && token.text != "struct" &&
            (Peek(1).IsPunct("(") || templated)) {
          // A constructor: float4(...), vector<float,2>(...), or a typedef
          // name. Without the parenthesis a type word is a plain name: state
          // values such as `ColorArg1[0] = Texture;` use them.
          const SourceLocation where = token.where;
          TypeSpec type = ParseType();
          ExprPtr ctor = MakeExpr(ExprKind::Constructor, where);
          ctor->type = std::move(type);
          ParseArguments(*ctor);
          return ctor;
        }
        Next();
        if (Peek().IsPunct("(")) {
          ExprPtr call = MakeExpr(ExprKind::Call, token.where, token.text);
          ParseArguments(*call);
          return call;
        }
        return MakeExpr(ExprKind::Identifier, token.where, token.text);
      }

      const std::vector<Token>& mTokens;
      EffectAst& mAst;
      std::size_t mPos = 0;
      std::map<std::string, TypeSpec> mTypedefs;
      std::map<std::string, const StructDef*> mStructs;
    };

  } // namespace

  EffectAst ParseEffect(const std::vector<Token>& tokens, std::vector<Diagnostic>& diagnostics)
  {
    EffectAst ast;
    if (tokens.empty()) {
      return ast;
    }
    try {
      Parser(tokens, ast).Run();
    } catch (const FxError& error) {
      diagnostics.push_back({Diagnostic::Severity::Error, error.Where(), error.what()});
    }
    return ast;
  }

} // namespace gpg::gal::fx
