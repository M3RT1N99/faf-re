#pragma once

// FxHlslEmitter: Shader Model 5 HLSL for one pass stage of a D3D9 effect, generated at load time,
// plus what a backend has to bind for it.
//
// The engine's effects are fx_2_0 HLSL compiled by the October 2006 legacy compiler (vs_1_1,
// vs_2_0, ps_2_0, ps_2_a; port/graphics/fx/README.md). Each pass stage becomes one SM5 shader with
// the entry point "main": the effect's own functions, structs and static globals that the stage's
// entry point reaches (and nothing else, so one function a stage does not use cannot fail it,
// m6u-CRIT.txt R5), rewritten where SM5 differs from D3D9, and a wrapper that turns D3D9's stage
// interface into SM5's. The D3D9 behaviours kept (docs/port/README.md, the table under "Planned
// effect front end"):
//
//   - samplers: a D3D9 sampler is a texture plus states. Each becomes an SM5 SamplerState of the
//     same name and one texture resource per dimension it is sampled with (tex2D -> Texture2D,
//     texCUBE -> TextureCube, tex3D -> Texture3D; tex1D samples a 2D texture at v = 0.5). The
//     resource is named after the sampler_state's texture parameter ("Texture1", "Texture1_FxCube"),
//     so every sampler of one texture binds one resource. tex*proj/lod/bias/grad map to Sample with
//     the divide, SampleLevel, SampleBias and SampleGrad.
//   - sampler parameters of functions are split into texture and SamplerState parameters, with the
//     dimensions the function samples them with; call sites pass both.
//   - uniform arguments of the compile statement (`compile ps_2_0 PrimBatcherPS(LinearSampler)`)
//     are passed by the wrapper; the entry function is otherwise unchanged.
//   - effect parameters live in one constant buffer, FxGenParams, one float4 register per scalar,
//     vector or array element and one per matrix row (row_major, so the matrix the engine sets is the
//     matrix the shader multiplies with, as D3DX does), placed with packoffset. Only the parameters a
//     stage reads are declared; their offsets are the effect's (ConstantLayout), so every stage of
//     an effect shares one buffer.
//   - vertex inputs: ATTRIB<n> by D3D9 usage (VertexSlot), converted to what a vs_1_1/vs_2_0 input
//     register holds (D3DCOLOR swizzled .bgra, UBYTE4/SHORT4 as floats, missing inputs (0, 0, 0, 1)).
//   - vertex COLOR outputs are clamped to [0, 1] (D3D9 clamps oD0/oD1 for shader models below 3.0).
//   - varyings: SV_Position, COLOR0..1 and TEXCOORD0..7, each a float4, in that order; the vertex
//     shader writes exactly what the pixel shader reads, zero where it has nothing.
//   - D3D9's half-pixel offset is added to the clip-space position from the draw constants.
//   - fixed-function alpha test (AlphaTestEnable/AlphaFunc/AlphaRef) is a `discard` in the pixel
//     wrapper, driven by the draw constants, because the engine sets alpha test state outside the
//     effect too.
//   - VertexShader = null (FIXED_FUNC_VS in d3d9states.compat) and pre-transformed (POSITIONT)
//     vertices: a generated vertex shader doing what D3D9's fixed-function pipeline does for
//     transformed vertices (render-target pixels to clip space, texture coordinates and colour
//     passed through), not the effects' own D3D10 FixedFuncVS, which remaps texture coordinates
//     (m6u-FX.txt section 5).
//   - PixelShader = null: a pixel shader returning the interpolated COLOR0.
//
// Not yet (errors, so nothing slips through silently): asm blocks, MRT beyond SV_Target3. (Struct
// parameters and writes to parameters are handled: ConstantSlot::structure, ComputeWrittenParameters.)
// D3D9 integer arithmetic is real integer arithmetic in SM5 (ps_2_x emulates it with floats).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gpg/gal/fx/FxMetadata.h"

namespace gpg::gal::fx {

