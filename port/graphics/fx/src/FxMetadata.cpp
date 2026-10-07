#include "gpg/gal/fx/FxMetadata.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <set>

#include "gpg/gal/fx/FxEffectParser.h"
#include "gpg/gal/fx/FxLexer.h"

namespace gpg::gal::fx {

  namespace {

    // ---- values ----------------------------------------------------------------

    // The legacy compiler folds constants in double precision and rounds to
    // float only when it stores a value: literals are not rounded first
    // (float3(...) arguments neither), so 16777216.0 + 1.0 - 16777216.0 is 1
    // and 0.1 + 0.2 == 0.3 is false. Measured on 300 normalize(float3(...))
    // initializers: only double evaluation reproduces all of D3DX's words.
    // Integers are 32-bit and wrap (2147483647 + 2 gives -2147483647).

    std::int32_t TruncateToInt(const double d)
    {
      // What x86 cvttsd2si gives, which is what a C cast compiles to there:
      // NaN and out-of-range values become INT_MIN.
      if (!(d > -2147483649.0 && d < 2147483648.0)) {
        return INT_MIN;
      }
      return static_cast<std::int32_t>(d);
    }

    struct Component
    {
      enum class Kind : std::uint8_t
      {
        Bool,
        Int,
        Float
      };

      Kind kind = Kind::Int;
      std::int32_t i = 0;
      double d = 0.0;

      static Component Bool(const bool b)
      {
        Component c;
        c.kind = Kind::Bool;
        c.i = b ? 1 : 0;
        return c;
      }

      static Component Int(const std::int32_t value)
      {
        Component c;
        c.kind = Kind::Int;
        c.i = value;
        return c;
      }

      static Component Float(const double value)
      {
        Component c;
        c.kind = Kind::Float;
        c.d = value;
        return c;
      }

      [[nodiscard]] double AsDouble() const { return kind == Kind::Float ? d : static_cast<double>(i); }
      [[nodiscard]] std::int32_t AsInt() const { return kind == Kind::Float ? TruncateToInt(d) : i; }
      [[nodiscard]] bool AsBool() const { return kind == Kind::Float ? d != 0.0 : i != 0; }
    };

    struct Value
    {
      std::vector<Component> components;
      std::uint8_t rows = 1; // > 1 for matrices, for row indexing
      bool isString = false;
      std::string string;
    };

    // double -> float as the x86 conversion does it: round to nearest,
    // overflow to infinity (a plain cast is undefined for out-of-range values).
    float RoundToFloat(const double d)
    {
      constexpr double kOverflow = 3.4028235677973366e38; // FLT_MAX + half an ulp
      if (d >= kOverflow) {
        return std::numeric_limits<float>::infinity();
      }
      if (d <= -kOverflow) {
        return -std::numeric_limits<float>::infinity();
      }
      return static_cast<float>(d);
    }

    std::uint32_t FloatBits(const float f)
    {
      std::uint32_t bits = 0;
      std::memcpy(&bits, &f, sizeof(bits));
      return bits;
    }

    std::int32_t WrapAdd(const std::int32_t a, const std::int32_t b)
    {
      return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
    }

    std::int32_t WrapSub(const std::int32_t a, const std::int32_t b)
    {
      return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
    }

    std::int32_t WrapMul(const std::int32_t a, const std::int32_t b)
    {
      return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
    }

    Component ParseNumber(const std::string& text, const SourceLocation where)
    {
      const bool hex = text.size() > 1 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
      bool isFloat = false;
      if (!hex) {
        for (const char c : text) {
          if (c == '.' || c == 'e' || c == 'E') {
            isFloat = true;
          }
        }
        const char last = text.back();
        if (last == 'f' || last == 'F' || last == 'h' || last == 'H') {
          isFloat = true;
        }
      }
      if (isFloat) {
        std::string digits = text;
        const char last = digits.back();
        if (last == 'f' || last == 'F' || last == 'h' || last == 'H') {
          digits.pop_back();
        }
        char* end = nullptr;
        // Kept as a double: see the note on constant folding above.
        const double value = std::strtod(digits.c_str(), &end);
        if (end == nullptr || *end != '\0') {
          throw FxError(where, "invalid number '" + text + "'");
        }
        return Component::Float(value);
      }
      std::size_t i = 0;
      int base = 10;
      if (hex) {
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
        value = (value * static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(digit)) & 0xFFFFFFFFull;
      }
      for (; i < text.size(); ++i) {
        const char c = text[i];
        if (c != 'u' && c != 'U' && c != 'l' && c != 'L') {
          throw FxError(where, "invalid number '" + text + "'");
        }
      }
      return Component::Int(static_cast<std::int32_t>(static_cast<std::uint32_t>(value)));
    }

    ScalarType EffectiveScalar(const ScalarType scalar)
    {
      switch (scalar) {
      case ScalarType::Half:
      case ScalarType::Double:
        return ScalarType::Float; // stored as 32-bit float (measured: half 0.1 and double 0.1 read back as 0.1f)
      case ScalarType::UInt:
        return ScalarType::Int;
      default:
        return scalar;
      }
    }

    Component ConvertTo(const Component& c, const ScalarType scalar)
    {
      switch (EffectiveScalar(scalar)) {
      case ScalarType::Bool:
        return Component::Bool(c.AsBool());
      case ScalarType::Int:
        return Component::Int(c.AsInt());
      case ScalarType::Float:
        return Component::Float(c.AsDouble()); // no rounding until the value is stored
      default:
        return c;
      }
    }

    std::uint32_t ComponentWord(const Component& c)
    {
      switch (c.kind) {
      case Component::Kind::Bool:
        return c.i != 0 ? 1U : 0U;
      case Component::Kind::Int:
        return static_cast<std::uint32_t>(c.i);
      case Component::Kind::Float:
        return FloatBits(RoundToFloat(c.d));
      }
      return 0;
    }

    // ---- the builder ------------------------------------------------------------

    struct EvalContext
    {
      const StateInfo* state = nullptr; // enum names of this state resolve first
      bool* dynamic = nullptr; // set when a non-static parameter is read
    };

