#pragma once

// FxMetadata: what ID3DXEffect reports about an effect, computed without
// D3DX.
//
// - Parameters as ID3DXEffect::GetParameterDesc describes them (D3DX's
//   class/type numbering, rows, columns, elements, struct members, flags,
//   bytes), their default values as GetValue returns them, string values,
//   annotations, and for samplers the sampler_state assignments D3DX applies
//   when a pass uses the sampler.
// - Techniques and passes with their annotations, the state assignments of
//   each pass in source order as the DWORDs D3DX passes to
//   SetRenderState / SetTextureStageState / SetSamplerState, and the vertex
//   and pixel shader entry points (profile, function, uniform arguments).
// - Technique validity for a device profile: the D3D9 backend lists only the
//   techniques FindNextValidTechnique accepts (D3D9Interfaces.cpp:4226-4250),
//   and D3DX rejects a technique when the device refuses one of its shaders.
//
// Byte sizes of object parameters are those of the Win32 engine (a pointer
// is 4 bytes), as main.exe sees them.
//
// scripts/port/fx_metadata_gate.py checks all of it against D3DX on every
// effect the game and FAF load.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "gpg/gal/fx/FxPreprocessor.h"
#include "gpg/gal/fx/FxSource.h"
#include "gpg/gal/fx/FxStates.h"

namespace gpg::gal::fx {

  // D3DXPARAMETER_CLASS
  enum class ParameterClass : std::uint32_t
  {
    Scalar = 0,
    Vector = 1,
    MatrixRows = 2,
    MatrixColumns = 3,
    Object = 4,
    Struct = 5
  };

  // D3DXPARAMETER_TYPE
  enum class ParameterType : std::uint32_t
  {
    Void = 0,
    Bool = 1,
    Int = 2,
    Float = 3,
    String = 4,
    Texture = 5,
    Texture1D = 6,
    Texture2D = 7,
    Texture3D = 8,
    TextureCube = 9,
    Sampler = 10,
    Sampler1D = 11,
    Sampler2D = 12,
    Sampler3D = 13,
    SamplerCube = 14,
    PixelShader = 15,
    VertexShader = 16
  };

  constexpr std::uint32_t kParameterShared = 1; // D3DX_PARAMETER_SHARED
  constexpr std::uint32_t kParameterAnnotation = 4; // D3DX_PARAMETER_ANNOTATION, set on every annotation

  struct ParameterDesc
  {
    std::string name;
    std::string semantic; // empty: none
    ParameterClass parameterClass = ParameterClass::Scalar;
    ParameterType type = ParameterType::Void;
    std::uint32_t rows = 0;
    std::uint32_t columns = 0;
    std::uint32_t elements = 0;
    std::uint32_t structMembers = 0;
    std::uint32_t flags = 0;
    std::uint32_t bytes = 0;
  };

  struct AnnotationInfo
  {
    ParameterDesc desc;
    std::vector<std::uint32_t> value; // numeric annotations: 4 bytes per component
    std::string string; // string annotations
  };

  struct SamplerStateValue
  {
    std::uint32_t state = 0; // D3DSAMPLERSTATETYPE
    std::uint32_t value = 0;
    bool dynamic = false; // computed from a parameter at BeginPass
  };

  struct ParameterInfo
  {
    ParameterDesc desc;
    std::vector<AnnotationInfo> annotations;
    std::vector<std::uint32_t> value; // numeric and struct parameters: the GetValue bytes as words
    std::string string; // string parameters
    std::vector<ParameterDesc> members; // struct parameters: the members of one element
    bool hasSamplerState = false;
    std::string samplerTexture; // sampler_state Texture, empty if none
    bool samplerTextureNull = false; // Texture = NULL
    std::vector<SamplerStateValue> samplerStates; // sampler_state order, Texture excluded
  };

  struct ShaderEntry
  {
    enum class Kind : std::uint8_t
    {
      Unassigned, // the pass leaves the shader alone
      Null, // `= null` (FIXED_FUNC_VS / FIXED_FUNC_PS in d3d9states.compat)
      Compile, // compile <profile> <entry>(<args>)
      Asm // asm { } block
    };

    Kind kind = Kind::Unassigned;
    std::string profile; // as written: vs_1_1, ps_2_a
    std::uint32_t versionToken = 0; // the bytecode version token the profile compiles to
    std::string entry;
    std::vector<std::string> arguments; // uniform arguments as written
    SourceLocation where;
  };

  struct PassState
  {
    StateOp op = StateOp::Render;
    std::uint32_t index = 0; // stage / sampler index
    std::uint32_t state = 0;
    std::uint32_t value = 0;
    std::string texture; // StateOp::Texture: the texture parameter, empty for NULL
    bool dynamic = false;
  };

  struct PassInfo
  {
    std::string name;
    bool named = false;
    std::vector<AnnotationInfo> annotations;
    std::vector<PassState> states; // source order, shaders excluded
    ShaderEntry vertexShader;
    ShaderEntry pixelShader;
  };

  struct TechniqueInfo
  {
    std::string name;
    bool named = false;
    std::vector<AnnotationInfo> annotations;
    std::vector<PassInfo> passes;
  };

  struct EffectMetadata
  {
    std::vector<ParameterInfo> parameters;
    std::vector<TechniqueInfo> techniques;
  };

  /// What a device accepts: the highest vertex and pixel shader bytecode
  /// version (0xMMmm, ps_2_a/ps_2_b compile to 2.1).
  struct DeviceProfile
  {
    const char* name;
    std::uint32_t maxVertexShader;
    std::uint32_t maxPixelShader;
  };

  /// "all" (any SM3 device: everything the corpus uses), "sm2" (vs/ps 2.0
  /// without the 2_x extensions) and "sm1" (vs 1.1 / ps 1.4).
  [[nodiscard]] const std::vector<DeviceProfile>& StandardDeviceProfiles();

  /// A technique is valid when the device accepts the shader of every pass.
  [[nodiscard]] bool IsTechniqueValid(const TechniqueInfo& technique, const DeviceProfile& profile);

  /// The bytecode version token a profile compiles to (0xFFFE0101 for
  /// vs_1_1, 0xFFFF0201 for ps_2_a), or 0 for an unknown profile.
  [[nodiscard]] std::uint32_t ProfileVersionToken(const std::string& profile);

  /// "vs_1_1", "ps_2_0", "ps_2_x", "vs_3_0" for a version token.
  [[nodiscard]] std::string VersionTokenName(std::uint32_t token);

  /// One effect as the engine assembles it: the compat prelude and the .fx
  /// text as separate parts, concatenated without a separator.
  struct EffectInput
  {
    std::vector<std::pair<std::string, std::string>> parts; // (name, text)
    std::vector<MacroDefinition> macros;
  };

  struct FrontEndResult
  {
    bool ok = false;
    SourceBuffer source;
    std::vector<Diagnostic> diagnostics;
    EffectMetadata metadata;
  };

  /// Lexes, preprocesses and parses the effect and builds its metadata.
  [[nodiscard]] FrontEndResult BuildEffectMetadata(const EffectInput& input);

  /// Preprocessed text only (for `fxmeta --preprocess`).
  [[nodiscard]] std::string PreprocessEffect(const EffectInput& input, std::vector<Diagnostic>& diagnostics,
                                             SourceBuffer& source);

} // namespace gpg::gal::fx