  // ---- vertex inputs ---------------------------------------------------------------------

  /// The ATTRIB<n> slot of each D3D9 vertex usage the engine's 24 vertex formats use. The same
  /// numbers are the backend's contract (port/graphics/diligent/PassBinding.h VertexAttributeSlot).
  enum VertexSlot : std::uint32_t
  {
    kSlotPosition = 0, // POSITION0, POSITIONT0
    kSlotPosition1 = 1,
    kSlotNormal = 2,
    kSlotTangent = 3,
    kSlotBinormal = 4,
    kSlotBlendIndices = 5,
    kSlotColor0 = 6,
    kSlotTexcoord0 = 7, // TEXCOORD0..7 -> 7..14
    kVertexSlotCount = 15
  };

  /// How a slot is fed (PassBinding.h AttribKind has the same values).
  enum class InputKind : std::uint8_t
  {
    Absent = 0,
    Float = 1,
    UNormBgra = 2, // D3DCOLOR: bytes B, G, R, A as UNORM8x4
    UInt = 3, // UBYTE4 as UINT8x4
    SInt = 4, // SHORT2/4 as SINT16xN
    PositionT = 5 // pre-transformed position (slot 0)
  };

  struct VertexInputLayout
  {
    InputKind kinds[kVertexSlotCount] = {};
  };

  // ---- varyings --------------------------------------------------------------------------

  /// One bit per varying after SV_Position, in their order.
  enum VaryingBit : std::uint32_t
  {
    kVaryingColor0 = 1U << 0,
    kVaryingColor1 = 1U << 1,
    kVaryingTexcoord0 = 1U << 2 // TEXCOORDn is bit 2 + n
  };

  // ---- constants -------------------------------------------------------------------------

  /// One numeric effect parameter in FxGenParams.
  struct ConstantSlot
  {
    std::size_t parameter = 0; // index into EffectMetadata::parameters
    std::uint32_t registerOffset = 0; // float4 register (members: relative to their element)
    std::uint32_t registerCount = 0;
    std::uint32_t elements = 0; // 0: not an array
    std::uint32_t rows = 1; // matrix rows (1 for scalars and vectors)
    std::uint32_t columns = 1;
    ParameterType type = ParameterType::Float; // Bool, Int or Float (structs: Void)
    bool matrix = false;
    /// A struct parameter (sky.fx `Cirrus aCirrus[4]`): every member starts a register, so an
    /// element takes elementRegisters registers; the HLSL struct is padded to that layout.
    bool structure = false;
    std::uint32_t elementRegisters = 0;
    std::vector<ConstantSlot> members;
  };

  struct ConstantLayout
  {
    std::vector<ConstantSlot> slots;
    std::vector<int> slotOfParameter; // per metadata parameter: index into slots, -1 if none
    std::uint32_t registerCount = 0;
    /// The default values (FxMetadata ParameterInfo::value) in register layout: registerCount * 4
    /// words; floats as bits, ints as ints, bools as 0/1.
    std::vector<std::uint32_t> defaults;
  };

  /// The register layout of every numeric parameter of an effect, in parameter order. Struct
  /// parameters whose members are scalars, vectors or four-column matrices get a slot; others
  /// none (a stage reading one fails to emit).
  [[nodiscard]] ConstantLayout BuildConstantLayout(const EffectMetadata& metadata);

  /// Where component `component` of a parameter (ID3DXEffect storage order: element, member, row,
  /// column; what SetFloatArray and SetValue fill) lives: the word index in the register words
  /// (register * 4 + lane) and its type. False past the last component.
  bool ConstantComponentWord(const ConstantSlot& slot, std::uint32_t component, std::uint32_t& word, ParameterType& type);