    class Builder
    {
    public:
      Builder(const EffectAst& ast, std::vector<Diagnostic>& diagnostics)
        : mAst(ast)
        , mDiagnostics(diagnostics)
      {
        for (const VariableDecl& global : ast.globals) {
          mGlobals[global.name] = &global;
        }
        for (const FunctionDecl& function : ast.functions) {
          mFunctions.insert(function.name);
        }
      }

      EffectMetadata Run()
      {
        EffectMetadata metadata;
        for (const VariableDecl& global : mAst.globals) {
          if (global.isStatic) {
            continue; // static globals are not effect parameters
          }
          metadata.parameters.push_back(BuildParameter(global));
        }
        for (const TechniqueDecl& technique : mAst.techniques) {
          metadata.techniques.push_back(BuildTechnique(technique));
        }
        return metadata;
      }

    private:
      void Warn(const SourceLocation where, std::string message)
      {
        mDiagnostics.push_back({Diagnostic::Severity::Warning, where, std::move(message)});
      }

      // ---- types ----------------------------------------------------------------

      std::uint32_t ArrayElements(const std::vector<ArrayDim>& dims, const SourceLocation where)
      {
        if (dims.empty()) {
          return 0;
        }
        if (dims.size() > 1) {
          throw FxError(where, "multi-dimensional arrays are not supported");
        }
        if (!dims[0].size) {
          return 0; // unsized: decided by the initializer
        }
        const Value size = Eval(*dims[0].size, {});
        if (size.components.size() != 1 || size.components[0].AsInt() <= 0) {
          throw FxError(where, "array size must be a positive integer constant");
        }
        return static_cast<std::uint32_t>(size.components[0].AsInt());
      }

      static std::uint32_t ObjectBytes(const ObjectType object)
      {
        switch (object) {
        case ObjectType::Sampler:
        case ObjectType::Sampler1D:
        case ObjectType::Sampler2D:
        case ObjectType::Sampler3D:
        case ObjectType::SamplerCube:
          return 0; // measured: D3DX reports 0 bytes for samplers
        default:
          return 4; // a pointer in the Win32 process (8 in an x64 D3DX)
        }
      }

      static ParameterType ObjectParameterType(const ObjectType object)
      {
        switch (object) {
        case ObjectType::String: return ParameterType::String;
        case ObjectType::Texture: return ParameterType::Texture;
        case ObjectType::Texture1D: return ParameterType::Texture1D;
        case ObjectType::Texture2D: return ParameterType::Texture2D;
        case ObjectType::Texture3D: return ParameterType::Texture3D;
        case ObjectType::TextureCube: return ParameterType::TextureCube;
        case ObjectType::Sampler: return ParameterType::Sampler;
        case ObjectType::Sampler1D: return ParameterType::Sampler1D;
        case ObjectType::Sampler2D: return ParameterType::Sampler2D;
        case ObjectType::Sampler3D: return ParameterType::Sampler3D;
        case ObjectType::SamplerCube: return ParameterType::SamplerCube;
        case ObjectType::PixelShader: return ParameterType::PixelShader;
        case ObjectType::VertexShader: return ParameterType::VertexShader;
        default: return ParameterType::Void;
        }
      }

      // Bytes of one element.
      std::uint32_t ElementBytes(const TypeSpec& type, const SourceLocation where)
      {
        switch (type.shape) {
        case TypeSpec::Shape::Scalar:
        case TypeSpec::Shape::Vector:
        case TypeSpec::Shape::Matrix:
          return 4U * type.rows * type.columns;
        case TypeSpec::Shape::Object:
          return ObjectBytes(type.object);
        case TypeSpec::Shape::Struct: {
          std::uint32_t bytes = 0;
          for (const StructMember& member : type.structDef->members) {
            const std::uint32_t elements = ArrayElements(member.dims, member.where);
            bytes += ElementBytes(member.type, member.where) * std::max<std::uint32_t>(1, elements);
          }
          return bytes;
        }
        case TypeSpec::Shape::Void:
          break;
        }
        throw FxError(where, "a parameter cannot be void");
      }

      ParameterDesc Describe(
        const TypeSpec& type, const std::uint32_t elements, const std::string& name, const std::string& semantic,
        const SourceLocation where
      )
      {
        ParameterDesc desc;
        desc.name = name;
        desc.semantic = semantic;
        desc.elements = elements;
        switch (type.shape) {
        case TypeSpec::Shape::Scalar:
        case TypeSpec::Shape::Vector:
        case TypeSpec::Shape::Matrix:
          // Every matrix reads back as D3DXPC_MATRIX_ROWS, row_major or
          // column_major alike (measured with d3dx9_43 + the legacy compiler).
          desc.parameterClass = (type.shape == TypeSpec::Shape::Scalar)   ? ParameterClass::Scalar
                                : (type.shape == TypeSpec::Shape::Vector) ? ParameterClass::Vector
                                                                          : ParameterClass::MatrixRows;
          switch (EffectiveScalar(type.scalar)) {
          case ScalarType::Bool: desc.type = ParameterType::Bool; break;
          case ScalarType::Int: desc.type = ParameterType::Int; break;
          default: desc.type = ParameterType::Float; break;
          }
          desc.rows = type.rows;
          desc.columns = type.columns;
          break;
        case TypeSpec::Shape::Object:
          desc.parameterClass = ParameterClass::Object;
          desc.type = ObjectParameterType(type.object);
          break;
        case TypeSpec::Shape::Struct:
          desc.parameterClass = ParameterClass::Struct;
          desc.type = ParameterType::Void;
          desc.structMembers = static_cast<std::uint32_t>(type.structDef->members.size());
          break;
        case TypeSpec::Shape::Void:
          throw FxError(where, "'" + name + "' cannot be void");
        }
        desc.bytes = ElementBytes(type, where) * std::max<std::uint32_t>(1, elements);
        return desc;
      }

      // The scalar type of every component of one element, in GetValue order.
      void Layout(const TypeSpec& type, std::vector<ScalarType>& layout, const SourceLocation where)
      {
        if (type.IsNumeric()) {
          layout.insert(layout.end(), type.ComponentCount(), EffectiveScalar(type.scalar));
          return;
        }
        if (type.shape == TypeSpec::Shape::Struct) {
          for (const StructMember& member : type.structDef->members) {
            const std::uint32_t elements = std::max<std::uint32_t>(1, ArrayElements(member.dims, member.where));
            for (std::uint32_t e = 0; e < elements; ++e) {
              Layout(member.type, layout, member.where);
            }
          }
          return;
        }
        throw FxError(where, "'" + type.spelling + "' cannot hold a numeric value");
      }

