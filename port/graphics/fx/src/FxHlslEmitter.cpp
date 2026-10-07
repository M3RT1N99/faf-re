#include "gpg/gal/fx/FxHlslEmitter.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

#include "gpg/gal/fx/FxEffectParser.h"
#include "gpg/gal/fx/FxLexer.h"
#include "gpg/gal/fx/FxPreprocessor.h"

namespace gpg::gal::fx {

  namespace {

    constexpr std::size_t kNone = static_cast<std::size_t>(-1);

    std::string Upper(std::string text)
    {
      std::transform(text.begin(), text.end(), text.begin(),
                     [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
      return text;
    }

    std::string Lower(std::string text)
    {
      std::transform(text.begin(), text.end(), text.begin(),
                     [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return text;
    }

    bool IsWordChar(const char c)
    {
      return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    // Shader Model 5 keywords and reserved words that D3D9 HLSL lets an effect use as a name.
    // Renamed with a prefix everywhere they appear (declarations and uses alike).
    bool IsSm5Reserved(const std::string& word)
    {
      static const std::set<std::string> reserved = {
        "line", "lineadj", "point", "triangle", "triangleadj", "sample", "precise", "linear", "centroid",
        "nointerpolation", "noperspective", "groupshared", "snorm", "unorm", "globallycoherent", "export",
        "class", "interface", "namespace", "cbuffer", "tbuffer", "packoffset", "Buffer", "SamplerState",
        "SamplerComparisonState", "Texture2D", "Texture3D", "TextureCube", "Texture1D", "RWTexture2D",
        "StructuredBuffer", "ByteAddressBuffer", "AppendStructuredBuffer", "ConsumeStructuredBuffer",
        "InputPatch", "OutputPatch", "PointStream", "LineStream", "TriangleStream", "numthreads", "this",
        "unsigned", "switch", "case", "default", "template", "typename", "operator", "mutable", "new",
        "delete", "friend", "private", "protected", "public", "virtual", "explicit", "sizeof", "union",
        "using", "goto", "enum", "short", "long", "char", "signed", "auto", "catch", "throw", "try",
        "const_cast", "dynamic_cast", "reinterpret_cast", "static_cast", "min16float", "min10float",
        "min16int", "min12int", "min16uint", "pass", "technique", "technique10", "technique11", "compile",
        "stateblock", "stateblock_state", "sampler_state", "fxgroup"};
      return reserved.count(word) != 0;
    }

    std::string Rename(const std::string& word)
    {
      return IsSm5Reserved(word) ? "FxGenName_" + word : word;
    }

    // ---- types ---------------------------------------------------------------------------

    struct TypeInfo
    {
      enum class Kind : std::uint8_t
      {
        Unknown,
        Void,
        Numeric,
        Struct,
        Sampler,
        Texture,
        String
      };

      Kind kind = Kind::Unknown;
      ScalarType scalar = ScalarType::Float;
      std::uint32_t rows = 1;
      std::uint32_t columns = 1;
      bool matrix = false;
      bool vector = false; // floatN (N may be 1)
      const StructDef* structDef = nullptr;
      bool samplerTyped = false; // sampler1D/2D/3D/CUBE (dimension known from the type)
      TextureDim samplerDim = TextureDim::Tex2D;
      bool sampler1D = false;
    };

    std::optional<ScalarType> ScalarFromWord(const std::string& name)
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

    std::optional<TypeInfo> NumericFromWord(const std::string& word)
    {
      std::size_t end = 0;
      while (end < word.size() && std::isalpha(static_cast<unsigned char>(word[end])) != 0) {
        ++end;
      }
      const std::optional<ScalarType> scalar = ScalarFromWord(word.substr(0, end));
      if (!scalar) {
        return std::nullopt;
      }
      TypeInfo type;
      type.scalar = *scalar;
      const std::string rest = word.substr(end);
      if (rest.empty()) {
        type.kind = (*scalar == ScalarType::Void) ? TypeInfo::Kind::Void : TypeInfo::Kind::Numeric;
        return type;
      }
      if (*scalar == ScalarType::Void) {
        return std::nullopt;
      }
      auto digit = [](const char c) { return c >= '1' && c <= '4'; };
      if (rest.size() == 1 && digit(rest[0])) {
        type.kind = TypeInfo::Kind::Numeric;
        type.vector = true;
        type.columns = static_cast<std::uint32_t>(rest[0] - '0');
        return type;
      }
      if (rest.size() == 3 && digit(rest[0]) && rest[1] == 'x' && digit(rest[2])) {
        type.kind = TypeInfo::Kind::Numeric;
        type.matrix = true;
        type.rows = static_cast<std::uint32_t>(rest[0] - '0');
        type.columns = static_cast<std::uint32_t>(rest[2] - '0');
        return type;
      }
      return std::nullopt;
    }

    TypeInfo FromTypeSpec(const TypeSpec& spec)
    {
      TypeInfo type;
      switch (spec.shape) {
      case TypeSpec::Shape::Void:
        type.kind = TypeInfo::Kind::Void;
        break;
      case TypeSpec::Shape::Scalar:
      case TypeSpec::Shape::Vector:
      case TypeSpec::Shape::Matrix:
        type.kind = TypeInfo::Kind::Numeric;
        type.scalar = spec.scalar;
        type.rows = spec.rows;
        type.columns = spec.columns;
        type.vector = spec.shape == TypeSpec::Shape::Vector;
        type.matrix = spec.shape == TypeSpec::Shape::Matrix;
        break;
      case TypeSpec::Shape::Struct:
        type.kind = TypeInfo::Kind::Struct;
        type.structDef = spec.structDef;
        break;
      case TypeSpec::Shape::Object:
        switch (spec.object) {
        case ObjectType::Sampler:
          type.kind = TypeInfo::Kind::Sampler;
          break;
        case ObjectType::Sampler1D:
          type.kind = TypeInfo::Kind::Sampler;
          type.samplerTyped = true;
          type.sampler1D = true;
          break;
        case ObjectType::Sampler2D:
          type.kind = TypeInfo::Kind::Sampler;
          type.samplerTyped = true;
          break;
        case ObjectType::Sampler3D:
          type.kind = TypeInfo::Kind::Sampler;
          type.samplerTyped = true;
          type.samplerDim = TextureDim::Tex3D;
          break;
        case ObjectType::SamplerCube:
          type.kind = TypeInfo::Kind::Sampler;
          type.samplerTyped = true;
          type.samplerDim = TextureDim::Cube;
          break;
        case ObjectType::String:
          type.kind = TypeInfo::Kind::String;
          break;
        default:
          type.kind = TypeInfo::Kind::Texture;
          break;
        }
        break;
      }
      return type;
    }

    const char* ScalarSpelling(const ScalarType scalar)
    {
      switch (scalar) {
      case ScalarType::Bool: return "bool";
      case ScalarType::Int: return "int";
      case ScalarType::UInt: return "uint";
      default: return "float"; // half and double are 32-bit floats in D3D9 shaders
      }
    }

    std::string NumericSpelling(const TypeInfo& type)
    {
      std::string text = ScalarSpelling(type.scalar);
      if (type.matrix) {
        return text + std::to_string(type.rows) + "x" + std::to_string(type.columns);
      }
      if (type.vector) {
        return text + std::to_string(type.columns);
      }
      return text;
    }

    const char* DimSuffix(const TextureDim dim)
    {
      switch (dim) {
      case TextureDim::Tex3D: return "3D";
      case TextureDim::Cube: return "Cube";
      default: return "2D";
      }
    }

    const char* DimTextureType(const TextureDim dim)
    {
      switch (dim) {
      case TextureDim::Tex3D: return "Texture3D";
      case TextureDim::Cube: return "TextureCube";
      default: return "Texture2D";
      }
    }

    struct Semantic
    {
      std::string name; // upper case, without the index
      std::uint32_t index = 0;
      bool present = false;
    };

    Semantic ParseSemantic(const std::string& text)
    {
      Semantic semantic;
      if (text.empty()) {
        return semantic;
      }
      std::size_t end = text.size();
      while (end > 0 && std::isdigit(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
      }
      semantic.name = Upper(text.substr(0, end));
      semantic.index = (end < text.size()) ? static_cast<std::uint32_t>(std::stoul(text.substr(end))) : 0U;
      semantic.present = true;
      return semantic;
    }

    // The ATTRIB slot of a vertex shader input semantic, or -1.
    int VertexSlotOf(const Semantic& semantic)
    {
      if (semantic.name == "POSITION") {
        return semantic.index == 0 ? int{kSlotPosition} : semantic.index == 1 ? int{kSlotPosition1} : -1;
      }
      if (semantic.index != 0 && semantic.name != "TEXCOORD") {
        return -1;
      }
      if (semantic.name == "NORMAL") return int{kSlotNormal};
      if (semantic.name == "TANGENT") return int{kSlotTangent};
      if (semantic.name == "BINORMAL") return int{kSlotBinormal};
      if (semantic.name == "BLENDINDICES") return int{kSlotBlendIndices};
      if (semantic.name == "COLOR") return int{kSlotColor0};
      if (semantic.name == "TEXCOORD") {
        return semantic.index < 8 ? static_cast<int>(kSlotTexcoord0 + semantic.index) : -1;
      }
      return -1;
    }

    // The varying bit of a COLORn/TEXCOORDn semantic, or 0.
    std::uint32_t VaryingBitOf(const Semantic& semantic)
    {
      if (semantic.name == "COLOR" && semantic.index < 2) {
        return 1U << semantic.index;
      }
      if (semantic.name == "TEXCOORD" && semantic.index < 8) {
        return 1U << (2U + semantic.index);
      }
      return 0;
    }

    std::string VaryingMember(const std::uint32_t bit)
    {
      for (std::uint32_t i = 0; i < 10; ++i) {
        if (bit == (1U << i)) {
          return i < 2 ? "FxGenColor" + std::to_string(i) : "FxGenTexcoord" + std::to_string(i - 2);
        }
      }
      return "FxGenInvalid";
    }

    std::string VaryingSemantic(const std::uint32_t index)
    {
      return index < 2 ? "COLOR" + std::to_string(index) : "TEXCOORD" + std::to_string(index - 2);
    }

    const char* Swizzle(const std::uint32_t count)
    {
      switch (count) {
      case 1: return "x";
      case 2: return "xy";
      case 3: return "xyz";
      default: return "xyzw";
      }
    }

    // A float4 expression converted to a numeric type (scalar, vector; not matrix).
    std::string FromFloat4(const std::string& value, const TypeInfo& type)
    {
      const std::uint32_t count = type.vector ? type.columns : 1U;
      std::string components = "(" + value + ")." + Swizzle(count);
      if (type.scalar == ScalarType::Float || type.scalar == ScalarType::Half || type.scalar == ScalarType::Double) {
        return components;
      }
      return "(" + NumericSpelling(type) + ")(" + components + ")";
    }

    // A numeric value padded to a float4 (missing components 0).
    std::string ToFloat4(const std::string& value, const TypeInfo& type)
    {
      const std::uint32_t count = (type.kind == TypeInfo::Kind::Numeric && !type.matrix && type.vector) ? type.columns : 1U;
      switch (count) {
      case 1: return "float4((float)(" + value + "), 0, 0, 0)";
      case 2: return "float4((float2)(" + value + "), 0, 0)";
      case 3: return "float4((float3)(" + value + "), 0)";
      default: return "(float4)(" + value + ")";
      }
    }

    // The element count of a one-dimensional array declared with a literal size, 0 if not an array.
    std::uint32_t ArraySizeOf(const std::vector<ArrayDim>& dims)
    {
      if (dims.empty() || !dims[0].size || dims[0].size->kind != ExprKind::Number) {
        return 0;
      }
      return static_cast<std::uint32_t>(std::stoul(dims[0].size->text, nullptr, 0));
    }

    // ---- top-level items -----------------------------------------------------------------

    struct ParamInfo
    {
      bool in = true;
      bool out = false;
      bool uniform = false;
      TypeInfo type;
      std::string typeText; // the type as written (tokens joined), for locals
      std::string name;
      std::uint32_t arraySize = 0;
      std::string semantic;
      std::size_t begin = 0; // tokens of the whole parameter
      std::size_t end = 0;
    };

    struct FunctionInfo
    {
      std::string name;
      std::size_t begin = 0; // first token of the declaration
      std::size_t nameIndex = 0;
      std::size_t paramsOpen = 0;
      std::size_t paramsClose = 0;
      std::size_t bodyOpen = kNone; // kNone: a prototype
      std::size_t end = 0; // one past the last token
      TypeInfo returnType;
      std::string returnTypeText;
      std::string semantic;
      std::vector<ParamInfo> params;
      std::vector<std::set<TextureDim>> samplerDims; // per parameter
    };

    struct Item
    {
      enum class Kind : std::uint8_t
      {
        Struct,
        Typedef,
        Function,
        Variable,
        Technique
      };

      Kind kind = Kind::Variable;
      std::size_t begin = 0;
      std::size_t end = 0;
      std::size_t function = kNone; // Kind::Function: index into functions
    };

    enum class GlobalKind : std::uint8_t
    {
      Parameter,
      Static,
      Sampler,
      Texture,
      String
    };

    struct GlobalInfo
    {
      GlobalKind kind = GlobalKind::Parameter;
      const VariableDecl* decl = nullptr;
      std::size_t item = kNone; // the Variable item that declares it
      int parameter = -1; // metadata index (not for statics)
    };

    enum class TexMode : std::uint8_t
    {
      Plain,
      Proj,
      Lod,
      Bias,
      Grad
    };

    struct TexIntrinsic
    {
      TextureDim dim = TextureDim::Tex2D;
      bool oneD = false;
      TexMode mode = TexMode::Plain;
    };

    std::optional<TexIntrinsic> FindTexIntrinsic(const std::string& name)
    {
      static const std::map<std::string, TexIntrinsic> table = {
        {"tex1D", {TextureDim::Tex2D, true, TexMode::Plain}},
        {"tex1Dproj", {TextureDim::Tex2D, true, TexMode::Proj}},
        {"tex1Dlod", {TextureDim::Tex2D, true, TexMode::Lod}},
        {"tex1Dbias", {TextureDim::Tex2D, true, TexMode::Bias}},
        {"tex1Dgrad", {TextureDim::Tex2D, true, TexMode::Grad}},
        {"tex2D", {TextureDim::Tex2D, false, TexMode::Plain}},
        {"tex2Dproj", {TextureDim::Tex2D, false, TexMode::Proj}},
        {"tex2Dlod", {TextureDim::Tex2D, false, TexMode::Lod}},
        {"tex2Dbias", {TextureDim::Tex2D, false, TexMode::Bias}},
        {"tex2Dgrad", {TextureDim::Tex2D, false, TexMode::Grad}},
        {"tex3D", {TextureDim::Tex3D, false, TexMode::Plain}},
        {"tex3Dproj", {TextureDim::Tex3D, false, TexMode::Proj}},
        {"tex3Dlod", {TextureDim::Tex3D, false, TexMode::Lod}},
        {"tex3Dbias", {TextureDim::Tex3D, false, TexMode::Bias}},
        {"tex3Dgrad", {TextureDim::Tex3D, false, TexMode::Grad}},
        {"texCUBE", {TextureDim::Cube, false, TexMode::Plain}},
        {"texCUBEproj", {TextureDim::Cube, false, TexMode::Proj}},
        {"texCUBElod", {TextureDim::Cube, false, TexMode::Lod}},
        {"texCUBEbias", {TextureDim::Cube, false, TexMode::Bias}},
        {"texCUBEgrad", {TextureDim::Cube, false, TexMode::Grad}},
      };
      const auto it = table.find(name);
      return (it == table.end()) ? std::nullopt : std::optional<TexIntrinsic>(it->second);
    }

    // What one stage's emission collects.
    struct Usage
    {
      std::set<std::size_t> items; // structs, typedefs, statics, functions to emit
      std::set<int> parameters; // metadata indices
      std::map<std::string, std::set<TextureDim>> samplers; // global sampler -> dimensions sampled
      bool pixel = false; // the stage is a pixel shader (implicit derivatives exist)
      bool pointSnap = false; // FxGenPointSnap2D is used
      std::set<std::string> cubeFaces; // global samplers sampled through FxGenSampleCubeD3D9
      std::set<int> writtenCopies; // parameters read through their static copy (ComputeWrittenParameters)
    };

  } // namespace

  // ---- constant layout ---------------------------------------------------------------------

  namespace {

    // A scalar, vector or matrix described by a D3DX desc, or nothing.
    std::optional<ConstantSlot> NumericSlot(const ParameterDesc& desc)
    {
      const bool numericClass = desc.parameterClass == ParameterClass::Scalar || desc.parameterClass == ParameterClass::Vector ||
                                desc.parameterClass == ParameterClass::MatrixRows ||
                                desc.parameterClass == ParameterClass::MatrixColumns;
      const bool numericType = desc.type == ParameterType::Bool || desc.type == ParameterType::Int || desc.type == ParameterType::Float;
      if (!numericClass || !numericType) {
        return std::nullopt;
      }
      ConstantSlot slot;
      slot.type = desc.type;
      slot.elements = desc.elements;
      slot.rows = std::max<std::uint32_t>(1, desc.rows);
      slot.columns = std::max<std::uint32_t>(1, desc.columns);
      slot.matrix = desc.parameterClass == ParameterClass::MatrixRows || desc.parameterClass == ParameterClass::MatrixColumns;
      slot.registerCount = std::max<std::uint32_t>(1, slot.elements) * (slot.matrix ? slot.rows : 1U);
      return slot;
    }

    std::uint32_t ComponentsOf(const ConstantSlot& slot)
    {
      if (slot.structure) {
        std::uint32_t perElement = 0;
        for (const ConstantSlot& member : slot.members) {
          perElement += ComponentsOf(member);
        }
        return std::max<std::uint32_t>(1, slot.elements) * perElement;
      }
      return std::max<std::uint32_t>(1, slot.elements) * slot.rows * slot.columns;
    }

  } // namespace

  bool ConstantComponentWord(const ConstantSlot& slot, std::uint32_t component, std::uint32_t& word, ParameterType& type)
  {
    if (slot.structure) {
      std::uint32_t perElement = 0;
      for (const ConstantSlot& member : slot.members) {
        perElement += ComponentsOf(member);
      }
      if (perElement == 0 || component >= std::max<std::uint32_t>(1, slot.elements) * perElement) {
        return false;
      }
      const std::uint32_t element = component / perElement;
      std::uint32_t rest = component % perElement;
      for (const ConstantSlot& member : slot.members) {
        const std::uint32_t count = ComponentsOf(member);
        if (rest < count) {
          ConstantSlot placed = member;
          placed.registerOffset = slot.registerOffset + element * slot.elementRegisters + member.registerOffset;
          return ConstantComponentWord(placed, rest, word, type);
        }
        rest -= count;
      }
      return false;
    }
    const std::uint32_t perElement = slot.rows * slot.columns;
    if (component >= std::max<std::uint32_t>(1, slot.elements) * perElement) {
      return false;
    }
    const std::uint32_t element = component / perElement;
    const std::uint32_t row = (component % perElement) / slot.columns;
    const std::uint32_t column = component % slot.columns;
    const std::uint32_t reg = slot.registerOffset + element * (slot.matrix ? slot.rows : 1U) + (slot.matrix ? row : 0U);
    const std::uint32_t lane = slot.matrix ? column : row * slot.columns + column;
    if (lane >= 4) {
      return false;
    }
    word = reg * 4U + lane;
    type = slot.type;
    return true;
  }

  ConstantLayout BuildConstantLayout(const EffectMetadata& metadata)
  {
    ConstantLayout layout;
    layout.slotOfParameter.assign(metadata.parameters.size(), -1);
    for (std::size_t index = 0; index < metadata.parameters.size(); ++index) {
      const ParameterInfo& parameter = metadata.parameters[index];
      std::optional<ConstantSlot> slot = NumericSlot(parameter.desc);
      if (!slot && parameter.desc.parameterClass == ParameterClass::Struct && !parameter.members.empty()) {
        // Every member starts a register (the HLSL struct gets padding to match). Members that
        // are arrays, structs or matrices narrower than a register would need padding HLSL and
        // std140 place differently, so such structs stay unsupported.
        ConstantSlot structure;
        structure.structure = true;
        structure.type = ParameterType::Void;
        structure.elements = parameter.desc.elements;
        bool supported = true;
        for (const ParameterDesc& memberDesc : parameter.members) {
          std::optional<ConstantSlot> member = NumericSlot(memberDesc);
          if (!member || member->elements != 0 || (member->matrix && member->columns != 4)) {
            supported = false;
            break;
          }
          member->registerOffset = structure.elementRegisters;
          structure.elementRegisters += member->registerCount;
          structure.members.push_back(*member);
        }
        if (supported) {
          structure.registerCount = std::max<std::uint32_t>(1, structure.elements) * structure.elementRegisters;
          slot = structure;
        }
      }
      if (!slot) {
        continue;
      }
      slot->parameter = index;
      slot->registerOffset = layout.registerCount;
      layout.registerCount += slot->registerCount;
      layout.slotOfParameter[index] = static_cast<int>(layout.slots.size());
      layout.slots.push_back(*slot);
    }
    layout.defaults.assign(static_cast<std::size_t>(layout.registerCount) * 4U, 0U);
    for (const ConstantSlot& slot : layout.slots) {
      const std::vector<std::uint32_t>& value = metadata.parameters[slot.parameter].value;
      for (std::uint32_t component = 0; component < value.size(); ++component) {
        std::uint32_t word = 0;
        ParameterType type = ParameterType::Float;
        if (ConstantComponentWord(slot, component, word, type)) {
          layout.defaults[word] = value[component];
        }
      }
    }
    return layout;
  }

  // ---- the emitter -------------------------------------------------------------------------

  struct HlslEmitter::Impl
  {
    EffectMetadata metadata;
    ConstantLayout constants;
    SourceBuffer source;
    std::vector<Token> tokens;
    EffectAst ast;
    std::vector<Item> items;
    std::vector<FunctionInfo> functions;
    std::multimap<std::string, std::size_t> functionsByName;
    std::map<std::string, std::size_t> structItems; // struct name -> item
    std::map<std::string, std::pair<std::size_t, TypeInfo>> typedefs; // alias -> (item, type)
    std::map<std::string, const StructDef*> structDefs;
    std::map<std::string, GlobalInfo> globals;
    std::map<std::string, int> parameterIndex;
    std::set<std::string> writtenParameters; // ComputeWrittenParameters

    // ---- token helpers ---------------------------------------------------------------------

    [[nodiscard]] const Token& At(const std::size_t index) const
    {
      return tokens[std::min(index, tokens.size() - 1)];
    }

    [[nodiscard]] bool AtEnd(const std::size_t index) const
    {
      return index >= tokens.size() || tokens[index].kind == TokenKind::End;
    }

    // Index of the token closing the group opened at `open` ("(", "[", "{" or "<").
    [[nodiscard]] std::size_t Match(const std::size_t open) const
    {
      const std::string& opener = tokens[open].text;
      const std::string closer = opener == "(" ? ")" : opener == "[" ? "]" : opener == "{" ? "}" : ">";
      int depth = 0;
      for (std::size_t i = open; !AtEnd(i); ++i) {
        const Token& token = tokens[i];
        if (token.kind != TokenKind::Punct) {
          continue;
        }
        if (token.text == opener) {
          ++depth;
        } else if (token.text == closer) {
          if (--depth == 0) {
            return i;
          }
        }
      }
      throw FxError(tokens[open].where, "unbalanced '" + opener + "'");
    }

    // Splits [begin, end) at top-level commas.
    [[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> SplitArguments(const std::size_t begin, const std::size_t end) const
    {
      std::vector<std::pair<std::size_t, std::size_t>> parts;
      if (begin >= end) {
        return parts;
      }
      int depth = 0;
      std::size_t start = begin;
      for (std::size_t i = begin; i < end; ++i) {
        const Token& token = tokens[i];
        if (token.kind != TokenKind::Punct) {
          continue;
        }
        if (token.text == "(" || token.text == "[" || token.text == "{") {
          ++depth;
        } else if (token.text == ")" || token.text == "]" || token.text == "}") {
          --depth;
        } else if (token.text == "," && depth == 0) {
          parts.emplace_back(start, i);
          start = i + 1;
        }
      }
      parts.emplace_back(start, end);
      return parts;
    }

    [[nodiscard]] std::string Spell(const std::size_t begin, const std::size_t end) const
    {
      std::string text;
      for (std::size_t i = begin; i < end; ++i) {
        AppendToken(text, tokens[i], TokenText(tokens[i]));
      }
      return text;
    }

    [[nodiscard]] static std::string TokenText(const Token& token)
    {
      if (token.kind == TokenKind::Identifier) {
        return Rename(token.text);
      }
      if (token.kind == TokenKind::Number) {
        // D3D9's half suffix (1.5h); SM5 shaders here are all 32-bit floats.
        const std::string& text = token.text;
        const bool hex = text.size() > 1 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
        if (!hex && !text.empty() && (text.back() == 'h' || text.back() == 'H')) {
          return text.substr(0, text.size() - 1);
        }
      }
      return token.text;
    }

    static void AppendToken(std::string& out, const Token& token, const std::string& text)
    {
      if (text.empty()) {
        return;
      }
      if (!out.empty()) {
        if (token.lineStart) {
          out += '\n';
        } else if (token.leadingSpace || (IsWordChar(out.back()) && IsWordChar(text.front()))) {
          out += ' ';
        }
      }
      out += text;
    }

    static void AppendText(std::string& out, const std::string& text)
    {
      if (text.empty()) {
        return;
      }
      if (!out.empty() && IsWordChar(out.back()) && IsWordChar(text.front())) {
        out += ' ';
      }
      out += text;
    }

    // ---- types -----------------------------------------------------------------------------

    [[nodiscard]] TypeInfo ResolveTypeTokens(std::size_t begin, const std::size_t end) const
    {
      static const std::set<std::string> qualifiers = {
        "const", "row_major", "column_major", "uniform", "in", "out", "inout", "static", "linear",
        "centroid", "nointerpolation", "noperspective", "precise", "inline", "shared", "volatile", "extern"};
      while (begin < end && tokens[begin].kind == TokenKind::Identifier && qualifiers.count(tokens[begin].text) != 0) {
        ++begin;
      }
      TypeInfo type;
      if (begin >= end) {
        return type;
      }
      const Token& first = tokens[begin];
      if (first.IsIdent("struct") && begin + 1 < end) {
        const auto it = structDefs.find(tokens[begin + 1].text);
        if (it != structDefs.end()) {
          type.kind = TypeInfo::Kind::Struct;
          type.structDef = it->second;
        }
        return type;
      }
      if ((first.IsIdent("vector") || first.IsIdent("matrix")) && begin + 1 < end && tokens[begin + 1].IsPunct("<")) {
        std::vector<std::string> words;
        for (std::size_t i = begin + 2; i < end && !tokens[i].IsPunct(">"); ++i) {
          if (!tokens[i].IsPunct(",")) {
            words.push_back(tokens[i].text);
          }
        }
        const std::optional<ScalarType> scalar = words.empty() ? std::nullopt : ScalarFromWord(words[0]);
        if (!scalar) {
          return type;
        }
        type.kind = TypeInfo::Kind::Numeric;
        type.scalar = *scalar;
        if (first.text == "vector" && words.size() == 2) {
          type.vector = true;
          type.columns = static_cast<std::uint32_t>(std::stoul(words[1]));
        } else if (first.text == "matrix" && words.size() == 3) {
          type.matrix = true;
          type.rows = static_cast<std::uint32_t>(std::stoul(words[1]));
          type.columns = static_cast<std::uint32_t>(std::stoul(words[2]));
        }
        return type;
      }
      return ResolveTypeName(first.text);
    }

    [[nodiscard]] TypeInfo ResolveTypeName(const std::string& word) const
    {
      if (const std::optional<TypeInfo> numeric = NumericFromWord(word)) {
        return *numeric;
      }
      TypeInfo type;
      const std::string lower = Lower(word);
      if (lower == "sampler" || lower == "sampler_state") {
        type.kind = TypeInfo::Kind::Sampler;
        return type;
      }
      if (lower == "sampler1d" || lower == "sampler2d" || lower == "sampler3d" || lower == "samplercube") {
        type.kind = TypeInfo::Kind::Sampler;
        type.samplerTyped = true;
        type.sampler1D = lower == "sampler1d";
        type.samplerDim = lower == "sampler3d" ? TextureDim::Tex3D : lower == "samplercube" ? TextureDim::Cube : TextureDim::Tex2D;
        return type;
      }
      if (lower.rfind("texture", 0) == 0) {
        type.kind = TypeInfo::Kind::Texture;
        return type;
      }
      if (lower == "string") {
        type.kind = TypeInfo::Kind::String;
        return type;
      }
      const auto structIt = structDefs.find(word);
      if (structIt != structDefs.end()) {
        type.kind = TypeInfo::Kind::Struct;
        type.structDef = structIt->second;
        return type;
      }
      const auto aliasIt = typedefs.find(word);
      if (aliasIt != typedefs.end()) {
        return aliasIt->second.second;
      }
      return type;
    }

    // ---- segmentation ----------------------------------------------------------------------

    void Segment()
    {
      std::size_t i = 0;
      while (!AtEnd(i)) {
        const Token& token = tokens[i];
        if (token.IsPunct(";")) {
          ++i;
          continue;
        }
        Item item;
        item.begin = i;
        if (token.IsIdent("technique") || token.IsIdent("technique10") || token.IsIdent("technique11")) {
          std::size_t j = i + 1;
          while (!AtEnd(j) && !tokens[j].IsPunct("{")) {
            j = tokens[j].IsPunct("<") ? Match(j) + 1 : j + 1;
          }
          if (AtEnd(j)) {
            throw FxError(token.where, "technique without a body");
          }
          item.kind = Item::Kind::Technique;
          item.end = Match(j) + 1;
          items.push_back(item);
          i = item.end;
          continue;
        }
        if (token.IsIdent("typedef")) {
          std::size_t j = i;
          while (!AtEnd(j) && !tokens[j].IsPunct(";")) {
            ++j;
          }
          item.kind = Item::Kind::Typedef;
          item.end = j + 1;
          // typedef <qualifiers> <type...> Alias [, Alias2] ;
          std::size_t nameIndex = j - 1;
          const TypeInfo aliased = ResolveTypeTokens(i + 1, nameIndex);
          typedefs[tokens[nameIndex].text] = {items.size(), aliased};
          items.push_back(item);
          i = item.end;
          continue;
        }
        if (token.IsIdent("struct") && (At(i + 1).IsPunct("{") || At(i + 2).IsPunct("{"))) {
          const std::size_t open = At(i + 1).IsPunct("{") ? i + 1 : i + 2;
          std::size_t j = Match(open) + 1;
          while (!AtEnd(j) && !tokens[j].IsPunct(";")) {
            ++j; // `struct S { ... } variable;` declares a variable too (not in shipped effects)
          }
          item.kind = Item::Kind::Struct;
          item.end = j + 1;
          if (open == i + 2) {
            structItems[tokens[i + 1].text] = items.size();
          }
          items.push_back(item);
          i = item.end;
          continue;
        }
        // A function or a variable declaration.
        bool assigned = false;
        int depth = 0;
        std::size_t j = i;
        bool done = false;
        while (!AtEnd(j) && !done) {
          const Token& t = tokens[j];
          if (t.kind == TokenKind::Punct && depth == 0) {
            if (t.text == "(" && !assigned && j > i && tokens[j - 1].kind == TokenKind::Identifier) {
              const std::size_t close = Match(j);
              std::size_t k = close + 1;
              std::string semantic;
              if (At(k).IsPunct(":") && At(k + 1).kind == TokenKind::Identifier) {
                semantic = At(k + 1).text;
                k += 2;
              }
              if (At(k).IsPunct("{") || At(k).IsPunct(";")) {
                FunctionInfo function;
                function.name = tokens[j - 1].text;
                function.begin = i;
                function.nameIndex = j - 1;
                function.paramsOpen = j;
                function.paramsClose = close;
                function.semantic = semantic;
                if (At(k).IsPunct("{")) {
                  function.bodyOpen = k;
                  function.end = Match(k) + 1;
                } else {
                  function.end = k + 1;
                }
                item.kind = Item::Kind::Function;
                item.end = function.end;
                item.function = functions.size();
                functions.push_back(std::move(function));
                done = true;
                continue;
              }
            }
            if (t.text == "<" && !assigned) {
              j = Match(j) + 1; // annotations
              continue;
            }
            if (t.text == "=") {
              assigned = true;
            } else if (t.text == ";") {
              item.kind = Item::Kind::Variable;
              item.end = j + 1;
              done = true;
              continue;
            }
          }
          if (t.kind == TokenKind::Punct) {
            if (t.text == "(" || t.text == "[" || t.text == "{") {
              ++depth;
            } else if (t.text == ")" || t.text == "]" || t.text == "}") {
              --depth;
            }
          }
          ++j;
        }
        if (!done) {
          throw FxError(token.where, "unterminated declaration");
        }
        items.push_back(item);
        i = item.end;
      }
    }

    void ParseFunctions()
    {
      for (std::size_t index = 0; index < functions.size(); ++index) {
        FunctionInfo& function = functions[index];
        function.returnType = ResolveTypeTokens(function.begin, function.nameIndex);
        function.returnTypeText = Spell(function.begin, function.nameIndex);
        // Drop storage words that SM5 rejects on a return type.
        for (const char* word : {"inline ", "static ", "uniform "}) {
          const std::size_t at = function.returnTypeText.find(word);
          if (at == 0) {
            function.returnTypeText.erase(0, std::string(word).size());
          }
        }
        for (const auto& [begin, end] : SplitArguments(function.paramsOpen + 1, function.paramsClose)) {
          if (begin >= end || (end - begin == 1 && tokens[begin].IsIdent("void"))) {
            continue;
          }
          ParamInfo param;
          param.begin = begin;
          param.end = end;
          std::size_t typeBegin = begin;
          for (;; ++typeBegin) {
            const Token& t = tokens[typeBegin];
            if (t.IsIdent("in")) {
              param.in = true;
            } else if (t.IsIdent("out")) {
              param.in = false;
              param.out = true;
            } else if (t.IsIdent("inout")) {
              param.in = true;
              param.out = true;
            } else if (t.IsIdent("uniform")) {
              param.uniform = true;
            } else if (!(t.IsIdent("const") || t.IsIdent("linear") || t.IsIdent("centroid") || t.IsIdent("nointerpolation"))) {
              break;
            }
          }
          // The name: the last identifier before '[', ':' or '='.
          std::size_t stop = end;
          for (std::size_t k = typeBegin; k < end; ++k) {
            if (tokens[k].IsPunct("[") || tokens[k].IsPunct(":") || tokens[k].IsPunct("=")) {
              stop = k;
              break;
            }
          }
          std::size_t nameIndex = stop - 1;
          param.name = tokens[nameIndex].text;
          param.type = ResolveTypeTokens(typeBegin, nameIndex);
          param.typeText = Spell(typeBegin, nameIndex);
          for (std::size_t k = stop; k < end; ++k) {
            if (tokens[k].IsPunct("[") && k + 1 < end && tokens[k + 1].kind == TokenKind::Number) {
              param.arraySize = static_cast<std::uint32_t>(std::stoul(tokens[k + 1].text, nullptr, 0));
            }
            if (tokens[k].IsPunct(":") && k + 1 < end && tokens[k + 1].kind == TokenKind::Identifier) {
              param.semantic = tokens[k + 1].text;
            }
            if (tokens[k].IsPunct("=")) {
              break;
            }
          }
          function.params.push_back(std::move(param));
        }
        function.samplerDims.assign(function.params.size(), {});
        functionsByName.emplace(function.name, index);
      }
    }

    void IndexStructs()
    {
      for (const auto& structDef : ast.structs) {
        if (!structDef->name.empty()) {
          structDefs[structDef->name] = structDef.get();
        }
      }
    }

    void IndexGlobals()
    {
      for (std::size_t i = 0; i < metadata.parameters.size(); ++i) {
        parameterIndex[metadata.parameters[i].desc.name] = static_cast<int>(i);
      }
      for (const VariableDecl& decl : ast.globals) {
        GlobalInfo info;
        info.decl = &decl;
        const TypeInfo type = FromTypeSpec(decl.type);
        if (decl.isStatic) {
          info.kind = GlobalKind::Static;
        } else if (type.kind == TypeInfo::Kind::Sampler) {
          info.kind = GlobalKind::Sampler;
        } else if (type.kind == TypeInfo::Kind::Texture) {
          info.kind = GlobalKind::Texture;
        } else if (type.kind == TypeInfo::Kind::String) {
          info.kind = GlobalKind::String;
        } else {
          info.kind = GlobalKind::Parameter;
        }
        if (!decl.isStatic) {
          const auto it = parameterIndex.find(decl.name);
          info.parameter = (it == parameterIndex.end()) ? -1 : it->second;
        }
        // The declaring item: the Variable item holding the name token at the declaration's position.
        for (std::size_t itemIndex = 0; itemIndex < items.size() && info.item == kNone; ++itemIndex) {
          const Item& item = items[itemIndex];
          if (item.kind != Item::Kind::Variable) {
            continue;
          }
          for (std::size_t k = item.begin; k < item.end; ++k) {
            if (tokens[k].kind == TokenKind::Identifier && tokens[k].text == decl.name &&
                tokens[k].where.line == decl.where.line && tokens[k].where.column == decl.where.column) {
              info.item = itemIndex;
              break;
            }
          }
        }
        globals[decl.name] = info;
      }
    }

    // ---- function lookup and sampler parameter dimensions -----------------------------------

    [[nodiscard]] std::vector<std::size_t> Overloads(const std::string& name, const std::size_t arity) const
    {
      std::vector<std::size_t> found;
      const auto range = functionsByName.equal_range(name);
      for (auto it = range.first; it != range.second; ++it) {
        const FunctionInfo& function = functions[it->second];
        std::size_t required = 0;
        for (const ParamInfo& param : function.params) {
          bool hasDefault = false;
          for (std::size_t k = param.begin; k < param.end; ++k) {
            hasDefault = hasDefault || tokens[k].IsPunct("=");
          }
          if (!hasDefault) {
            ++required;
          }
        }
        if (arity >= required && arity <= function.params.size()) {
          found.push_back(it->second);
        }
      }
      return found;
    }

    [[nodiscard]] int ParamIndex(const FunctionInfo& function, const std::string& name) const
    {
      for (std::size_t i = 0; i < function.params.size(); ++i) {
        if (function.params[i].name == name) {
          return static_cast<int>(i);
        }
      }
      return -1;
    }

    // The identifier an argument consists of (parentheses allowed), or empty.
    [[nodiscard]] std::string SoleIdentifier(std::size_t begin, std::size_t end) const
    {
      while (begin + 1 < end && tokens[begin].IsPunct("(") && tokens[end - 1].IsPunct(")")) {
        ++begin;
        --end;
      }
      if (end == begin + 1 && tokens[begin].kind == TokenKind::Identifier) {
        return tokens[begin].text;
      }
      return {};
    }

    // Fixed point: the dimensions each sampler parameter is sampled with, directly or through calls.
    void ComputeSamplerDims()
    {
      bool changed = true;
      while (changed) {
        changed = false;
        for (FunctionInfo& function : functions) {
          if (function.bodyOpen == kNone) {
            continue;
          }
          const std::size_t bodyEnd = function.end;
          for (std::size_t i = function.bodyOpen; i < bodyEnd; ++i) {
            if (tokens[i].kind != TokenKind::Identifier || !At(i + 1).IsPunct("(")) {
              continue;
            }
            const std::size_t close = Match(i + 1);
            const auto args = SplitArguments(i + 2, close);
            if (const std::optional<TexIntrinsic> tex = FindTexIntrinsic(tokens[i].text)) {
              if (!args.empty()) {
                const int param = ParamIndex(function, SoleIdentifier(args[0].first, args[0].second));
                if (param >= 0 && function.samplerDims[param].insert(tex->dim).second) {
                  changed = true;
                }
              }
              continue;
            }
            for (const std::size_t callee : Overloads(tokens[i].text, args.size())) {
              for (std::size_t a = 0; a < args.size(); ++a) {
                if (functions[callee].params[a].type.kind != TypeInfo::Kind::Sampler) {
                  continue;
                }
                const int param = ParamIndex(function, SoleIdentifier(args[a].first, args[a].second));
                if (param < 0) {
                  continue;
                }
                for (const TextureDim dim : functions[callee].samplerDims[a]) {
                  if (function.samplerDims[param].insert(dim).second) {
                    changed = true;
                  }
                }
              }
            }
          }
          // A typed sampler parameter always has its type's dimension.
          for (std::size_t p = 0; p < function.params.size(); ++p) {
            const ParamInfo& param = function.params[p];
            if (param.type.kind == TypeInfo::Kind::Sampler && param.type.samplerTyped) {
              if (function.samplerDims[p].insert(param.type.samplerDim).second) {
                changed = true;
              }
            }
          }
        }
      }
    }

    // ---- resources -------------------------------------------------------------------------

    struct SamplerTexture
    {
      std::string resource;
      int textureParameter = -1;
    };

    [[nodiscard]] SamplerTexture TextureOfSampler(const std::string& samplerName, const TextureDim dim) const
    {
      SamplerTexture result;
      const auto global = globals.find(samplerName);
      const int parameter = (global == globals.end()) ? -1 : global->second.parameter;
      std::string texture;
      if (parameter >= 0) {
        const ParameterInfo& info = metadata.parameters[static_cast<std::size_t>(parameter)];
        if (info.hasSamplerState && !info.samplerTextureNull) {
          texture = info.samplerTexture;
        }
      }
      const auto textureIt = parameterIndex.find(texture);
      if (texture.empty() || textureIt == parameterIndex.end()) {
        result.resource = std::string("FxGenNullTexture") + DimSuffix(dim);
        return result;
      }
      result.textureParameter = textureIt->second;
      result.resource = Rename(texture) + (dim == TextureDim::Tex2D ? "" : std::string("_Fx") + DimSuffix(dim));
      return result;
    }

    // ---- function emission -----------------------------------------------------------------

    struct EmitContext
    {
      const FunctionInfo* function = nullptr;
      Usage* usage = nullptr;
      std::vector<std::size_t>* pending = nullptr; // functions still to emit
      std::set<std::string>* locals = nullptr; // names the current function declares (they hide globals)
      // Sampler parameters of the entry function bound to a global sampler by the compile statement
      // (`PrimBatcherPS(PointSampler)`): inside the entry they are that sampler, so its states are known.
      const std::map<std::string, std::string>* aliases = nullptr;
    };

    // The global sampler an identifier stands for in the current function, or "" for a sampler
    // parameter (or something that is no sampler).
    [[nodiscard]] std::string GlobalSamplerOf(const std::string& name, const EmitContext& context) const
    {
      if (context.aliases != nullptr) {
        const auto alias = context.aliases->find(name);
        if (alias != context.aliases->end()) {
          return alias->second;
        }
      }
      if (context.function != nullptr && ParamIndex(*context.function, name) >= 0) {
        return {};
      }
      const auto global = globals.find(name);
      return (global != globals.end() && global->second.kind == GlobalKind::Sampler) ? name : std::string();
    }

    // D3D9 point sampling picks texel floor(u * size) exactly; measured with fxdiff on this
    // machine's GPU, the D3D11 sampler picks the neighbouring texel for coordinates within about
    // 1/512 texel above a texel boundary (0 of 388,598 D3D9 samples differ from the floor rule; 691
    // D3D11 samples do). Lookups through a sampler whose MINFILTER and MAGFILTER are POINT (D3D9's
    // default) therefore sample the centre of the texel D3D9 picks.
    [[nodiscard]] bool IsPointSampler(const std::string& global) const
    {
      const auto it = globals.find(global);
      if (it == globals.end() || it->second.parameter < 0) {
        return false;
      }
      std::uint32_t min = 1;
      std::uint32_t mag = 1;
      for (const SamplerStateValue& state : metadata.parameters[static_cast<std::size_t>(it->second.parameter)].samplerStates) {
        if (state.state == 6) {
          min = state.value;
        } else if (state.state == 5) {
          mag = state.value;
        }
      }
      return min == 1 && mag == 1;
    }

    [[nodiscard]] bool HasMipFilter(const std::string& global) const
    {
      const auto it = globals.find(global);
      if (it == globals.end() || it->second.parameter < 0) {
        return false;
      }
      for (const SamplerStateValue& state : metadata.parameters[static_cast<std::size_t>(it->second.parameter)].samplerStates) {
        if (state.state == 7) {
          return state.value != 0;
        }
      }
      return false; // D3D9's default MIPFILTER is NONE
    }

    [[nodiscard]] bool HasLinearMipFilter(const std::string& global) const
    {
      const auto it = globals.find(global);
      if (it == globals.end() || it->second.parameter < 0) {
        return false;
      }
      for (const SamplerStateValue& state : metadata.parameters[static_cast<std::size_t>(it->second.parameter)].samplerStates) {
        if (state.state == 7) {
          return state.value == 2; // D3DTEXF_LINEAR
        }
      }
      return false;
    }

    [[nodiscard]] bool IsLocal(const std::string& name, const EmitContext& context) const
    {
      return (context.function != nullptr && ParamIndex(*context.function, name) >= 0) ||
             (context.locals != nullptr && context.locals->count(name) != 0);
    }

    void MarkIdentifier(const std::string& name, const EmitContext& context) const
    {
      if (IsLocal(name, context)) {
        return;
      }
      const auto global = globals.find(name);
      if (global != globals.end()) {
        const GlobalInfo& info = global->second;
        if (info.kind == GlobalKind::Parameter) {
          if (info.parameter < 0 || constants.slotOfParameter[static_cast<std::size_t>(info.parameter)] < 0) {
            throw FxError(info.decl->where, "parameter '" + name + "' is a struct this backend cannot lay out, or not numeric");
          }
          context.usage->parameters.insert(info.parameter);
          if (info.decl->type.structDef != nullptr) {
            MarkType(info.decl->type.structDef->name, context); // its (padded) definition comes first
          }
        } else if (info.kind == GlobalKind::Static) {
          if (info.item != kNone && context.usage->items.insert(info.item).second) {
            MarkItemReferences(items[info.item], context);
          }
        } else if (info.kind == GlobalKind::Texture) {
          throw FxError(info.decl->where, "texture '" + name + "' is used directly in shader code");
        }
        return;
      }
      MarkType(name, context);
    }

    void MarkType(const std::string& name, const EmitContext& context) const
    {
      const auto structIt = structItems.find(name);
      if (structIt != structItems.end()) {
        if (context.usage->items.insert(structIt->second).second) {
          MarkItemReferences(items[structIt->second], context);
        }
        return;
      }
      const auto aliasIt = typedefs.find(name);
      if (aliasIt != typedefs.end()) {
        if (context.usage->items.insert(aliasIt->second.first).second) {
          MarkItemReferences(items[aliasIt->second.first], context);
        }
      }
    }

    // Struct, typedef and static declarations: what they name in turn.
    void MarkItemReferences(const Item& item, const EmitContext& context) const
    {
      EmitContext inner = context;
      inner.function = nullptr;
      for (std::size_t k = item.begin; k < item.end; ++k) {
        const Token& token = tokens[k];
        if (token.kind != TokenKind::Identifier || (k > item.begin && tokens[k - 1].IsPunct("."))) {
          continue;
        }
        if (structItems.count(token.text) != 0 || typedefs.count(token.text) != 0) {
          MarkType(token.text, inner);
        } else if (k != item.begin) {
          const auto global = globals.find(token.text);
          if (global != globals.end() && global->second.kind == GlobalKind::Static && global->second.item != kNone &&
              global->second.item != static_cast<std::size_t>(&item - items.data())) {
            MarkIdentifier(token.text, inner);
          }
        }
      }
    }

    // The texture and sampler expressions for a sampler argument sampled with `dim`.
    std::pair<std::string, std::string> ResolveSampler(
      const std::string& name, const TextureDim dim, const EmitContext& context, const SourceLocation where
    ) const
    {
      if (context.aliases != nullptr && context.aliases->count(name) != 0) {
        const std::string& global = context.aliases->at(name);
        context.usage->samplers[global].insert(dim);
        return {TextureOfSampler(global, dim).resource, Rename(global)};
      }
      if (context.function != nullptr) {
        const int param = ParamIndex(*context.function, name);
        if (param >= 0) {
          if (context.function->params[static_cast<std::size_t>(param)].type.kind != TypeInfo::Kind::Sampler) {
            throw FxError(where, "'" + name + "' is not a sampler");
          }
          return {Rename(name) + "_Fx" + DimSuffix(dim), Rename(name)};
        }
      }
      const auto global = globals.find(name);
      if (global == globals.end() || global->second.kind != GlobalKind::Sampler) {
        throw FxError(where, "unsupported sampler expression '" + name + "'");
      }
      context.usage->samplers[name].insert(dim);
      return {TextureOfSampler(name, dim).resource, Rename(name)};
    }

    // The SamplerState of a sampler argument whose texture the callee never samples.
    std::string SamplerStateOnly(const std::string& name, const EmitContext& context, const SourceLocation where) const
    {
      if (context.aliases != nullptr && context.aliases->count(name) != 0) {
        const std::string& global = context.aliases->at(name);
        context.usage->samplers[global];
        return Rename(global);
      }
      if (context.function != nullptr && ParamIndex(*context.function, name) >= 0) {
        return Rename(name);
      }
      const auto global = globals.find(name);
      if (global == globals.end() || global->second.kind != GlobalKind::Sampler) {
        throw FxError(where, "unsupported sampler expression '" + name + "'");
      }
      context.usage->samplers[name];
      return Rename(name);
    }

    std::string Coordinate(const std::string& value, const TexIntrinsic& tex) const
    {
      if (tex.oneD) {
        return "float2((float)(" + value + "), 0.5)";
      }
      return (tex.dim == TextureDim::Tex2D ? "(float2)(" : "(float3)(") + value + ")";
    }

    std::string EmitTex(const TexIntrinsic& tex, const std::vector<std::pair<std::size_t, std::size_t>>& args,
                        const EmitContext& context, const SourceLocation where) const
    {
      if (args.size() < 2) {
        throw FxError(where, "texture lookup needs a sampler and coordinates");
      }
      const std::string sampler = SoleIdentifier(args[0].first, args[0].second);
      if (sampler.empty()) {
        throw FxError(where, "unsupported sampler expression in a texture lookup");
      }
      const auto [texture, state] = ResolveSampler(sampler, tex.dim, context, where);
      const std::string coord = "(" + EmitRange(args[1].first, args[1].second, context) + ")";
      std::string call;
      if (tex.mode == TexMode::Grad || (tex.mode == TexMode::Plain && args.size() == 4)) {
        if (args.size() != 4) {
          throw FxError(where, "texture gradient lookup needs four arguments");
        }
        const std::string dx = "(" + EmitRange(args[2].first, args[2].second, context) + ")";
        const std::string dy = "(" + EmitRange(args[3].first, args[3].second, context) + ")";
        call = texture + ".SampleGrad(" + state + ", " + Coordinate(coord, tex) + ", " + Coordinate(dx, tex) + ", " +
               Coordinate(dy, tex) + ")";
        return call;
      }
      const std::string v4 = "((float4)" + coord + ")";
      const std::string xyz = tex.oneD ? "x" : tex.dim == TextureDim::Tex2D ? "xy" : "xyz";
      const std::string global = GlobalSamplerOf(sampler, context);
      if (tex.dim == TextureDim::Cube && !global.empty() && tex.mode != TexMode::Grad) {
        // D3D9 samples a cube map within one face and clamps at its edges; D3D10+ hardware filters
        // across faces. Measured with fxdiff (primbatcher TSkyBox): 581 of 4035 pixels along the
        // face boundaries differ by up to 0.41 when sampled through the TextureCube. So the face
        // and its coordinates are computed as the D3D cube convention defines them and the face is
        // sampled from a 2D-array view with CLAMP addressing, at the level of detail the hardware
        // computes for the cube lookup.
        context.usage->cubeFaces.insert(global);
        std::string dir;
        std::string lod;
        switch (tex.mode) {
        case TexMode::Proj: dir = "(" + v4 + ".xyz / " + v4 + ".w)"; break;
        case TexMode::Lod:
        case TexMode::Bias: dir = "(" + v4 + ".xyz)"; break;
        default: dir = "((float3)" + coord + ")"; break;
        }
        const std::string faces = TextureOfSampler(global, TextureDim::Cube).resource + "Faces";
        const std::string clamp = Rename(global) + "_FxClamp";
        if (tex.mode == TexMode::Lod) {
          lod = v4 + ".w";
        } else if (!context.usage->pixel) {
          lod = "0.0"; // no implicit derivatives in a vertex shader
        } else {
          lod = texture + ".CalculateLevelOfDetail(" + state + ", " + dir + ")" + (tex.mode == TexMode::Bias ? " + " + v4 + ".w" : "");
        }
        return "FxGenSampleCubeD3D9(" + faces + ", " + clamp + ", " + dir + ", " + lod + ")";
      }
      if (tex.dim == TextureDim::Tex2D && !global.empty() && IsPointSampler(global)) {
        // D3D9's texel choice for point sampling (IsPointSampler): sample the centre of texel
        // floor(uv * size) of the finest level the lookup can use, which stays that texel in the
        // level the sampler picks (a coarser level's texel boundaries are a subset).
        context.usage->pointSnap = true;
        std::string uv;
        std::string level;
        switch (tex.mode) {
        case TexMode::Proj: uv = Coordinate(v4 + "." + xyz + " / " + v4 + ".w", tex); break;
        case TexMode::Lod:
        case TexMode::Bias: uv = Coordinate(v4 + "." + xyz, tex); break;
        default: uv = Coordinate(coord, tex); break;
        }
        const std::string temp = "(" + uv + ")";
        if (tex.mode == TexMode::Lod) {
          level = v4 + ".w";
          return texture + ".SampleLevel(" + state + ", FxGenPointSnap2D(" + texture + ", floor(" + level + "), " + temp + "), " + level + ")";
        }
        if (!HasMipFilter(global) || !context.usage->pixel) {
          // MIPFILTER NONE samples level 0 (the sampler's MaxLOD is 0); a vertex shader has no
          // implicit derivatives (and D3D9 vertex shaders only use tex2Dlod).
          return texture + ".SampleLevel(" + state + ", FxGenPointSnap2D(" + texture + ", 0, " + temp + "), 0)";
        }
        // The level of detail of the unsnapped coordinates (the snapped ones have no useful
        // derivatives), as the sampler computes it; snapping uses its finer level.
        const std::string bias = tex.mode == TexMode::Bias ? " + " + v4 + ".w" : "";
        const std::string lod = "(" + texture + ".CalculateLevelOfDetail(" + state + ", " + temp + ")" + bias + ")";
        if (!HasLinearMipFilter(global) && tex.mode != TexMode::Bias) {
          // MIPFILTER POINT: the sampler picks the level from the gradients of the unsnapped
          // coordinates, as D3D9's texld does from its implicit ones. Snapping at the finest level
          // it can pick keeps the texel (a coarser level's boundaries are a subset). fxdiff: equal to
          // D3D9 except 1 pixel in about 20,000 that takes the next coarser level (an explicit level
          // from CalculateLevelOfDetail gives the same pixels; open).
          return texture + ".SampleGrad(" + state + ", FxGenPointSnap2D(" + texture + ", floor(" + lod + "), " + temp + "), ddx(" + temp +
                 "), ddy(" + temp + "))";
        }
        return texture + ".SampleLevel(" + state + ", FxGenPointSnap2D(" + texture + ", floor(" + lod + "), " + temp + "), " + lod + ")";
      }
      switch (tex.mode) {
      case TexMode::Proj:
        // D3D9 divides the coordinates by their last component (tex2Dproj).
        call = texture + ".Sample(" + state + ", " + Coordinate(v4 + "." + xyz + " / " + v4 + ".w", tex) + ")";
        break;
      case TexMode::Lod:
        call = texture + ".SampleLevel(" + state + ", " + Coordinate(v4 + "." + xyz, tex) + ", " + v4 + ".w)";
        break;
      case TexMode::Bias:
        call = texture + ".SampleBias(" + state + ", " + Coordinate(v4 + "." + xyz, tex) + ", " + v4 + ".w)";
        break;
      default:
        call = texture + ".Sample(" + state + ", " + Coordinate(coord, tex) + ")";
        break;
      }
      return call;
    }

    // Arguments for a call to `callee`: sampler arguments become texture(s) plus SamplerState.
    std::string EmitCallArguments(
      const FunctionInfo& callee, const std::vector<std::pair<std::size_t, std::size_t>>& args, const EmitContext& context,
      const SourceLocation where
    ) const
    {
      std::string out;
      for (std::size_t a = 0; a < args.size(); ++a) {
        if (a != 0) {
          out += ", ";
        }
        if (callee.params[a].type.kind != TypeInfo::Kind::Sampler) {
          out += EmitRange(args[a].first, args[a].second, context);
          continue;
        }
        const std::string sampler = SoleIdentifier(args[a].first, args[a].second);
        if (sampler.empty()) {
          throw FxError(where, "unsupported sampler argument");
        }
        std::string state;
        for (const TextureDim dim : callee.samplerDims[a]) {
          const auto [texture, samplerState] = ResolveSampler(sampler, dim, context, where);
          out += texture + ", ";
          state = samplerState;
        }
        if (state.empty()) {
          state = SamplerStateOnly(sampler, context, where); // never sampled through this parameter
        }
        out += state;
      }
      return out;
    }

    // Effect parameters some function body assigns to (water2.fx:443 `skyreflectionAmount = 1.0;`).
    // The legacy compiler accepts that and works on a copy (a constant register cannot be written);
    // SM5 refuses writes to cbuffer members, so such a parameter is read through a static copy that
    // every entry point initialises from the parameter (docs/port/README.md "shader code writing a
    // uniform").
    void ComputeWrittenParameters()
    {
      for (const FunctionInfo& function : functions) {
        if (function.bodyOpen == kNone) {
          continue;
        }
        for (std::size_t i = function.bodyOpen; i < function.end; ++i) {
          const Token& token = tokens[i];
          if (token.kind != TokenKind::Identifier || tokens[i - 1].IsPunct(".") || ParamIndex(function, token.text) >= 0) {
            continue;
          }
          const auto global = globals.find(token.text);
          if (global != globals.end() && global->second.kind == GlobalKind::Parameter && IsWrite(i)) {
            writtenParameters.insert(token.text);
          }
        }
      }
    }

    [[nodiscard]] bool IsWrite(const std::size_t index) const
    {
      // NAME [.swizzle | [index]]* <assignment>, or ++NAME / --NAME.
      std::size_t k = index + 1;
      while (!AtEnd(k)) {
        if (tokens[k].IsPunct(".") && At(k + 1).kind == TokenKind::Identifier) {
          k += 2;
        } else if (tokens[k].IsPunct("[")) {
          k = Match(k) + 1;
        } else {
          break;
        }
      }
      static const std::set<std::string> assignments = {"=", "+=", "-=", "*=", "/=", "%=", "++", "--"};
      const bool preIncrement = index > 0 && (tokens[index - 1].IsPunct("++") || tokens[index - 1].IsPunct("--"));
      return preIncrement || (tokens[k].kind == TokenKind::Punct && assignments.count(tokens[k].text) != 0);
    }

    void CheckParameterWrite(const std::size_t index, const EmitContext& context) const
    {
      // NAME [.swizzle | [index]]* <assignment>: a write to an effect parameter (water2.fx:443).
      if (writtenParameters.count(tokens[index].text) != 0) {
        return; // read and written through its static copy
      }
      std::size_t k = index + 1;
      while (!AtEnd(k)) {
        if (tokens[k].IsPunct(".") && At(k + 1).kind == TokenKind::Identifier) {
          k += 2;
        } else if (tokens[k].IsPunct("[")) {
          k = Match(k) + 1;
        } else {
          break;
        }
      }
      static const std::set<std::string> assignments = {"=", "+=", "-=", "*=", "/=", "%=", "++", "--"};
      const bool preIncrement = index > 0 && (tokens[index - 1].IsPunct("++") || tokens[index - 1].IsPunct("--"));
      if (preIncrement || (tokens[k].kind == TokenKind::Punct && assignments.count(tokens[k].text) != 0)) {
        static_cast<void>(context);
        throw FxError(tokens[index].where, "shader code writes effect parameter '" + tokens[index].text + "'; not supported yet");
      }
    }

    std::string EmitRange(const std::size_t begin, const std::size_t end, const EmitContext& context) const
    {
      std::string out;
      for (std::size_t i = begin; i < end; ++i) {
        const Token& token = tokens[i];
        if (token.kind == TokenKind::Identifier) {
          const bool member = i > begin && tokens[i - 1].IsPunct(".");
          if (!member && At(i + 1).IsPunct("(")) {
            const std::size_t close = Match(i + 1);
            const auto args = SplitArguments(i + 2, close);
            if (const std::optional<TexIntrinsic> tex = FindTexIntrinsic(token.text)) {
              AppendToken(out, token, EmitTex(*tex, args, context, token.where));
              i = close;
              continue;
            }
            const std::vector<std::size_t> callees = Overloads(token.text, args.size());
            if (!callees.empty()) {
              for (const std::size_t callee : callees) {
                if (context.usage->items.insert(functionItem(callee)).second) {
                  context.pending->push_back(callee);
                }
              }
              const FunctionInfo& callee = functions[callees.front()];
              AppendToken(out, token, Rename(token.text) + "(" + EmitCallArguments(callee, args, context, token.where) + ")");
              i = close;
              continue;
            }
            const std::string d3d9 = D3D9Intrinsic(token.text, args, context);
            if (!d3d9.empty()) {
              AppendToken(out, token, d3d9);
              i = close;
              continue;
            }
          }
          if (!member && context.locals != nullptr && i > begin && IsTypeWord(tokens[i - 1]) &&
              (At(i + 1).IsPunct("=") || At(i + 1).IsPunct(";") || At(i + 1).IsPunct(",") || At(i + 1).IsPunct("[") ||
               At(i + 1).IsPunct(":"))) {
            context.locals->insert(token.text); // a local declaration: later uses are not the global
          }
          if (!member) {
            const auto global = globals.find(token.text);
            const bool isParam = IsLocal(token.text, context);
            if (!isParam && global != globals.end() && global->second.kind == GlobalKind::Parameter) {
              CheckParameterWrite(i, context);
            }
            if (!isParam && global != globals.end() && global->second.kind == GlobalKind::Sampler) {
              throw FxError(token.where, "sampler '" + token.text + "' used outside a texture lookup or call");
            }
            MarkIdentifier(token.text, context);
            if (!isParam && writtenParameters.count(token.text) != 0) {
              context.usage->writtenCopies.insert(global->second.parameter);
              AppendToken(out, token, "FxGenW_" + Rename(token.text));
              continue;
            }
          }
        }
        AppendToken(out, token, TokenText(token));
      }
      return out;
    }

    // Intrinsics whose D3D9 instruction differs from SM5 for some inputs, measured with fxdiff
    // (legacy compiler, this machine's D3D9 driver, against FXC SM5 on D3D11):
    //   sqrt(x), rsqrt(x)        rsq takes |x| (sqrt is rcp(rsq(x))): sqrt(-0.2) is 0.447, SM5 NaN;
    //   log, log2, log10         log takes |x|: log2(-0.25) is -2, SM5 NaN;
    //   pow(x, y)                pow takes |x| unless y is an integer literal, which both compilers
    //                            expand to multiplications (pow(-0.2, 3) is -0.008 on both);
    //   normalize(v)             v * rsq(dot(v, v)) with DX9 multiply (0 * inf = 0): the zero
    //                            vector stays zero, SM5 gives NaN.
    // Returns "" for anything else.
    [[nodiscard]] std::string D3D9Intrinsic(const std::string& name, const std::vector<std::pair<std::size_t, std::size_t>>& args,
                                            const EmitContext& context) const
    {
      if (name == "sqrt" || name == "rsqrt" || name == "log" || name == "log2" || name == "log10") {
        if (args.size() != 1) {
          return {};
        }
        return name + "(abs(" + EmitRange(args[0].first, args[0].second, context) + "))";
      }
      if (name == "pow" && args.size() == 2) {
        const std::string base = EmitRange(args[0].first, args[0].second, context);
        const std::string exponent = EmitRange(args[1].first, args[1].second, context);
        bool integerLiteral = args[1].second == args[1].first + 1 && tokens[args[1].first].kind == TokenKind::Number;
        if (integerLiteral) {
          const double value = std::strtod(tokens[args[1].first].text.c_str(), nullptr);
          integerLiteral = value == static_cast<double>(static_cast<long long>(value));
        }
        return integerLiteral ? "pow(" + base + ", " + exponent + ")" : "pow(abs(" + base + "), " + exponent + ")";
      }
      if (name == "normalize" && args.size() == 1) {
        const std::string v = "(" + EmitRange(args[0].first, args[0].second, context) + ")";
        return "(" + v + " * (dot(" + v + ", " + v + ") == 0.0 ? 0.0 : rsqrt(dot(" + v + ", " + v + "))))";
      }
      return {};
    }

    [[nodiscard]] bool IsTypeWord(const Token& token) const
    {
      if (token.kind != TokenKind::Identifier) {
        return false;
      }
      return NumericFromWord(token.text).has_value() || structDefs.count(token.text) != 0 || typedefs.count(token.text) != 0;
    }

    [[nodiscard]] std::size_t functionItem(const std::size_t function) const
    {
      for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].kind == Item::Kind::Function && items[i].function == function) {
          return i;
        }
      }
      return kNone;
    }

    // The declaration of one function: parameters with samplers split, body rewritten.
    std::string EmitFunction(const FunctionInfo& function, const EmitContext& outer) const
    {
      EmitContext context = outer;
      context.function = &function;
      std::set<std::string> locals;
      context.locals = &locals;
      std::string out = function.returnTypeText;
      MarkTypeWords(function.begin, function.nameIndex, context);
      AppendText(out, Rename(function.name));
      out += "(";
      bool first = true;
      for (std::size_t p = 0; p < function.params.size(); ++p) {
        const ParamInfo& param = function.params[p];
        if (context.aliases != nullptr && context.aliases->count(param.name) != 0) {
          continue; // a sampler argument of the compile statement: the global sampler itself
        }
        if (!first) {
          out += ", ";
        }
        first = false;
        if (param.type.kind == TypeInfo::Kind::Sampler) {
          for (const TextureDim dim : function.samplerDims[p]) {
            out += std::string(DimTextureType(dim)) + " " + Rename(param.name) + "_Fx" + DimSuffix(dim) + ", ";
          }
          out += "SamplerState " + Rename(param.name);
          continue;
        }
        MarkTypeWords(param.begin, param.end, context);
        std::string text;
        for (std::size_t k = param.begin; k < param.end; ++k) {
          if (tokens[k].IsIdent("uniform")) {
            continue;
          }
          AppendToken(text, tokens[k], TokenText(tokens[k]));
        }
        // A default value may name a static.
        out += text;
      }
      out += ")";
      if (!function.semantic.empty()) {
        out += " : " + function.semantic;
      }
      if (function.bodyOpen == kNone) {
        return out + ";\n";
      }
      out += "\n" + EmitRange(function.bodyOpen, function.end, context) + "\n";
      return out;
    }

    void MarkTypeWords(const std::size_t begin, const std::size_t end, const EmitContext& context) const
    {
      for (std::size_t k = begin; k < end; ++k) {
        if (tokens[k].kind == TokenKind::Identifier) {
          MarkType(tokens[k].text, context);
        }
      }
    }

    // ---- the stage ---------------------------------------------------------------------------

    struct StageText
    {
      std::string declarations; // structs, typedefs, statics in source order
      std::string functions; // in source order
    };

    // Emits everything the entry function reaches; returns the entry's index.
    std::size_t EmitReachable(const ShaderEntry& entry, Usage& usage, std::map<std::size_t, std::string>& functionText,
                              std::map<std::string, std::string>& aliases) const
    {
      const auto range = functionsByName.equal_range(entry.entry);
      std::size_t chosen = kNone;
      for (auto it = range.first; it != range.second; ++it) {
        const FunctionInfo& function = functions[it->second];
        if (function.bodyOpen == kNone) {
          continue;
        }
        std::size_t uniforms = 0;
        for (const ParamInfo& param : function.params) {
          uniforms += param.uniform ? 1U : 0U;
        }
        if (chosen == kNone || uniforms == entry.arguments.size()) {
          chosen = it->second;
        }
      }
      if (chosen == kNone) {
        throw FxError(entry.where, "entry point '" + entry.entry + "' has no definition");
      }
      // Uniform sampler parameters bound to a global sampler become that sampler inside the entry.
      aliases.clear();
      std::size_t argument = 0;
      for (const ParamInfo& param : functions[chosen].params) {
        if (!IsBoundUniform(param)) {
          continue;
        }
        if (argument < entry.arguments.size() && param.type.kind == TypeInfo::Kind::Sampler) {
          const std::string& value = entry.arguments[argument];
          const auto global = globals.find(value);
          if (global != globals.end() && global->second.kind == GlobalKind::Sampler) {
            aliases[param.name] = value;
          }
        }
        ++argument;
      }
      std::vector<std::size_t> pending{chosen};
      usage.items.insert(functionItem(chosen));
      while (!pending.empty()) {
        const std::size_t index = pending.back();
        pending.pop_back();
        EmitContext context;
        context.usage = &usage;
        context.pending = &pending;
        if (index == chosen) {
          context.aliases = &aliases;
        }
        functionText[index] = EmitFunction(functions[index], context);
      }
      return chosen;
    }

    // An entry parameter the compile statement's arguments bind (uniform, or with no semantic).
    [[nodiscard]] static bool IsBoundUniform(const ParamInfo& param)
    {
      return param.uniform || (param.semantic.empty() && param.type.kind != TypeInfo::Kind::Struct && !param.out);
    }

    std::string EmitDeclarations(const Usage& usage, const std::map<std::size_t, std::string>& functionText,
                                 EmittedStage& out, const std::string& header) const
    {
      std::ostringstream text;
      text << header;
      // Structs and typedefs. A struct an effect parameter has is padded so every member starts a
      // register (ConstantLayout); locals of that type get the padding too, which they never see.
      std::set<std::string> padded;
      for (const int parameter : usage.parameters) {
        const auto global = globals.find(metadata.parameters[static_cast<std::size_t>(parameter)].desc.name);
        if (global != globals.end() && global->second.decl->type.structDef != nullptr) {
          padded.insert(global->second.decl->type.structDef->name);
        }
      }
      for (const std::size_t index : usage.items) {
        const Item& item = items[index];
        if (item.kind == Item::Kind::Struct || item.kind == Item::Kind::Typedef) {
          std::string name;
          for (const auto& [structName, structItem] : structItems) {
            if (structItem == index) {
              name = structName;
            }
          }
          if (padded.count(name) != 0) {
            text << PaddedStruct(*structDefs.at(name));
          } else {
            text << Spell(item.begin, item.end) << "\n";
          }
        }
      }
      // Draw constants (every stage).
      text << "cbuffer FxGenDraw\n{\n"
              "    float4 FxGenViewport : packoffset(c0);\n"
              "    float4 FxGenViewportDepth : packoffset(c1);\n"
              "    float4 FxGenPositionOffset : packoffset(c2);\n"
              "    float4 FxGenAlphaTest : packoffset(c3);\n"
              "};\n";
      // Effect parameters.
      if (!usage.parameters.empty()) {
        out.usesParams = true;
        text << "cbuffer FxGenParams\n{\n";
        for (const int parameter : usage.parameters) {
          const ConstantSlot& slot = constants.slots[static_cast<std::size_t>(constants.slotOfParameter[static_cast<std::size_t>(parameter)])];
          const ParameterDesc& desc = metadata.parameters[static_cast<std::size_t>(parameter)].desc;
          text << "    " << SlotTypeOf(slot) << " " << Rename(desc.name) << SlotArray(slot) << " : packoffset(c" << slot.registerOffset << ");\n";
        }
        text << "};\n";
      }
      for (const int parameter : usage.writtenCopies) {
        const ConstantSlot& slot = constants.slots[static_cast<std::size_t>(constants.slotOfParameter[static_cast<std::size_t>(parameter)])];
        text << "static " << SlotTypeOf(slot) << " FxGenW_" << Rename(metadata.parameters[static_cast<std::size_t>(parameter)].desc.name)
             << SlotArray(slot) << ";\n";
      }
      // Textures and samplers.
      std::set<std::string> declared;
      for (const auto& [sampler, dims] : usage.samplers) {
        for (const TextureDim dim : dims) {
          const SamplerTexture texture = TextureOfSampler(sampler, dim);
          if (declared.insert(texture.resource).second) {
            text << DimTextureType(dim) << " " << texture.resource << ";\n";
            TextureResource resource;
            resource.name = texture.resource;
            resource.textureParameter = texture.textureParameter;
            resource.dim = dim;
            out.textures.push_back(resource);
          }
        }
        text << "SamplerState " << Rename(sampler) << ";\n";
        SamplerResource resource;
        resource.name = Rename(sampler);
        resource.samplerParameter = globals.at(sampler).parameter;
        out.samplers.push_back(resource);
        if (usage.cubeFaces.count(sampler) != 0) {
          const SamplerTexture texture = TextureOfSampler(sampler, TextureDim::Cube);
          if (declared.insert(texture.resource + "Faces").second) {
            text << "Texture2DArray " << texture.resource << "Faces;\n";
            TextureResource faces;
            faces.name = texture.resource + "Faces";
            faces.textureParameter = texture.textureParameter;
            faces.dim = TextureDim::Cube;
            faces.cubeFaces = true;
            out.textures.push_back(faces);
          }
          text << "SamplerState " << Rename(sampler) << "_FxClamp;\n";
          SamplerResource clamp = resource;
          clamp.name = Rename(sampler) + "_FxClamp";
          clamp.clampAddress = true;
          out.samplers.push_back(clamp);
        }
      }
      if (!usage.cubeFaces.empty()) {
        // A cube lookup as D3D9 does it: the face of the major axis (z before y before x when they
        // tie), its coordinates (D3D's cube map convention: +X face s = -z, t = -y; -X s = z;
        // +Y s = x, t = z; -Y t = -z; +Z s = x, t = -y; -Z s = -x), sampled in that face only.
        text << "float4 FxGenSampleCubeD3D9(Texture2DArray fxFaces, SamplerState fxClamp, float3 fxDir, float fxLod)\n{\n"
                "    float3 fxA = abs(fxDir);\n"
                "    float fxFace;\n"
                "    float2 fxSt;\n"
                "    float fxMajor;\n"
                "    if (fxA.z >= fxA.x && fxA.z >= fxA.y) {\n"
                "        fxMajor = fxA.z;\n"
                "        fxFace = fxDir.z >= 0.0 ? 4.0 : 5.0;\n"
                "        fxSt = float2(fxDir.z >= 0.0 ? fxDir.x : -fxDir.x, -fxDir.y);\n"
                "    } else if (fxA.y >= fxA.x) {\n"
                "        fxMajor = fxA.y;\n"
                "        fxFace = fxDir.y >= 0.0 ? 2.0 : 3.0;\n"
                "        fxSt = float2(fxDir.x, fxDir.y >= 0.0 ? fxDir.z : -fxDir.z);\n"
                "    } else {\n"
                "        fxMajor = fxA.x;\n"
                "        fxFace = fxDir.x >= 0.0 ? 0.0 : 1.0;\n"
                "        fxSt = float2(fxDir.x >= 0.0 ? -fxDir.z : fxDir.z, -fxDir.y);\n"
                "    }\n"
                "    float2 fxUv = mad(fxSt, rcp(2.0 * fxMajor), 0.5);\n"
                "    return fxFaces.SampleLevel(fxClamp, float3(fxUv, fxFace), fxLod);\n"
                "}\n";
      }
      if (usage.pointSnap) {
        // The centre of texel floor(uv * size) at `level` (clamped to the texture's levels): D3D9's
        // point-sampling texel choice (IsPointSampler).
        text << "float2 FxGenPointSnap2D(Texture2D fxTexture, float fxLevel, float2 fxUv)\n{\n"
                "    uint fxWidth, fxHeight, fxLevels;\n"
                "    fxTexture.GetDimensions(0, fxWidth, fxHeight, fxLevels);\n"
                "    uint fxIndex = (uint)clamp(fxLevel, 0.0, (float)(fxLevels - 1));\n"
                "    fxTexture.GetDimensions(fxIndex, fxWidth, fxHeight, fxLevels);\n"
                "    float2 fxSize = float2(fxWidth, fxHeight);\n"
                "    return (floor(fxUv * fxSize) + 0.5) / fxSize;\n"
                "}\n";
      }
      // Statics, then functions, each in source order.
      for (const std::size_t index : usage.items) {
        const Item& item = items[index];
        if (item.kind == Item::Kind::Variable) {
          text << Spell(item.begin, item.end) << "\n";
        }
      }
      for (const std::size_t index : usage.items) {
        const Item& item = items[index];
        if (item.kind == Item::Kind::Function) {
          text << functionText.at(item.function);
        }
      }
      return text.str();
    }

    // The HLSL type of a parameter in FxGenParams (matrices row_major: the engine's matrix is the one
    // the shader multiplies with, as ID3DXEffect::SetMatrix arranges it).
    static std::string SlotType(const ConstantSlot& slot)
    {
      const char* base = slot.type == ParameterType::Bool ? "bool" : slot.type == ParameterType::Int ? "int" : "float";
      if (slot.matrix) {
        return std::string("row_major ") + base + std::to_string(slot.rows) + "x" + std::to_string(slot.columns);
      }
      return slot.columns > 1 ? base + std::to_string(slot.columns) : std::string(base);
    }

    // SlotType, with a struct parameter's struct name.
    [[nodiscard]] std::string SlotTypeOf(const ConstantSlot& slot) const
    {
      if (slot.structure) {
        const auto global = globals.find(metadata.parameters[slot.parameter].desc.name);
        if (global != globals.end() && global->second.decl->type.structDef != nullptr) {
          return global->second.decl->type.structDef->name;
        }
      }
      return SlotType(slot);
    }

    // A struct whose every member starts a float4 register, matching ConstantLayout: scalar float
    // padding after each scalar or vector member (scalars, so HLSL packing and std140, which
    // glslang uses for Vulkan, place them alike); four-column matrices fill their registers.
    static std::string PaddedStruct(const StructDef& definition)
    {
      std::string text = "struct " + definition.name + "\n{\n";
      std::uint32_t pad = 0;
      for (const StructMember& member : definition.members) {
        const bool matrix = member.type.shape == TypeSpec::Shape::Matrix;
        text += std::string("    ") + (matrix ? "row_major " : "") + member.type.spelling + " " + Rename(member.name);
        if (!member.semantic.empty()) {
          text += " : " + member.semantic;
        }
        text += ";\n";
        const std::uint32_t used = matrix ? 4U : (member.type.shape == TypeSpec::Shape::Vector ? member.type.columns : 1U);
        for (std::uint32_t lane = used; lane < 4; ++lane) {
          text += "    float FxGenPad" + std::to_string(pad++) + ";\n";
        }
      }
      return text + "};\n";
    }

    static std::string SlotArray(const ConstantSlot& slot)
    {
      return slot.elements != 0 ? "[" + std::to_string(slot.elements) + "]" : std::string();
    }

    // The entry wrapper's first statements: each written parameter's copy starts at its value.
    std::string WrittenCopyInit(const Usage& usage) const
    {
      std::string text;
      for (const int parameter : usage.writtenCopies) {
        const std::string name = Rename(metadata.parameters[static_cast<std::size_t>(parameter)].desc.name);
        text += "    FxGenW_" + name + " = " + name + ";\n";
      }
      return text;
    }

    static std::string Header(const ShaderEntry* entry, const char* stage)
    {
      std::string text = "// Generated by gpg::gal::fx::HlslEmitter (port/graphics/fx/src/FxHlslEmitter.cpp): ";
      text += stage;
      if (entry != nullptr) {
        text += " " + entry->profile + " " + entry->entry + "(";
        for (std::size_t i = 0; i < entry->arguments.size(); ++i) {
          text += (i != 0 ? ", " : "") + entry->arguments[i];
        }
        text += ")";
      }
      return text + "\n";
    }

    static std::string VaryingsStruct(const std::uint32_t varyings)
    {
      std::string text = "struct FxGenVaryings\n{\n    float4 FxGenPosition : SV_Position;\n";
      for (std::uint32_t i = 0; i < 10; ++i) {
        if ((varyings & (1U << i)) != 0) {
          text += "    float4 " + VaryingMember(1U << i) + " : " + VaryingSemantic(i) + ";\n";
        }
      }
      return text + "};\n";
    }

    static std::string VertexInputStruct(const VertexInputLayout& inputs, const std::uint32_t slots)
    {
      std::string text = "struct FxGenVertexInput\n{\n";
      for (std::uint32_t slot = 0; slot < kVertexSlotCount; ++slot) {
        if ((slots & (1U << slot)) == 0) {
          continue;
        }
        const InputKind kind = inputs.kinds[slot];
        const char* type = kind == InputKind::UInt ? "uint4" : kind == InputKind::SInt ? "int4" : "float4";
        text += std::string("    ") + type + " FxGenAttrib" + std::to_string(slot) + " : ATTRIB" + std::to_string(slot) + ";\n";
      }
      if (slots == 0) {
        // SV_VertexID keeps the struct non-empty; nothing reads it.
        text += "    uint FxGenVertexId : SV_VertexID;\n";
      }
      return text + "};\n";
    }

    // A float4 for a vertex input slot, as a vs_1_1/vs_2_0 input register holds it.
    static std::string SlotValue(const VertexInputLayout& inputs, const int slot, std::uint32_t& usedSlots)
    {
      if (slot < 0 || inputs.kinds[slot] == InputKind::Absent) {
        return "float4(0, 0, 0, 1)";
      }
      usedSlots |= 1U << static_cast<std::uint32_t>(slot);
      const std::string raw = "fxIn.FxGenAttrib" + std::to_string(slot);
      switch (inputs.kinds[slot]) {
      case InputKind::UNormBgra: return raw + ".bgra"; // D3DCOLOR is B, G, R, A in memory
      case InputKind::UInt:
      case InputKind::SInt: return "(float4)(" + raw + ")";
      default: return raw;
      }
    }

    // The value for one entry-point input of `type` (a member or a parameter) from its semantic.
    std::string VertexInputValue(const TypeInfo& type, const std::uint32_t arraySize, const Semantic& semantic,
                                 const VertexInputLayout& inputs, std::uint32_t& usedSlots, const SourceLocation where) const
    {
      if (type.kind != TypeInfo::Kind::Numeric) {
        throw FxError(where, "vertex input of a non-numeric type");
      }
      auto slotFor = [&](const std::uint32_t offset) {
        Semantic shifted = semantic;
        shifted.index += offset;
        return VertexSlotOf(shifted);
      };
      if (arraySize != 0) {
        // `int boneIndex[4] : BLENDINDICES`: element k is the x of usage index k.
        std::string list = "{ ";
        TypeInfo element = type;
        element.vector = false;
        element.columns = 1;
        for (std::uint32_t k = 0; k < arraySize; ++k) {
          list += (k != 0 ? ", " : "") + FromFloat4(SlotValue(inputs, slotFor(k), usedSlots), element);
        }
        return list + " }";
      }
      if (type.matrix) {
        std::string value = NumericSpelling(type) + "(";
        for (std::uint32_t row = 0; row < type.rows; ++row) {
          TypeInfo rowType = type;
          rowType.matrix = false;
          rowType.vector = true;
          value += (row != 0 ? ", " : "") + FromFloat4(SlotValue(inputs, slotFor(row), usedSlots), rowType);
        }
        return value + ")";
      }
      return FromFloat4(SlotValue(inputs, slotFor(0), usedSlots), type);
    }

    // `fxOut.<varying> = <value>;` for one vertex output, or nothing for an output the pixel shader
    // does not read (or one SM5 has no counterpart for: PSIZE, FOG).
    static std::string VertexOutputStatement(const Semantic& semantic, const std::string& value, const TypeInfo& type,
                                             const std::uint32_t varyings, std::uint32_t& written)
    {
      if (semantic.name == "POSITION" && semantic.index == 0) {
        return "    fxOut.FxGenPosition = " + ToFloat4(value, type) + ";\n";
      }
      const std::uint32_t bit = VaryingBitOf(semantic);
      if (bit == 0 || (varyings & bit) == 0) {
        return {};
      }
      written |= bit;
      std::string converted = ToFloat4(value, type);
      if (semantic.name == "COLOR") {
        // vs_1_1/vs_2_0 clamp the colour outputs (oD0/oD1) to [0, 1].
        converted = "saturate(" + converted + ")";
      }
      return "    fxOut." + VaryingMember(bit) + " = " + converted + ";\n";
    }

    // The arguments of the entry call; `before` gets the locals, `after` the output mapping.
    struct EntryCall
    {
      std::string before;
      std::string arguments;
      std::string after;
    };

    template <class InputFn, class OutputFn>
    EntryCall BuildEntryCall(const FunctionInfo& function, const ShaderEntry& entry, Usage& usage,
                             const std::map<std::string, std::string>& aliases, InputFn&& input, OutputFn&& output) const
    {
      EntryCall call;
      std::size_t uniformIndex = 0;
      EmitContext context;
      context.usage = &usage;
      for (std::size_t p = 0; p < function.params.size(); ++p) {
        const ParamInfo& param = function.params[p];
        const Semantic semantic = ParseSemantic(param.semantic);
        const bool bound = IsBoundUniform(param);
        if (bound && aliases.count(param.name) != 0) {
          ++uniformIndex; // the entry samples the global sampler directly (EmitReachable)
          continue;
        }
        if (!call.arguments.empty()) {
          call.arguments += ", ";
        }
        if (bound) {
          if (uniformIndex >= entry.arguments.size()) {
            throw FxError(entry.where, "no compile argument for uniform parameter '" + param.name + "'");
          }
          const std::string& argument = entry.arguments[uniformIndex++];
          if (param.type.kind == TypeInfo::Kind::Sampler) {
            std::string state;
            for (const TextureDim dim : function.samplerDims[p]) {
              const auto [texture, samplerState] = ResolveSampler(argument, dim, context, entry.where);
              call.arguments += texture + ", ";
              state = samplerState;
            }
            if (state.empty()) {
              state = SamplerStateOnly(argument, context, entry.where);
            }
            call.arguments += state;
          } else {
            // A literal or a parameter (frame.fx RangeBurn(rangeColor)): the parameter is read at run time.
            for (const auto& [name, info] : globals) {
              if (info.kind == GlobalKind::Parameter && argument.find(name) != std::string::npos) {
                const std::vector<Token> words = LexFragment(argument, entry.where);
                for (const Token& word : words) {
                  if (word.kind == TokenKind::Identifier && word.text == name) {
                    MarkIdentifier(name, context);
                  }
                }
              }
            }
            call.arguments += "(" + argument + ")";
          }
          continue;
        }
        const std::string local = "fxArg" + std::to_string(p);
        if (param.type.kind == TypeInfo::Kind::Struct) {
          MarkType(param.typeText, context);
          for (std::size_t k = param.begin; k < param.end; ++k) {
            if (tokens[k].kind == TokenKind::Identifier) {
              MarkType(tokens[k].text, context);
            }
          }
          call.before += "    " + param.typeText + " " + local + " = (" + param.typeText + ")0;\n";
          if (param.in) {
            for (const StructMember& member : param.type.structDef->members) {
              const Semantic memberSemantic = ParseSemantic(member.semantic);
              if (!memberSemantic.present) {
                continue;
              }
              const std::uint32_t arraySize = ArraySizeOf(member.dims);
              const std::string value = input(FromTypeSpec(member.type), arraySize, memberSemantic, member.where);
              if (!value.empty()) {
                call.before += "    " + local + "." + Rename(member.name) + " = " + value + ";\n";
              }
            }
          }
          if (param.out) {
            for (const StructMember& member : param.type.structDef->members) {
              call.after += output(ParseSemantic(member.semantic), local + "." + Rename(member.name), FromTypeSpec(member.type));
            }
          }
        } else {
          if (!semantic.present) {
            throw FxError(entry.where, "entry parameter '" + param.name + "' has no semantic");
          }
          std::string declared = param.typeText + " " + local;
          if (param.arraySize != 0) {
            declared += "[" + std::to_string(param.arraySize) + "]";
          }
          if (param.in) {
            call.before += "    " + declared + " = " + input(param.type, param.arraySize, semantic, entry.where) + ";\n";
          } else {
            call.before += "    " + declared + ";\n";
          }
          if (param.out) {
            call.after += output(semantic, local, param.type);
          }
        }
        call.arguments += local;
      }
      if (uniformIndex != entry.arguments.size()) {
        throw FxError(entry.where, "compile statement passes " + std::to_string(entry.arguments.size()) +
                                     " arguments, '" + entry.entry + "' takes " + std::to_string(uniformIndex));
      }
      return call;
    }

    std::string ReturnMapping(const FunctionInfo& function, Usage& usage,
                              const std::function<std::string(const Semantic&, const std::string&, const TypeInfo&)>& output) const
    {
      std::string after;
      if (function.returnType.kind == TypeInfo::Kind::Struct) {
        EmitContext context;
        context.usage = &usage;
        MarkTypeWords(function.begin, function.nameIndex, context);
        for (const StructMember& member : function.returnType.structDef->members) {
          after += output(ParseSemantic(member.semantic), "fxResult." + Rename(member.name), FromTypeSpec(member.type));
        }
      } else if (function.returnType.kind == TypeInfo::Kind::Numeric) {
        after += output(ParseSemantic(function.semantic), "fxResult", function.returnType);
      }
      return after;
    }

    static std::string AlphaTestFunction()
    {
      // D3D9's alpha test (D3DRS_ALPHATESTENABLE, ALPHAFUNC, ALPHAREF) after the pixel shader. The
      // draw path passes D3DCMP_ALWAYS (8) when the test is off.
      return "void FxGenAlphaTestDiscard(float alpha)\n{\n"
             "    float func = FxGenAlphaTest.x;\n"
             "    float reference = FxGenAlphaTest.y;\n"
             "    bool fxPass = true;\n"
             "    if (func < 1.5) fxPass = false;\n"
             "    else if (func < 2.5) fxPass = alpha < reference;\n"
             "    else if (func < 3.5) fxPass = alpha == reference;\n"
             "    else if (func < 4.5) fxPass = alpha <= reference;\n"
             "    else if (func < 5.5) fxPass = alpha > reference;\n"
             "    else if (func < 6.5) fxPass = alpha != reference;\n"
             "    else if (func < 7.5) fxPass = alpha >= reference;\n"
             "    if (!fxPass) discard;\n"
             "}\n";
    }

    static std::string PositionEpilogue()
    {
      // D3D9 samples pixel centres at integer coordinates, SM4+ at .5: move by half a pixel.
      return "    fxOut.FxGenPosition.xy += FxGenPositionOffset.xy * fxOut.FxGenPosition.w;\n";
    }
  };

  // ---- public ------------------------------------------------------------------------------

  HlslEmitter::HlslEmitter()
    : mImpl(std::make_unique<Impl>())
  {}

  HlslEmitter::~HlslEmitter() = default;

  std::unique_ptr<HlslEmitter> HlslEmitter::Create(
    const EffectInput& input, const EffectMetadata& metadata, std::vector<Diagnostic>& diagnostics
  )
  {
    std::unique_ptr<HlslEmitter> emitter(new HlslEmitter());
    Impl& impl = *emitter->mImpl;
    impl.metadata = metadata;
    impl.constants = BuildConstantLayout(metadata);
    for (const auto& [name, text] : input.parts) {
      impl.source.Append(name, text);
    }
    const std::size_t firstDiagnostic = diagnostics.size();
    const std::vector<Token> lexed = LexText(impl.source.Text(), diagnostics);
    impl.tokens = Preprocess(lexed, input.macros, diagnostics);
    auto failed = [&]() {
      for (std::size_t i = firstDiagnostic; i < diagnostics.size(); ++i) {
        if (diagnostics[i].severity == Diagnostic::Severity::Error) {
          return true;
        }
      }
      return false;
    };
    if (failed()) {
      return nullptr;
    }
    impl.ast = ParseEffect(impl.tokens, diagnostics);
    if (failed()) {
      return nullptr;
    }
    try {
      impl.IndexStructs(); // segmentation resolves typedefs against the structs
      impl.Segment();
      impl.IndexGlobals();
      impl.ParseFunctions();
      impl.ComputeSamplerDims();
      impl.ComputeWrittenParameters();
    } catch (const FxError& error) {
      diagnostics.push_back({Diagnostic::Severity::Error, error.Where(), error.what()});
      return nullptr;
    }
    return emitter;
  }

  const ConstantLayout& HlslEmitter::Constants() const
  {
    return mImpl->constants;
  }

  bool HlslEmitter::EmitPixelShader(const ShaderEntry& entry, EmittedStage& out, std::vector<Diagnostic>& diagnostics) const
  {
    const Impl& impl = *mImpl;
    out = EmittedStage{};
    try {
      if (entry.kind != ShaderEntry::Kind::Compile) {
        throw FxError(entry.where, "pixel shader is not a compile statement");
      }
      Usage usage;
      usage.pixel = true;
      std::map<std::string, std::string> aliases;
      std::map<std::size_t, std::string> functionText;
      const std::size_t index = impl.EmitReachable(entry, usage, functionText, aliases);
      const FunctionInfo& function = impl.functions[index];

      std::uint32_t varyings = 0;
      bool frontFace = false;
      auto input = [&](const TypeInfo& type, const std::uint32_t arraySize, const Semantic& semantic,
                       const SourceLocation where) -> std::string {
        if (arraySize != 0 || type.kind != TypeInfo::Kind::Numeric || type.matrix) {
          throw FxError(where, "unsupported pixel shader input type");
        }
        std::string value;
        if (semantic.name == "POSITION") {
          value = "fxIn.FxGenPosition";
        } else if (semantic.name == "VPOS") {
          // ps_3_0 VPOS is the pixel's integer position; SV_Position is its centre.
          value = "float4(fxIn.FxGenPosition.xy - 0.5, 0, 0)";
        } else if (semantic.name == "VFACE") {
          frontFace = true;
          value = "float4(fxFrontFace ? 1.0 : -1.0, 0, 0, 0)";
        } else {
          const std::uint32_t bit = VaryingBitOf(semantic);
          if (bit == 0) {
            throw FxError(where, "unsupported pixel shader input semantic '" + semantic.name + "'");
          }
          varyings |= bit;
          value = "fxIn." + VaryingMember(bit);
        }
        return FromFloat4(value, type);
      };
      std::uint32_t targets = 0;
      bool depth = false;
      auto output = [&](const Semantic& semantic, const std::string& value, const TypeInfo& type) -> std::string {
        if (!semantic.present) {
          return {};
        }
        if (semantic.name == "COLOR" && semantic.index < 4) {
          targets = std::max(targets, semantic.index + 1);
          return "    fxOut.FxGenTarget" + std::to_string(semantic.index) + " = " + ToFloat4(value, type) + ";\n";
        }
        if (semantic.name == "DEPTH" && semantic.index == 0) {
          depth = true;
          return "    fxOut.FxGenDepth = (float)(" + value + ");\n";
        }
        throw FxError(entry.where, "unsupported pixel shader output semantic '" + semantic.name + "'");
      };
      const Impl::EntryCall call = impl.BuildEntryCall(function, entry, usage, aliases, input, output);
      const std::string returned = impl.ReturnMapping(function, usage, output);

      std::string text = impl.EmitDeclarations(usage, functionText, out, Impl::Header(&entry, "pixel shader"));
      text += Impl::VaryingsStruct(varyings);
      text += "struct FxGenPixelOutput\n{\n";
      for (std::uint32_t t = 0; t < std::max<std::uint32_t>(targets, 1); ++t) {
        text += "    float4 FxGenTarget" + std::to_string(t) + " : SV_Target" + std::to_string(t) + ";\n";
      }
      if (depth) {
        text += "    float FxGenDepth : SV_Depth;\n";
      }
      text += "};\n";
      text += Impl::AlphaTestFunction();
      text += "FxGenPixelOutput main(FxGenVaryings fxIn";
      if (frontFace) {
        text += ", bool fxFrontFace : SV_IsFrontFace";
      }
      text += ")\n{\n    FxGenPixelOutput fxOut;\n";
      text += impl.WrittenCopyInit(usage);
      for (std::uint32_t t = 0; t < std::max<std::uint32_t>(targets, 1); ++t) {
        text += "    fxOut.FxGenTarget" + std::to_string(t) + " = float4(0, 0, 0, 0);\n";
      }
      if (depth) {
        text += "    fxOut.FxGenDepth = 0;\n";
      }
      text += call.before;
      const std::string invocation = Rename(function.name) + "(" + call.arguments + ")";
      if (function.returnType.kind == TypeInfo::Kind::Void) {
        text += "    " + invocation + ";\n";
      } else {
        text += "    " + function.returnTypeText + " fxResult = " + invocation + ";\n";
      }
      text += call.after + returned;
      if (targets > 0) {
        text += "    FxGenAlphaTestDiscard(fxOut.FxGenTarget0.a);\n";
      }
      text += "    return fxOut;\n}\n";
      out.source = std::move(text);
      out.varyings = varyings;
      out.renderTargets = std::max<std::uint32_t>(targets, 1);
      out.writesDepth = depth;
      return true;
    } catch (const FxError& error) {
      diagnostics.push_back({Diagnostic::Severity::Error, error.Where(), error.what()});
      return false;
    }
  }

  bool HlslEmitter::EmitVertexShader(
    const ShaderEntry& entry, const VertexInputLayout& inputs, const std::uint32_t varyings, EmittedStage& out,
    std::vector<Diagnostic>& diagnostics
  ) const
  {
    const Impl& impl = *mImpl;
    out = EmittedStage{};
    try {
      if (entry.kind != ShaderEntry::Kind::Compile) {
        throw FxError(entry.where, "vertex shader is not a compile statement");
      }
      Usage usage;
      std::map<std::string, std::string> aliases;
      std::map<std::size_t, std::string> functionText;
      const std::size_t index = impl.EmitReachable(entry, usage, functionText, aliases);
      const FunctionInfo& function = impl.functions[index];

      std::uint32_t usedSlots = 0;
      std::uint32_t written = 0;
      auto input = [&](const TypeInfo& type, const std::uint32_t arraySize, const Semantic& semantic,
                       const SourceLocation where) -> std::string {
        return impl.VertexInputValue(type, arraySize, semantic, inputs, usedSlots, where);
      };
      auto output = [&](const Semantic& semantic, const std::string& value, const TypeInfo& type) -> std::string {
        if (!semantic.present || type.kind != TypeInfo::Kind::Numeric) {
          return {};
        }
        return Impl::VertexOutputStatement(semantic, value, type, varyings, written);
      };
      const Impl::EntryCall call = impl.BuildEntryCall(function, entry, usage, aliases, input, output);
      const std::string returned = impl.ReturnMapping(function, usage, output);

      std::string text = impl.EmitDeclarations(usage, functionText, out, Impl::Header(&entry, "vertex shader"));
      text += Impl::VertexInputStruct(inputs, usedSlots);
      text += Impl::VaryingsStruct(varyings);
      text += "FxGenVaryings main(FxGenVertexInput fxIn)\n{\n    FxGenVaryings fxOut;\n";
      text += impl.WrittenCopyInit(usage);
      text += "    fxOut.FxGenPosition = float4(0, 0, 0, 1);\n";
      for (std::uint32_t i = 0; i < 10; ++i) {
        if ((varyings & (1U << i)) != 0) {
          text += "    fxOut." + VaryingMember(1U << i) + " = float4(0, 0, 0, 0);\n";
        }
      }
      text += call.before;
      const std::string invocation = Rename(function.name) + "(" + call.arguments + ")";
      if (function.returnType.kind == TypeInfo::Kind::Void) {
        text += "    " + invocation + ";\n";
      } else {
        text += "    " + function.returnTypeText + " fxResult = " + invocation + ";\n";
      }
      text += call.after + returned;
      text += Impl::PositionEpilogue();
      text += "    return fxOut;\n}\n";
      out.source = std::move(text);
      out.varyings = varyings;
      out.vertexSlots = usedSlots;
      return true;
    } catch (const FxError& error) {
      diagnostics.push_back({Diagnostic::Severity::Error, error.Where(), error.what()});
      return false;
    }
  }

  bool HlslEmitter::EmitFixedFunctionVertexShader(
    const VertexInputLayout& inputs, const std::uint32_t varyings, EmittedStage& out, std::vector<Diagnostic>& diagnostics
  ) const
  {
    out = EmittedStage{};
    std::uint32_t usedSlots = 0;
    const std::string position = Impl::SlotValue(inputs, kSlotPosition, usedSlots);
    std::string body;
    if (inputs.kinds[kSlotPosition] == InputKind::PositionT) {
      // D3D9 takes POSITIONT as render-target pixels (x, y), depth z and rhw = 1/w, and does no
      // vertex processing; turn it into the clip-space position the viewport maps back to those
      // pixels (absolute coordinates, so the viewport origin is subtracted) and depth.
      body += "    float4 fxP = " + position + ";\n"
              "    float fxW = 1.0 / fxP.w;\n"
              "    float2 fxNdc = float2((fxP.x - FxGenViewport.x) / FxGenViewport.z * 2.0 - 1.0,\n"
              "                          1.0 - (fxP.y - FxGenViewport.y) / FxGenViewport.w * 2.0);\n"
              "    float fxDepthRange = FxGenViewportDepth.y - FxGenViewportDepth.x;\n"
              "    float fxZ = fxDepthRange > 0.0 ? (fxP.z - FxGenViewportDepth.x) / fxDepthRange : 0.0;\n"
              "    fxOut.FxGenPosition = float4(fxNdc * fxW, fxZ * fxW, fxW);\n";
    } else {
      // Untransformed vertices with no vertex shader: D3D9 applies the world, view and projection
      // transforms, which the engine leaves at identity for these draws.
      body += "    fxOut.FxGenPosition = float4(" + position + ".xyz, 1.0);\n";
      diagnostics.push_back({Diagnostic::Severity::Warning, {}, "fixed-function vertex processing of untransformed vertices assumes identity transforms"});
    }
    for (std::uint32_t i = 0; i < 10; ++i) {
      const std::uint32_t bit = 1U << i;
      if ((varyings & bit) == 0) {
        continue;
      }
      std::string value;
      if (i == 0) {
        // No diffuse colour in the vertex: D3D9's default is opaque white.
        value = inputs.kinds[kSlotColor0] == InputKind::Absent ? "float4(1, 1, 1, 1)"
                                                                : "saturate(" + Impl::SlotValue(inputs, kSlotColor0, usedSlots) + ")";
      } else if (i == 1) {
        value = "float4(0, 0, 0, 0)"; // specular: none in the engine's formats
      } else {
        const int slot = static_cast<int>(kSlotTexcoord0 + (i - 2));
        value = inputs.kinds[slot] == InputKind::Absent ? "float4(0, 0, 0, 0)" : Impl::SlotValue(inputs, slot, usedSlots);
      }
      body += "    fxOut." + VaryingMember(bit) + " = " + value + ";\n";
    }
    std::string text = Impl::Header(nullptr, "fixed-function vertex shader (pre-transformed vertices)");
    text += "cbuffer FxGenDraw\n{\n"
            "    float4 FxGenViewport : packoffset(c0);\n"
            "    float4 FxGenViewportDepth : packoffset(c1);\n"
            "    float4 FxGenPositionOffset : packoffset(c2);\n"
            "    float4 FxGenAlphaTest : packoffset(c3);\n"
            "};\n";
    text += Impl::VertexInputStruct(inputs, usedSlots);
    text += Impl::VaryingsStruct(varyings);
    text += "FxGenVaryings main(FxGenVertexInput fxIn)\n{\n    FxGenVaryings fxOut;\n";
    text += body;
    text += Impl::PositionEpilogue();
    text += "    return fxOut;\n}\n";
    out.source = std::move(text);
    out.varyings = varyings;
    out.vertexSlots = usedSlots;
    return true;
  }

  bool HlslEmitter::EmitFixedFunctionPixelShader(EmittedStage& out, std::vector<Diagnostic>& diagnostics) const
  {
    static_cast<void>(diagnostics);
    out = EmittedStage{};
    std::string text = Impl::Header(nullptr, "fixed-function pixel shader (PixelShader = null)");
    text += "cbuffer FxGenDraw\n{\n"
            "    float4 FxGenViewport : packoffset(c0);\n"
            "    float4 FxGenViewportDepth : packoffset(c1);\n"
            "    float4 FxGenPositionOffset : packoffset(c2);\n"
            "    float4 FxGenAlphaTest : packoffset(c3);\n"
            "};\n";
    text += Impl::VaryingsStruct(kVaryingColor0);
    text += Impl::AlphaTestFunction();
    text += "float4 main(FxGenVaryings fxIn) : SV_Target0\n{\n"
            "    FxGenAlphaTestDiscard(fxIn.FxGenColor0.a);\n"
            "    return fxIn.FxGenColor0;\n}\n";
    out.source = std::move(text);
    out.varyings = kVaryingColor0;
    out.renderTargets = 1;
    return true;
  }

} // namespace gpg::gal::fx