  /// The draw constants buffer every generated stage declares (FxGenDraw), as float4 registers:
  ///   c0 FxGenViewport        x, y, width, height of the device viewport, in pixels
  ///   c1 FxGenViewportDepth   x: MinZ, y: MaxZ
  ///   c2 FxGenPositionOffset  xy: added to clip-space xy times w after the vertex shader
  ///   c3 FxGenAlphaTest       x: D3DCMPFUNC (8 = ALWAYS: no test), y: AlphaRef / 255
  constexpr std::uint32_t kDrawConstantRegisters = 4;

  // ---- resources -------------------------------------------------------------------------

  enum class TextureDim : std::uint8_t
  {
    Tex2D,
    Tex3D,
    Cube
  };

  struct TextureResource
  {
    std::string name; // the HLSL resource name ("Texture1", "Texture1_FxCube", "FxGenNullTexture2D")
    int textureParameter = -1; // metadata parameter index; -1: none (samples D3D9's null texture)
    TextureDim dim = TextureDim::Tex2D;
    /// A Texture2DArray of the cube texture's six faces ("Texture1_FxCubeFaces", dim Cube): bind a
    /// 2D-array view of the same cube texture. D3D9 samples cube maps face by face, clamped at the
    /// face edges; D3D10 and later filter across faces (seamless), so texCUBE samples the faces.
    bool cubeFaces = false;
  };

  struct SamplerResource
  {
    std::string name; // the SamplerState name (the effect's sampler parameter name)
    int samplerParameter = -1; // metadata parameter index (its sampler_state gives the states)
    /// "<sampler>_FxClamp": the same states with CLAMP addressing, for the cube faces.
    bool clampAddress = false;
  };

  struct EmittedStage
  {
    std::string source; // complete HLSL, entry point "main"
    bool usesParams = false; // declares FxGenParams
    std::vector<TextureResource> textures;
    std::vector<SamplerResource> samplers;
    std::uint32_t varyings = 0; // vertex: written; pixel: read
    std::uint32_t vertexSlots = 0; // vertex: bit per slot declared as an input
    std::uint32_t renderTargets = 0; // pixel: SV_Target count
    bool writesDepth = false; // pixel
  };

  class HlslEmitter
  {
  public:
    /// Parses the effect for shader generation: the same input BuildEffectMetadata takes, and its
    /// metadata. Returns null (with diagnostics) if the source does not parse.
    [[nodiscard]] static std::unique_ptr<HlslEmitter> Create(
      const EffectInput& input, const EffectMetadata& metadata, std::vector<Diagnostic>& diagnostics
    );

    ~HlslEmitter();
    HlslEmitter(const HlslEmitter&) = delete;
    HlslEmitter& operator=(const HlslEmitter&) = delete;

    [[nodiscard]] const ConstantLayout& Constants() const;

    /// A pixel shader for a compile statement (ShaderEntry::Kind::Compile). Emit it before the
    /// vertex shader of the same pass: the vertex shader writes the varyings it reads.
    bool EmitPixelShader(const ShaderEntry& entry, EmittedStage& out, std::vector<Diagnostic>& diagnostics) const;

    /// A vertex shader for a compile statement, reading `inputs` and writing `varyings`.
    bool EmitVertexShader(
      const ShaderEntry& entry, const VertexInputLayout& inputs, std::uint32_t varyings, EmittedStage& out,
      std::vector<Diagnostic>& diagnostics
    ) const;

    /// D3D9's fixed-function vertex processing of pre-transformed vertices (POSITIONT in slot 0):
    /// for `VertexShader = null` passes and for any pass drawn with a POSITIONT layout.
    bool EmitFixedFunctionVertexShader(
      const VertexInputLayout& inputs, std::uint32_t varyings, EmittedStage& out, std::vector<Diagnostic>& diagnostics
    ) const;

    /// `PixelShader = null`: returns the interpolated COLOR0.
    bool EmitFixedFunctionPixelShader(EmittedStage& out, std::vector<Diagnostic>& diagnostics) const;

  private:
    HlslEmitter();
    struct Impl;
    std::unique_ptr<Impl> mImpl;
  };

} // namespace gpg::gal::fx