      // Converts an initializer to the components of a declared type: kinds
      // converted, floats not yet rounded. `elements` 0 means not an array;
      // an unsized array is sized here.
      std::vector<Component> ToComponents(
        const TypeSpec& type, std::uint32_t& elements, const bool unsized, const Value& value, const SourceLocation where
      )
      {
        std::vector<ScalarType> element;
        Layout(type, element, where);
        if (element.empty()) {
          return {};
        }
        if (unsized) {
          if (value.components.empty() || value.components.size() % element.size() != 0) {
            throw FxError(where, "the initializer does not fill a whole number of array elements");
          }
          elements = static_cast<std::uint32_t>(value.components.size() / element.size());
        }
        const std::size_t count = element.size() * std::max<std::uint32_t>(1, elements);
        std::vector<Component> components = value.components;
        if (components.size() == 1 && count > 1 && elements == 0 && type.IsNumeric()) {
          components.assign(count, components[0]); // float4 v = 0.5; broadcasts (measured)
        }
        if (components.size() > count) {
          Warn(where, "initializer has " + std::to_string(components.size()) + " values for " + std::to_string(count) +
                        "; the rest is dropped");
          components.resize(count);
        }
        if (components.size() < count) {
          throw FxError(where, "initializer has " + std::to_string(components.size()) + " values for " +
                                 std::to_string(count));
        }
        for (std::size_t i = 0; i < count; ++i) {
          components[i] = ConvertTo(components[i], element[i % element.size()]);
        }
        return components;
      }

      // The GetValue words: floats rounded to single precision once, here.
      std::vector<std::uint32_t> ToWords(
        const TypeSpec& type, std::uint32_t& elements, const bool unsized, const Value& value, const SourceLocation where
      )
      {
        std::vector<std::uint32_t> words;
        for (const Component& component : ToComponents(type, elements, unsized, value, where)) {
          words.push_back(ComponentWord(component));
        }
        return words;
      }

      // ---- expression evaluation ---------------------------------------------

      Value EvalGlobal(const VariableDecl& global, const SourceLocation where)
      {
        const auto cached = mGlobalValues.find(global.name);
        if (cached != mGlobalValues.end()) {
          return cached->second;
        }
        if (!mEvaluating.insert(global.name).second) {
          throw FxError(where, "'" + global.name + "' refers to itself");
        }
        Value value;
        if (global.type.IsNumeric()) {
          // The referenced value is not rounded to float (measured: with
          // `static const float SA = 16777217.0;`, SA - 16777216.0 folds to 1).
          std::uint32_t elements = ArrayElements(global.dims, global.where);
          const bool unsized = !global.dims.empty() && !global.dims[0].size;
          if (global.init) {
            value.components = ToComponents(global.type, elements, unsized, Eval(*global.init, {}), global.init->where);
          } else {
            const Component zero = ConvertTo(Component::Int(0), global.type.scalar);
            value.components.assign(global.type.ComponentCount() * std::max<std::uint32_t>(1, elements), zero);
          }
          value.rows = (global.type.shape == TypeSpec::Shape::Matrix) ? global.type.rows : 1;
        } else if (global.type.shape == TypeSpec::Shape::Object && global.type.object == ObjectType::String) {
          value.isString = true;
          if (global.init) {
            value.string = Eval(*global.init, {}).string;
          }
        } else {
          mEvaluating.erase(global.name);
          throw FxError(where, "'" + global.name + "' has no constant value");
        }
        mEvaluating.erase(global.name);
        mGlobalValues[global.name] = value;
        return value;
      }

      static void Broadcast(std::vector<Component>& a, std::vector<Component>& b)
      {
        if (a.size() == 1 && b.size() > 1) {
          a.assign(b.size(), a[0]);
        } else if (b.size() == 1 && a.size() > 1) {
          b.assign(a.size(), b[0]);
        } else if (a.size() != b.size()) {
          const std::size_t n = std::min(a.size(), b.size());
          a.resize(n);
          b.resize(n);
        }
      }

      Value EvalBinary(const Expr& expr, const EvalContext& context)
      {
        Value left = Eval(*expr.operands[0], context);
        Value right = Eval(*expr.operands[1], context);
        if (left.components.empty() || right.components.empty()) {
          throw FxError(expr.where, "operator '" + expr.text + "' needs numeric operands");
        }
        Broadcast(left.components, right.components);
        const std::string& op = expr.text;
        Value result;
        result.rows = left.rows;
        for (std::size_t i = 0; i < left.components.size(); ++i) {
          const Component& a = left.components[i];
          const Component& b = right.components[i];
          const bool isFloat = a.kind == Component::Kind::Float || b.kind == Component::Kind::Float;
          Component c;
          if (op == "&&") {
            c = Component::Bool(a.AsBool() && b.AsBool());
          } else if (op == "||") {
            c = Component::Bool(a.AsBool() || b.AsBool());
          } else if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") {
            bool r = false;
            if (isFloat) {
              const double x = a.AsDouble();
              const double y = b.AsDouble();
              r = (op == "==") ? x == y : (op == "!=") ? x != y : (op == "<") ? x < y : (op == ">") ? x > y
                : (op == "<=") ? x <= y : x >= y;
            } else {
              const std::int32_t x = a.AsInt();
              const std::int32_t y = b.AsInt();
              r = (op == "==") ? x == y : (op == "!=") ? x != y : (op == "<") ? x < y : (op == ">") ? x > y
                : (op == "<=") ? x <= y : x >= y;
            }
            c = Component::Bool(r);
          } else if (op == "|" || op == "&" || op == "^" || op == "<<" || op == ">>") {
            // Only state values reach here: the legacy compiler rejects these
            // operators in initializers, but accepts flag lists such as
            // `ColorWriteEnable = RED|GREEN`.
            const auto x = static_cast<std::uint32_t>(a.AsInt());
            const auto y = static_cast<std::uint32_t>(b.AsInt());
            std::uint32_t r = 0;
            if (op == "|") r = x | y;
            if (op == "&") r = x & y;
            if (op == "^") r = x ^ y;
            if (op == "<<") r = x << (y & 31U);
            if (op == ">>") r = x >> (y & 31U);
            c = Component::Int(static_cast<std::int32_t>(r));
          } else if (isFloat) {
            const double x = a.AsDouble();
            const double y = b.AsDouble();
            double r = 0.0;
            if ((op == "/" || op == "%") && y == 0.0) {
              throw FxError(expr.where, "division by zero"); // D3DX: error X3033
            }
            if (op == "+") r = x + y;
            else if (op == "-") r = x - y;
            else if (op == "*") r = x * y;
            else if (op == "/") r = x / y;
            else if (op == "%") r = std::fmod(x, y);
            else throw FxError(expr.where, "unsupported operator '" + op + "'");
            c = Component::Float(r);
          } else {
            const std::int32_t x = a.AsInt();
            const std::int32_t y = b.AsInt();
            std::int32_t r = 0;
            if (op == "+") r = WrapAdd(x, y);
            else if (op == "-") r = WrapSub(x, y);
            else if (op == "*") r = WrapMul(x, y);
            else if (op == "/" || op == "%") {
              if (y == 0) {
                throw FxError(expr.where, "integer division by zero");
              }
              if (x == INT_MIN && y == -1) {
                r = (op == "/") ? INT_MIN : 0;
              } else {
                r = (op == "/") ? x / y : x % y;
              }
            } else {
              throw FxError(expr.where, "unsupported operator '" + op + "'");
            }
            c = Component::Int(r);
          }
          result.components.push_back(c);
        }
        return result;
      }

