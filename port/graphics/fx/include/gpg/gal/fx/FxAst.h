#pragma once

// The effect-level syntax tree FxEffectParser produces.
//
// It covers what the D3D9 effect framework reflects: global variables (the
// effect parameters) with types, semantics, annotations and initializers;
// sampler_state blocks; struct and typedef declarations; techniques and
// passes with annotations and state assignments, including
// `compile <profile> <function>(<args>)`. Function bodies are not parsed:
// a function is its name, return type, parameter text and whether it has a
// body. Shader code generation (FxSema / FxHlslEmitter, docs/port/README.md)
// will extend this tree; metadata needs no more.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gpg/gal/fx/FxSource.h"

namespace gpg::gal::fx {

  enum class ScalarType : std::uint8_t
  {
    Void,
    Bool,
    Int,
    UInt,
    Half,
    Float,
    Double
  };

  enum class ObjectType : std::uint8_t
  {
    None,
    String,
    Texture,
    Texture1D,
    Texture2D,
    Texture3D,
    TextureCube,
    Sampler,
    Sampler1D,
    Sampler2D,
    Sampler3D,
    SamplerCube,
    PixelShader,
    VertexShader
  };

  struct StructDef;

  struct TypeSpec
  {
    enum class Shape : std::uint8_t
    {
      Void,
      Scalar, // float, int, ...
      Vector, // floatN, vector<T,N>; float1 is a vector of one (D3DX reports D3DXPC_VECTOR)
      Matrix, // floatRxC, matrix<T,R,C>
      Object, // string, texture*, sampler*, pixelshader, vertexshader
      Struct
    };

    Shape shape = Shape::Void;
    ScalarType scalar = ScalarType::Void;
    ObjectType object = ObjectType::None;
    std::uint8_t rows = 1;
    std::uint8_t columns = 1;
    const StructDef* structDef = nullptr;
    std::string spelling; // as written, for messages

    [[nodiscard]] bool IsNumeric() const
    {
      return shape == Shape::Scalar || shape == Shape::Vector || shape == Shape::Matrix;
    }

    [[nodiscard]] std::uint32_t ComponentCount() const { return IsNumeric() ? std::uint32_t{rows} * columns : 0U; }
  };

  struct Expr;
  using ExprPtr = std::unique_ptr<Expr>;

  enum class ExprKind : std::uint8_t
  {
    Number, // text holds the literal
    Bool, // text is "true" or "false"
    String, // text holds the unquoted, unescaped string
    Identifier, // text
    Unary, // text = operator, operands[0]
    Binary, // text = operator, operands[0..1]
    Ternary, // operands[0] ? operands[1] : operands[2]
    Call, // text = function name, operands = arguments
    Constructor, // type(operands...)
    Cast, // (type)operands[0]
    InitList, // { operands... }
    Member, // operands[0].text
    Index, // operands[0][operands[1]]
    Compile, // compile text(profile) entry(operands...)
    Null, // null / NULL
    AngleRef, // <text> in a state value: a reference to a parameter
    Asm // asm { ... }; text = the block as written
  };

  struct Expr
  {
    ExprKind kind = ExprKind::Number;
    SourceLocation where;
    std::string text;
    std::string entry; // Compile: the function name
    TypeSpec type; // Constructor / Cast
    std::vector<ExprPtr> operands;
    std::string spelling; // the expression as written (tokens joined)
  };

  struct ArrayDim
  {
    ExprPtr size; // null for `[]`
  };

  struct Annotation
  {
    TypeSpec type;
    std::string name;
    std::vector<ArrayDim> dims;
    ExprPtr value;
    SourceLocation where;
  };

  struct StateAssignment
  {
    std::string name;
    ExprPtr index; // `Name[index] = ...`
    ExprPtr value;
    SourceLocation where;
  };

  struct StructMember
  {
    TypeSpec type;
    std::string name;
    std::vector<ArrayDim> dims;
    std::string semantic;
    SourceLocation where;
  };

  struct StructDef
  {
    std::string name; // empty for an anonymous struct
    std::vector<StructMember> members;
    SourceLocation where;
  };

  struct VariableDecl
  {
    bool isStatic = false;
    bool isUniform = false;
    bool isExtern = false;
    bool isShared = false;
    bool isConst = false;
    bool isVolatile = false;
    bool rowMajor = false;
    bool columnMajor = false;
    TypeSpec type;
    std::string name;
    std::vector<ArrayDim> dims;
    std::string semantic;
    std::string registerBinding; // ": register(...)" as written
    std::vector<Annotation> annotations;
    ExprPtr init;
    bool hasSamplerState = false;
    std::vector<StateAssignment> samplerStates;
    SourceLocation where;
  };

  struct FunctionDecl
  {
    std::string name;
    TypeSpec returnType;
    std::string parameters; // the parameter list as written
    std::string semantic;
    bool hasBody = false;
    SourceLocation where;
  };

  struct PassDecl
  {
    std::string name;
    bool named = false;
    std::vector<Annotation> annotations;
    std::vector<StateAssignment> states;
    SourceLocation where;
  };

  struct TechniqueDecl
  {
    std::string name;
    bool named = false;
    std::vector<Annotation> annotations;
    std::vector<PassDecl> passes;
    SourceLocation where;
  };

  struct EffectAst
  {
    std::vector<std::unique_ptr<StructDef>> structs;
    std::vector<VariableDecl> globals; // declaration order, static ones included
    std::vector<FunctionDecl> functions;
    std::vector<TechniqueDecl> techniques;
  };

} // namespace gpg::gal::fx