      Value EvalConstruct(const TypeSpec& type, const std::vector<Component>& source, const bool cast,
                          const SourceLocation where)
      {
        if (!type.IsNumeric()) {
          throw FxError(where, "cannot evaluate a '" + type.spelling + "' value");
        }
        const std::size_t count = type.ComponentCount();
        std::vector<Component> components = source;
        if (components.size() == 1 && count > 1) {
          components.assign(count, components[0]);
        } else if (cast && components.size() > count) {
          components.resize(count);
        } else if (components.size() != count) {
          throw FxError(where, "'" + type.spelling + "' needs " + std::to_string(count) + " values, got " +
                                 std::to_string(components.size()));
        }
        Value value;
        value.rows = (type.shape == TypeSpec::Shape::Matrix) ? type.rows : 1;
        for (const Component& c : components) {
          value.components.push_back(ConvertTo(c, type.scalar));
        }
        return value;
      }

      // Intrinsics in constant initializers, folded in double precision like
      // the operators (water2.fx: SunDirection = normalize(float3(...))).
      // Checked against D3DX by tests/effects/folding.fx, including the
      // legacy compiler's quirks: length(v) folds to |v.x| and distance(a, b)
      // to |b.x - a.x| (length(float2(3, 4)) is 3), round(x) is
      // floor(x + 0.5), and domain errors are compile errors (X3031 sqrt,
      // X3032 log, X3070/X3071 acos/asin).
      Value EvalIntrinsic(const Expr& expr, const EvalContext& context)
      {
        std::vector<Value> args;
        for (const ExprPtr& operand : expr.operands) {
          args.push_back(Eval(*operand, context));
          if (args.back().components.empty()) {
            throw FxError(operand->where, "'" + expr.text + "' needs numeric arguments");
          }
        }
        const std::string& name = expr.text;
        auto need = [&](const std::size_t count) {
          if (args.size() != count) {
            throw FxError(expr.where, "'" + name + "' takes " + std::to_string(count) + " arguments");
          }
        };
        auto domain = [&](const bool ok, const char* message) {
          if (!ok) {
            throw FxError(expr.where, message);
          }
        };
        auto scalar = [](const double x) {
          Value value;
          value.components.push_back(Component::Float(x));
          return value;
        };
        // Component-wise over the arguments, scalars broadcast.
        auto map = [&](const std::size_t arity, const auto& fn) {
          need(arity);
          std::size_t width = 1;
          for (const Value& arg : args) {
            width = std::max(width, arg.components.size());
          }
          Value value;
          value.rows = args[0].rows;
          for (std::size_t i = 0; i < width; ++i) {
            double x[3] = {};
            for (std::size_t a = 0; a < arity; ++a) {
              const std::vector<Component>& c = args[a].components;
              x[a] = c[std::min(i, c.size() - 1)].AsDouble();
            }
            value.components.push_back(Component::Float(fn(x[0], x[1], x[2])));
          }
          return value;
        };
        auto dot = [](const Value& a, const Value& b) {
          double sum = 0.0;
          for (std::size_t i = 0; i < a.components.size() && i < b.components.size(); ++i) {
            sum += a.components[i].AsDouble() * b.components[i].AsDouble();
          }
          return sum;
        };
        // radians/degrees use pi rounded to float (measured: radians(-1.6)
        // is 0xbce4c389 only with 3.1415927410125732).
        constexpr double kPi = 3.1415927410125732;

        if (name == "abs") return map(1, [](double x, double, double) { return std::fabs(x); });
        if (name == "floor") return map(1, [](double x, double, double) { return std::floor(x); });
        if (name == "ceil") return map(1, [](double x, double, double) { return std::ceil(x); });
        if (name == "frac") return map(1, [](double x, double, double) { return x - std::floor(x); });
        if (name == "round") return map(1, [](double x, double, double) { return std::floor(x + 0.5); });
        if (name == "sign") return map(1, [](double x, double, double) { return x > 0.0 ? 1.0 : (x < 0.0 ? -1.0 : 0.0); });
        if (name == "saturate") return map(1, [](double x, double, double) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); });
        if (name == "sqrt" || name == "rsqrt") {
          for (const Component& c : args.empty() ? std::vector<Component>{} : args[0].components) {
            domain(c.AsDouble() >= 0.0, "imaginary square root");
          }
          if (name == "sqrt") return map(1, [](double x, double, double) { return std::sqrt(x); });
          return map(1, [](double x, double, double) { return 1.0 / std::sqrt(x); });
        }
        if (name == "log" || name == "log2" || name == "log10") {
          for (const Component& c : args.empty() ? std::vector<Component>{} : args[0].components) {
            domain(c.AsDouble() > 0.0, "infinite/indefinite log");
          }
          if (name == "log") return map(1, [](double x, double, double) { return std::log(x); });
          if (name == "log2") return map(1, [](double x, double, double) { return std::log(x) / std::log(2.0); });
          return map(1, [](double x, double, double) { return std::log10(x); });
        }
        if (name == "asin" || name == "acos") {
          for (const Component& c : args.empty() ? std::vector<Component>{} : args[0].components) {
            domain(c.AsDouble() >= -1.0 && c.AsDouble() <= 1.0, "indefinite asin/acos");
          }
          if (name == "asin") return map(1, [](double x, double, double) { return std::asin(x); });
          return map(1, [](double x, double, double) { return std::acos(x); });
        }
        if (name == "exp") return map(1, [](double x, double, double) { return std::exp(x); });
        if (name == "exp2") return map(1, [](double x, double, double) { return std::pow(2.0, x); });
        if (name == "sin") return map(1, [](double x, double, double) { return std::sin(x); });
        if (name == "cos") return map(1, [](double x, double, double) { return std::cos(x); });
        if (name == "tan") return map(1, [](double x, double, double) { return std::tan(x); });
        if (name == "atan") return map(1, [](double x, double, double) { return std::atan(x); });
        if (name == "radians") return map(1, [](double x, double, double) { return x * (kPi / 180.0); });
        if (name == "degrees") return map(1, [](double x, double, double) { return x * (180.0 / kPi); });
        if (name == "min") return map(2, [](double x, double y, double) { return x < y ? x : y; });
        if (name == "max") return map(2, [](double x, double y, double) { return x > y ? x : y; });
        if (name == "pow") return map(2, [](double x, double y, double) { return std::pow(x, y); });
        if (name == "atan2") return map(2, [](double y, double x, double) { return std::atan2(y, x); });
        if (name == "fmod") return map(2, [](double x, double y, double) { return std::fmod(x, y); });
        if (name == "step") return map(2, [](double a, double x, double) { return x >= a ? 1.0 : 0.0; });
        if (name == "lerp") return map(3, [](double x, double y, double s) { return x + s * (y - x); });
        if (name == "clamp") {
          return map(3, [](double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); });
        }
        if (name == "dot") {
          need(2);
          return scalar(dot(args[0], args[1]));
        }
        if (name == "length") {
          need(1);
          return scalar(std::fabs(args[0].components[0].AsDouble()));
        }
        if (name == "distance") {
          need(2);
          return scalar(std::fabs(args[1].components[0].AsDouble() - args[0].components[0].AsDouble()));
        }
        if (name == "normalize") {
          // Measured: x / sqrt(dot(x, x)) in double matches D3DX on 300
          // random vectors; any single-precision step does not.
          need(1);
          const double length = std::sqrt(dot(args[0], args[0]));
          Value value = args[0];
          for (Component& c : value.components) {
            c = Component::Float(c.AsDouble() / length);
          }
          return value;
        }
        if (name == "cross") {
          need(2);
          if (args[0].components.size() != 3 || args[1].components.size() != 3) {
            throw FxError(expr.where, "cross needs two float3 values");
          }
          const auto a = [&](const std::size_t i) { return args[0].components[i].AsDouble(); };
          const auto b = [&](const std::size_t i) { return args[1].components[i].AsDouble(); };
          Value value;
          value.components.push_back(Component::Float(a(1) * b(2) - a(2) * b(1)));
          value.components.push_back(Component::Float(a(2) * b(0) - a(0) * b(2)));
          value.components.push_back(Component::Float(a(0) * b(1) - a(1) * b(0)));
          return value;
        }
        throw FxError(expr.where, "cannot evaluate a call to '" + name + "' as a constant");
      }

      Value Eval(const Expr& expr, const EvalContext& context)
      {
        switch (expr.kind) {
        case ExprKind::Number: {
          Value value;
          value.components.push_back(ParseNumber(expr.text, expr.where));
          return value;
        }
        case ExprKind::Bool: {
          Value value;
          value.components.push_back(Component::Bool(expr.text == "true"));
          return value;
        }
        case ExprKind::String: {
          Value value;
          value.isString = true;
          value.string = expr.text;
          return value;
        }
        case ExprKind::Identifier:
        case ExprKind::AngleRef: {
          if (context.state != nullptr) {
            if (const std::optional<std::uint32_t> named = FindStateValue(*context.state, expr.text)) {
              Value value;
              value.components.push_back(Component::Int(static_cast<std::int32_t>(*named)));
              return value;
            }
          }
          const auto global = mGlobals.find(expr.text);
          if (global == mGlobals.end()) {
            if (context.state != nullptr) {
              // D3DX's wording: "State 'ZENABLE' does not accept 'Bogus' as a value".
              throw FxError(expr.where, std::string("state '") + context.state->name + "' does not accept '" +
                                          expr.text + "' as a value");
            }
            throw FxError(expr.where, "unknown name '" + expr.text + "'");
          }
          if (!global->second->isStatic && context.dynamic != nullptr) {
            *context.dynamic = true; // D3DX re-evaluates it from the parameter at BeginPass
          }
          return EvalGlobal(*global->second, expr.where);
        }
        case ExprKind::Unary: {
          Value value = Eval(*expr.operands[0], context);
          for (Component& c : value.components) {
            if (expr.text == "!") {
              c = Component::Bool(!c.AsBool());
            } else if (expr.text == "~") {
              c = Component::Int(static_cast<std::int32_t>(~static_cast<std::uint32_t>(c.AsInt())));
            } else if (expr.text == "-") {
              c = (c.kind == Component::Kind::Float) ? Component::Float(-c.d) : Component::Int(WrapSub(0, c.AsInt()));
            } else if (c.kind == Component::Kind::Bool) {
              c = Component::Int(c.i);
            }
          }
          return value;
        }
        case ExprKind::Binary:
          return EvalBinary(expr, context);
        case ExprKind::Ternary: {
          const Value condition = Eval(*expr.operands[0], context);
          if (condition.components.empty()) {
            throw FxError(expr.where, "the condition must be numeric");
          }
          return Eval(*expr.operands[condition.components[0].AsBool() ? 1 : 2], context);
        }
        case ExprKind::Constructor: {
          std::vector<Component> components;
          for (const ExprPtr& operand : expr.operands) {
            const Value part = Eval(*operand, context);
            components.insert(components.end(), part.components.begin(), part.components.end());
          }
          if (expr.operands.size() > 1 && components.size() == 1) {
            throw FxError(expr.where, "bad constructor");
          }
          return EvalConstruct(expr.type, components, false, expr.where);
        }
        case ExprKind::Cast: {
          const Value source = Eval(*expr.operands[0], context);
          return EvalConstruct(expr.type, source.components, true, expr.where);
        }
        case ExprKind::InitList: {
          Value value;
          for (const ExprPtr& operand : expr.operands) {
            const Value part = Eval(*operand, context);
            if (part.isString) {
              throw FxError(operand->where, "a string in an initializer list is not supported");
            }
            value.components.insert(value.components.end(), part.components.begin(), part.components.end());
          }
          return value;
        }
        case ExprKind::Member: {
          const Value base = Eval(*expr.operands[0], context);
          Value value;
          if (expr.text.size() > 4) {
            throw FxError(expr.where, "invalid swizzle '." + expr.text + "'");
          }
          for (const char c : expr.text) {
            const char* sets[] = {"xyzw", "rgba"};
            int index = -1;
            for (const char* set : sets) {
              if (const char* p = std::strchr(set, c)) {
                index = static_cast<int>(p - set);
              }
            }
            if (index < 0 || static_cast<std::size_t>(index) >= base.components.size()) {
              throw FxError(expr.where, "invalid swizzle '." + expr.text + "'");
            }
            value.components.push_back(base.components[static_cast<std::size_t>(index)]);
          }
          return value;
        }
        case ExprKind::Index: {
          const Value base = Eval(*expr.operands[0], context);
          const Value index = Eval(*expr.operands[1], {});
          if (index.components.size() != 1) {
            throw FxError(expr.where, "the index must be a scalar");
          }
          const std::int32_t i = index.components[0].AsInt();
          const std::size_t width = base.components.size() / std::max<std::size_t>(1, base.rows);
          const std::size_t step = (base.rows > 1) ? width : 1;
          if (i < 0 || static_cast<std::size_t>(i) * step + step > base.components.size()) {
            throw FxError(expr.where, "index out of range");
          }
          Value value;
          value.components.assign(base.components.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(i) * step),
                                  base.components.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(i) * step + step));
          return value;
        }
        case ExprKind::Call:
          return EvalIntrinsic(expr, context);
        case ExprKind::Compile:
        case ExprKind::Null:
        case ExprKind::Asm:
          break;
        }
        throw FxError(expr.where, "this is not a constant value");
      }

      // ---- annotations and parameters -------------------------------------------

      AnnotationInfo BuildAnnotation(const Annotation& annotation)
      {
        AnnotationInfo info;
        std::uint32_t elements = ArrayElements(annotation.dims, annotation.where);
        const bool unsized = !annotation.dims.empty() && !annotation.dims[0].size;
        const Value value = Eval(*annotation.value, {});
        if (annotation.type.shape == TypeSpec::Shape::Object && annotation.type.object == ObjectType::String) {
          if (!value.isString) {
            throw FxError(annotation.where, "string annotation '" + annotation.name + "' needs a string");
          }
          info.string = value.string;
        } else if (annotation.type.IsNumeric()) {
          info.value = ToWords(annotation.type, elements, unsized, value, annotation.where);
        } else {
          throw FxError(annotation.where, "unsupported annotation type '" + annotation.type.spelling + "'");
        }
        info.desc = Describe(annotation.type, elements, annotation.name, {}, annotation.where);
        info.desc.flags = kParameterAnnotation;
        return info;
      }

      std::vector<AnnotationInfo> BuildAnnotations(const std::vector<Annotation>& annotations)
      {
        std::vector<AnnotationInfo> infos;
        for (const Annotation& annotation : annotations) {
          infos.push_back(BuildAnnotation(annotation));
        }
        return infos;
      }

      std::uint32_t StateWord(const StateInfo& state, const Expr& expr, bool& dynamic)
      {
        EvalContext context;
        context.state = &state;
        context.dynamic = &dynamic;
        const Value value = Eval(expr, context);
        if (value.components.empty()) {
          throw FxError(expr.where, std::string("state '") + state.name + "' needs a numeric value");
        }
        const Component& first = value.components[0];
        switch (state.kind) {
        case StateValueKind::Float:
          return FloatBits(RoundToFloat(first.AsDouble()));
        case StateValueKind::Color:
          if (value.components.size() >= 3) {
            // D3DXCOLOR's DWORD conversion (d3dx9math.inl): clamp, scale, round.
            auto channel = [](const Component& c) -> std::uint32_t {
              const float f = RoundToFloat(c.AsDouble());
              return f >= 1.0f ? 0xFFU : f <= 0.0f ? 0x00U : static_cast<std::uint32_t>(f * 255.0f + 0.5f);
            };
            const std::uint32_t a = (value.components.size() >= 4) ? channel(value.components[3]) : 0xFFU;
            return (a << 24) | (channel(value.components[0]) << 16) | (channel(value.components[1]) << 8) |
                   channel(value.components[2]);
          }
          return static_cast<std::uint32_t>(first.AsInt());
        case StateValueKind::Dword:
        case StateValueKind::Bool:
        case StateValueKind::Enum:
          return static_cast<std::uint32_t>(first.AsInt());
        }
        return 0;
      }

      std::string TextureReference(const Expr& expr, bool& isNull)
      {
        isNull = false;
        if (expr.kind == ExprKind::Null) {
          isNull = true;
          return {};
        }
        if (expr.kind != ExprKind::Identifier && expr.kind != ExprKind::AngleRef) {
          throw FxError(expr.where, "Texture needs a texture parameter");
        }
        const auto global = mGlobals.find(expr.text);
        if (global == mGlobals.end() || global->second->type.shape != TypeSpec::Shape::Object) {
          throw FxError(expr.where, "'" + expr.text + "' is not a texture parameter");
        }
        return expr.text;
      }

      ParameterInfo BuildParameter(const VariableDecl& global)
      {
        ParameterInfo info;
        std::uint32_t elements = ArrayElements(global.dims, global.where);
        const bool unsized = !global.dims.empty() && !global.dims[0].size;
        if (global.type.IsNumeric() || global.type.shape == TypeSpec::Shape::Struct) {
          if (global.init) {
            info.value = ToWords(global.type, elements, unsized, Eval(*global.init, {}), global.init->where);
          } else {
            if (unsized) {
              throw FxError(global.where, "unsized array '" + global.name + "' needs an initializer");
            }
            std::vector<ScalarType> layout;
            Layout(global.type, layout, global.where);
            info.value.assign(layout.size() * std::max<std::uint32_t>(1, elements), 0U);
          }
        } else if (global.type.shape == TypeSpec::Shape::Object) {
          if (global.type.object == ObjectType::String && global.init) {
            const Value value = Eval(*global.init, {});
            if (!value.isString) {
              throw FxError(global.where, "string parameter '" + global.name + "' needs a string");
            }
            info.string = value.string;
          }
          if (global.hasSamplerState) {
            info.hasSamplerState = true;
            std::vector<const StateInfo*> keys;
            for (const StateAssignment& assignment : global.samplerStates) {
              const StateInfo* state = FindSamplerState(assignment.name);
              if (state == nullptr) {
                throw FxError(assignment.where, "'" + assignment.name + "' is not a sampler state");
              }
              if (state->op == StateOp::Texture) {
                info.samplerTexture = TextureReference(*assignment.value, info.samplerTextureNull);
                continue;
              }
              SamplerStateValue value;
              value.state = state->id;
              value.value = StateWord(*state, *assignment.value, value.dynamic);
              // A state assigned again replaces the earlier assignment, which
              // drops out of the order (see BuildPass).
              for (std::size_t i = 0; i < keys.size(); ++i) {
                if (keys[i] == state) {
                  keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(i));
                  info.samplerStates.erase(info.samplerStates.begin() + static_cast<std::ptrdiff_t>(i));
                  break;
                }
              }
              keys.push_back(state);
              info.samplerStates.push_back(value);
            }
          }
        }
        info.desc = Describe(global.type, elements, global.name, global.semantic, global.where);
        if (global.isShared) {
          info.desc.flags |= kParameterShared;
        }
        info.annotations = BuildAnnotations(global.annotations);
        if (global.type.shape == TypeSpec::Shape::Struct) {
          for (const StructMember& member : global.type.structDef->members) {
            info.members.push_back(
              Describe(member.type, ArrayElements(member.dims, member.where), member.name, member.semantic, member.where)
            );
          }
        }
        return info;
      }

      // ---- techniques ------------------------------------------------------------

      ShaderEntry BuildShader(const Expr& expr, const bool vertex)
      {
        ShaderEntry shader;
        shader.where = expr.where;
        switch (expr.kind) {
        case ExprKind::Null:
          shader.kind = ShaderEntry::Kind::Null;
          return shader;
        case ExprKind::Compile: {
          shader.kind = ShaderEntry::Kind::Compile;
          shader.profile = expr.text;
          shader.versionToken = ProfileVersionToken(expr.text);
          const std::uint32_t wanted = vertex ? 0xFFFE0000U : 0xFFFF0000U;
          if (shader.versionToken == 0 || (shader.versionToken & 0xFFFF0000U) != wanted) {
            throw FxError(expr.where, "'" + expr.text + "' is not a " + (vertex ? "vertex" : "pixel") +
                                        " shader profile");
          }
          shader.entry = expr.entry;
          if (mFunctions.count(expr.entry) == 0) {
            throw FxError(expr.where, "entry point '" + expr.entry + "' not found");
          }
          for (const ExprPtr& argument : expr.operands) {
            shader.arguments.push_back(argument->spelling);
          }
          return shader;
        }
        case ExprKind::Asm: {
          shader.kind = ShaderEntry::Kind::Asm;
          // The first token of an asm block is its version: vs_1_1, ps.2.0 ...
          std::string version;
          for (const char c : expr.text) {
            if (std::isspace(static_cast<unsigned char>(c)) && !version.empty()) {
              break;
            }
            if (!std::isspace(static_cast<unsigned char>(c))) {
              version += (c == '.') ? '_' : c;
            }
          }
          shader.profile = version;
          shader.versionToken = ProfileVersionToken(version);
          return shader;
        }
        case ExprKind::Identifier:
        case ExprKind::AngleRef:
        case ExprKind::Index: {
          // A shader parameter: `VertexShader = (SharedVS);` or an element
          // of a shader array, `PixelShader = (Shaders[1]);`.
          const Expr& name = (expr.kind == ExprKind::Index) ? *expr.operands[0] : expr;
          const auto global = mGlobals.find(name.text);
          if ((name.kind == ExprKind::Identifier || name.kind == ExprKind::AngleRef) && global != mGlobals.end() &&
              global->second->init && global->second->type.shape == TypeSpec::Shape::Object &&
              (global->second->type.object == ObjectType::VertexShader ||
               global->second->type.object == ObjectType::PixelShader)) {
            const Expr& init = *global->second->init;
            if (expr.kind != ExprKind::Index) {
              return BuildShader(init, vertex);
            }
            const Value index = Eval(*expr.operands[1], {});
            const std::int32_t i = index.components.empty() ? -1 : index.components[0].AsInt();
            if (init.kind == ExprKind::InitList && i >= 0 && static_cast<std::size_t>(i) < init.operands.size()) {
              return BuildShader(*init.operands[static_cast<std::size_t>(i)], vertex);
            }
          }
          break;
        }
        default:
          break;
        }
        throw FxError(expr.where, std::string(vertex ? "VertexShader" : "PixelShader") +
                                    " needs compile, null, asm or a shader parameter");
      }

      PassInfo BuildPass(const PassDecl& pass)
      {
        PassInfo info;
        info.name = pass.name;
        info.named = pass.named;
        info.annotations = BuildAnnotations(pass.annotations);
        // D3DX keeps one assignment per state (the effect state's table entry
        // and index): a later one replaces an earlier one and takes the later
        // position. Measured: `ZEnable = true; CullMode = None; ZEnable = false;`
        // reaches the state manager as CullMode, ZEnable(0); mesh.fx
        // FakeRingsNoDepth assigns ZEnable twice. StencilFail and
        // CCW_StencilFail are distinct entries although both set state 53.
        std::vector<std::pair<const StateInfo*, std::uint32_t>> keys;
        for (const StateAssignment& assignment : pass.states) {
          const StateInfo* state = FindPassState(assignment.name);
          if (state == nullptr) {
            throw FxError(assignment.where, "unknown or unsupported effect state '" + assignment.name + "'");
          }
          std::uint32_t index = 0;
          if (assignment.index) {
            const Value value = Eval(*assignment.index, {});
            if (value.components.size() != 1 || value.components[0].AsInt() < 0) {
              throw FxError(assignment.where, "state index must be a non-negative constant");
            }
            index = static_cast<std::uint32_t>(value.components[0].AsInt());
          }
          if (state->op == StateOp::VertexShader || state->op == StateOp::PixelShader) {
            const bool vertex = state->op == StateOp::VertexShader;
            ShaderEntry& slot = vertex ? info.vertexShader : info.pixelShader;
            slot = BuildShader(*assignment.value, vertex); // the last assignment wins, as above
            continue;
          }
          PassState passState;
          passState.op = state->op;
          passState.index = index;
          passState.state = state->id;
          if (state->op == StateOp::Texture) {
            bool isNull = false;
            passState.texture = TextureReference(*assignment.value, isNull);
          } else {
            passState.value = StateWord(*state, *assignment.value, passState.dynamic);
          }
          const std::pair<const StateInfo*, std::uint32_t> key(state, index);
          for (std::size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == key) {
              keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(i));
              info.states.erase(info.states.begin() + static_cast<std::ptrdiff_t>(i));
              break;
            }
          }
          keys.push_back(key);
          info.states.push_back(std::move(passState));
        }
        return info;
      }

      TechniqueInfo BuildTechnique(const TechniqueDecl& technique)
      {
        TechniqueInfo info;
        info.name = technique.name;
        info.named = technique.named;
        info.annotations = BuildAnnotations(technique.annotations);
        for (const PassDecl& pass : technique.passes) {
          info.passes.push_back(BuildPass(pass));
        }
        return info;
      }

      const EffectAst& mAst;
      std::vector<Diagnostic>& mDiagnostics;
      std::map<std::string, const VariableDecl*> mGlobals;
      std::set<std::string> mFunctions;
      std::map<std::string, Value> mGlobalValues;
      std::set<std::string> mEvaluating;
    };

    bool HasErrors(const std::vector<Diagnostic>& diagnostics)
    {
      return std::any_of(diagnostics.begin(), diagnostics.end(),
                         [](const Diagnostic& d) { return d.severity == Diagnostic::Severity::Error; });
    }

  } // namespace

  const std::vector<DeviceProfile>& StandardDeviceProfiles()
  {
    static const std::vector<DeviceProfile> profiles = {
      {"all", 0x0300, 0x0300},
      {"sm2", 0x0200, 0x0200},
      {"sm1", 0x0101, 0x0104},
    };
    return profiles;
  }

  bool IsTechniqueValid(const TechniqueInfo& technique, const DeviceProfile& profile)
  {
    for (const PassInfo& pass : technique.passes) {
      for (const ShaderEntry* shader : {&pass.vertexShader, &pass.pixelShader}) {
        if (shader->kind != ShaderEntry::Kind::Compile && shader->kind != ShaderEntry::Kind::Asm) {
          continue;
        }
        const std::uint32_t version = shader->versionToken & 0xFFFFU;
        const std::uint32_t limit =
          (shader == &pass.vertexShader) ? profile.maxVertexShader : profile.maxPixelShader;
        if (shader->versionToken == 0 || version > limit) {
          return false;
        }
      }
    }
    return true;
  }

  std::uint32_t ProfileVersionToken(const std::string& profile)
  {
    static const std::map<std::string, std::uint32_t> table = {
      {"vs_1_1", 0xFFFE0101}, {"vs_2_0", 0xFFFE0200}, {"vs_2_a", 0xFFFE0201}, {"vs_3_0", 0xFFFE0300},
      {"ps_1_1", 0xFFFF0101}, {"ps_1_2", 0xFFFF0102}, {"ps_1_3", 0xFFFF0103}, {"ps_1_4", 0xFFFF0104},
      {"ps_2_0", 0xFFFF0200}, {"ps_2_a", 0xFFFF0201}, {"ps_2_b", 0xFFFF0201}, {"ps_3_0", 0xFFFF0300},
    };
    const auto it = table.find(profile);
    return (it == table.end()) ? 0U : it->second;
  }

  std::string VersionTokenName(const std::uint32_t token)
  {
    const char* kind = ((token >> 16) == 0xFFFEU) ? "vs" : ((token >> 16) == 0xFFFFU) ? "ps" : "??";
    const std::uint32_t major = (token >> 8) & 0xFFU;
    const std::uint32_t minor = token & 0xFFU;
    std::string name = std::string(kind) + "_" + std::to_string(major) + "_";
    name += (major == 2 && minor == 1) ? "x" : std::to_string(minor);
    return name;
  }

  std::string PreprocessEffect(const EffectInput& input, std::vector<Diagnostic>& diagnostics, SourceBuffer& source)
  {
    for (const auto& [name, text] : input.parts) {
      source.Append(name, text);
    }
    const std::vector<Token> lexed = LexText(source.Text(), diagnostics);
    return FormatPreprocessed(Preprocess(lexed, input.macros, diagnostics));
  }

  FrontEndResult BuildEffectMetadata(const EffectInput& input)
  {
    FrontEndResult result;
    for (const auto& [name, text] : input.parts) {
      result.source.Append(name, text);
    }
    const std::vector<Token> lexed = LexText(result.source.Text(), result.diagnostics);
    const std::vector<Token> tokens = Preprocess(lexed, input.macros, result.diagnostics);
    if (HasErrors(result.diagnostics)) {
      return result;
    }
    const EffectAst ast = ParseEffect(tokens, result.diagnostics);
    if (HasErrors(result.diagnostics)) {
      return result;
    }
    try {
      result.metadata = Builder(ast, result.diagnostics).Run();
    } catch (const FxError& error) {
      result.diagnostics.push_back({Diagnostic::Severity::Error, error.Where(), error.what()});
      return result;
    }
    result.ok = !HasErrors(result.diagnostics);
    return result;
  }

} // namespace gpg::gal::fx
