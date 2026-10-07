// Unit tests for FxHlslEmitter: the constant layout, the D3D9 rewrites and the stage wrappers.
// Whether the generated code compiles is checked by tools/fxhlsl --fxc and the fxdiff tool
// (FXC and Diligent's glslang path); whether it computes what D3D9 computes, by fxdiff.

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "gpg/gal/fx/FxHlslEmitter.h"
#include "gpg/gal/fx/FxMetadata.h"

namespace {

  using namespace gpg::gal::fx;

  int gFailures = 0;
  int gChecks = 0;

  void Check(const bool condition, const char* what, const int line)
  {
    ++gChecks;
    if (!condition) {
      ++gFailures;
      std::printf("FAILED (line %d): %s\n", line, what);
    }
  }

#define CHECK(condition) Check((condition), #condition, __LINE__)

  bool Contains(const std::string& text, const char* part)
  {
    return text.find(part) != std::string::npos;
  }

  struct Built
  {
    FrontEndResult result;
    std::unique_ptr<HlslEmitter> emitter;
    std::vector<Diagnostic> diagnostics;
  };

  const char* const kCompat = "#define AlphaState(p) p\n"
                              "#define FIXED_FUNC_VS null\n"
                              "#define AlphaBlend_SrcAlpha_InvSrcAlpha_Write_RGB AlphaBlendEnable = true; ColorWriteEnable = 0x07; SrcBlend = SrcAlpha; DestBlend = InvSrcAlpha;\n"
                              "#define CompatSwizzle(pixel)\n";

  Built Build(const std::string& text)
  {
    Built built;
    EffectInput input;
    input.parts.emplace_back("d3d9states.compat", kCompat);
    input.parts.emplace_back("test.fx", text);
    built.result = BuildEffectMetadata(input);
    if (built.result.ok) {
      built.emitter = HlslEmitter::Create(input, built.result.metadata, built.diagnostics);
    }
    return built;
  }

  const PassInfo& Pass(const Built& built, const std::size_t technique, const std::size_t pass = 0)
  {
    return built.result.metadata.techniques.at(technique).passes.at(pass);
  }

  VertexInputLayout Format6()
  {
    // VertexFormatTableD3D9.inl code 6: POSITION float3, COLOR0 D3DCOLOR, TEXCOORD0 float2.
    VertexInputLayout layout;
    layout.kinds[kSlotPosition] = InputKind::Float;
    layout.kinds[kSlotColor0] = InputKind::UNormBgra;
    layout.kinds[kSlotTexcoord0] = InputKind::Float;
    return layout;
  }

  const char* const kPrimBatcher = R"(
float4x4 CompositeMatrix;
texture Texture1;
float AlphaMultiplier = 1.0;
float time = 0;
float BlurKernel[] = { -3, -2, -1, 0, 1, 2, 3, };
int useOverlay = 2;
sampler LinearSampler = sampler_state { Texture = (Texture1); MipFilter = LINEAR; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = Clamp; };
sampler PointSampler = sampler_state { Texture = (Texture1); MipFilter = POINT; };
sampler2D NoTexture = sampler_state { MipFilter = POINT; };
static const float kHalf = 0.5;
struct VS_OUTPUT { float4 Pos : POSITION; float4 Color : COLOR0; float2 Tex1 : TEXCOORD0; };
struct UNUSED { float4 x : TEXCOORD5; };
float Unused(float x) { return x * time; }
VS_OUTPUT PrimBatcherVS(float3 Pos : POSITION, float4 Color : COLOR0, float2 Tex : TEXCOORD0)
{
    VS_OUTPUT Out;
    CompatSwizzle(Color);
    Out.Pos = mul(float4(Pos, 1), CompositeMatrix);
    Out.Color = Color;
    Out.Tex1 = Tex;
    return Out;
}
float4 Helper(sampler s, float2 uv) { return tex2D(s, uv) * kHalf; }
float4 PrimBatcherPS(float4 Pos : POSITION, float4 Diff : COLOR0, float2 Tex1 : TEXCOORD0, uniform sampler Samp) : COLOR
{
    float4 color = tex2D(Samp, Tex1) * Diff;
    color.a *= AlphaMultiplier;
    float AlphaMultiplier2 = 1;
    return color + Helper(PointSampler, Tex1) * BlurKernel[2];
}
float4 SkyPS(float3 dir : TEXCOORD0, float4 c : COLOR1) : COLOR { return texCUBE(LinearSampler, dir) + tex2Dproj(NoTexture, float4(dir, 2)) + c; }
float4 FlagPS(float2 uv : TEXCOORD1, uniform bool glow, uniform int func) : COLOR { return glow ? tex1D(PointSampler, uv.x) : float4(func, 0, 0, 1); }
technique T0 { pass P0 {
    AlphaState( AlphaBlend_SrcAlpha_InvSrcAlpha_Write_RGB )
    AlphaTestEnable = true; AlphaRef = 0; AlphaFunc = Greater;
    VertexShader = compile vs_1_1 PrimBatcherVS();
    PixelShader = compile ps_2_0 PrimBatcherPS(LinearSampler);
} }
technique T1 { pass P0 { VertexShader = compile vs_1_1 PrimBatcherVS(); PixelShader = compile ps_2_0 SkyPS(); } }
technique T2 { pass P0 { VertexShader = FIXED_FUNC_VS; PixelShader = compile ps_2_0 FlagPS(true, 3); } }
)";

  void TestConstantLayout()
  {
    const Built built = Build(kPrimBatcher);
    CHECK(built.result.ok);
    const ConstantLayout layout = BuildConstantLayout(built.result.metadata);
    // CompositeMatrix 4 rows, AlphaMultiplier 1, time 1, BlurKernel 7 elements, useOverlay 1.
    CHECK(layout.registerCount == 14);
    CHECK(layout.slots.size() == 5);
    CHECK(layout.slots[0].matrix && layout.slots[0].registerCount == 4 && layout.slots[0].registerOffset == 0);
    CHECK(layout.slots[1].registerOffset == 4);
    CHECK(layout.slots[3].elements == 7 && layout.slots[3].registerOffset == 6 && layout.slots[3].registerCount == 7);
    CHECK(layout.slots[4].type == ParameterType::Int && layout.slots[4].registerOffset == 13);
    // Defaults: AlphaMultiplier 1.0f at c4.x, BlurKernel[2] = -1.0f at c8.x, useOverlay 2 at c13.x.
    CHECK(layout.defaults[4 * 4] == 0x3F800000U);
    CHECK(layout.defaults[8 * 4] == 0xBF800000U);
    CHECK(layout.defaults[8 * 4 + 1] == 0U);
    CHECK(layout.defaults[13 * 4] == 2U);
    // Texture and samplers have no constant slot.
    CHECK(layout.slotOfParameter[1] == -1);
  }

  void TestPrimBatcherStages()
  {
    const Built built = Build(kPrimBatcher);
    CHECK(built.emitter != nullptr);
    if (!built.emitter) {
      return;
    }
    std::vector<Diagnostic> diagnostics;
    EmittedStage ps;
    CHECK(built.emitter->EmitPixelShader(Pass(built, 0).pixelShader, ps, diagnostics));
    CHECK(diagnostics.empty());
    // Reads COLOR0 and TEXCOORD0; POSITION is SV_Position.
    CHECK(ps.varyings == (kVaryingColor0 | kVaryingTexcoord0));
    CHECK(ps.renderTargets == 1);
    // The uniform sampler argument is passed by the wrapper, split into texture and SamplerState.
    // The uniform sampler argument specializes the entry: inside it, Samp is LinearSampler.
    CHECK(Contains(ps.source, "PrimBatcherPS(float4 Pos : POSITION, float4 Diff : COLOR0, float2 Tex1 : TEXCOORD0)"));
    CHECK(Contains(ps.source, "Texture1.Sample(LinearSampler, (float2)((Tex1)))"));
    CHECK(Contains(ps.source, "PrimBatcherPS(fxArg0, fxArg1, fxArg2)"));
    // A sampler parameter of a helper is not specialized: its lookups stay plain.
    CHECK(Contains(ps.source, "s_Fx2D.Sample(s, (float2)((uv)))"));
    CHECK(!Contains(ps.source, "FxGenPointSnap2D"));
    // The helper's sampler parameter is split too, and its call site passes texture and sampler.
    CHECK(Contains(ps.source, "Helper(Texture1, PointSampler, Tex1)"));
    // Both samplers of Texture1 bind one texture resource.
    CHECK(ps.textures.size() == 1 && ps.textures[0].name == "Texture1" && ps.textures[0].textureParameter == 1);
    CHECK(ps.samplers.size() == 2);
    // Only what the entry reaches: no Unused(), no UNUSED struct, no `time`, no CompositeMatrix.
    CHECK(!Contains(ps.source, "Unused("));
    CHECK(!Contains(ps.source, "struct UNUSED"));
    CHECK(!Contains(ps.source, " time "));
    CHECK(!Contains(ps.source, "CompositeMatrix"));
    // Parameters at their effect-wide registers; the static comes along.
    CHECK(Contains(ps.source, "float AlphaMultiplier : packoffset(c4);"));
    CHECK(Contains(ps.source, "float BlurKernel[7] : packoffset(c6);"));
    CHECK(Contains(ps.source, "static const float kHalf = 0.5;"));
    // A local hides nothing it should not: AlphaMultiplier2 is a local, not a parameter.
    CHECK(!Contains(ps.source, "AlphaMultiplier2 :"));
    // The alpha test runs on the colour output.
    CHECK(Contains(ps.source, "FxGenAlphaTestDiscard(fxOut.FxGenTarget0.a);"));

    EmittedStage vs;
    CHECK(built.emitter->EmitVertexShader(Pass(built, 0).vertexShader, Format6(), ps.varyings, vs, diagnostics));
    CHECK(vs.vertexSlots == ((1U << kSlotPosition) | (1U << kSlotColor0) | (1U << kSlotTexcoord0)));
    CHECK(Contains(vs.source, "row_major float4x4 CompositeMatrix : packoffset(c0);"));
    CHECK(Contains(vs.source, "float4 FxGenAttrib6 : ATTRIB6;"));
    // D3DCOLOR comes in as B, G, R, A bytes.
    CHECK(Contains(vs.source, "fxIn.FxGenAttrib6.bgra"));
    // vs_1_1 colour outputs are clamped.
    CHECK(Contains(vs.source, "fxOut.FxGenColor0 = saturate("));
    CHECK(Contains(vs.source, "fxOut.FxGenPosition.xy += FxGenPositionOffset.xy * fxOut.FxGenPosition.w;"));
    CHECK(vs.varyings == ps.varyings);
  }

  void TestDimensionsAndNullTexture()
  {
    const Built built = Build(kPrimBatcher);
    if (!built.emitter) {
      CHECK(false);
      return;
    }
    std::vector<Diagnostic> diagnostics;
    EmittedStage ps;
    CHECK(built.emitter->EmitPixelShader(Pass(built, 1).pixelShader, ps, diagnostics));
    // LinearSampler sampled as a cube: a cube resource of Texture1; NoTexture has no texture.
    CHECK(Contains(ps.source, "TextureCube Texture1_FxCube;"));
    CHECK(Contains(ps.source, "Texture2D FxGenNullTexture2D;"));
    // D3D9 samples cube maps face by face: the faces as a 2D array with CLAMP addressing, at the
    // level of detail the cube lookup has.
    CHECK(Contains(ps.source, "FxGenSampleCubeD3D9(Texture1_FxCubeFaces, LinearSampler_FxClamp, ((float3)(dir)), "
                              "Texture1_FxCube.CalculateLevelOfDetail(LinearSampler, ((float3)(dir))))"));
    CHECK(Contains(ps.source, "Texture2DArray Texture1_FxCubeFaces;"));
    CHECK(Contains(ps.source, "SamplerState LinearSampler_FxClamp;"));
    bool faces = false;
    for (const TextureResource& texture : ps.textures) {
      faces = faces || (texture.cubeFaces && texture.name == "Texture1_FxCubeFaces" && texture.textureParameter == 1);
    }
    CHECK(faces);
    // tex2Dproj divides by w.
    CHECK(Contains(ps.source, ".xy / ((float4)(float4(dir, 2))).w"));
    CHECK(ps.varyings == (kVaryingTexcoord0 | kVaryingColor1));

    // A layout without COLOR0 for a vertex shader that reads it: (0, 0, 0, 1) inside the shader.
    VertexInputLayout layout;
    layout.kinds[kSlotPosition] = InputKind::Float;
    layout.kinds[kSlotTexcoord0] = InputKind::Float;
    EmittedStage vs;
    CHECK(built.emitter->EmitVertexShader(Pass(built, 1).vertexShader, layout, ps.varyings, vs, diagnostics));
    CHECK(Contains(vs.source, "float4(0, 0, 0, 1)"));
    CHECK(!Contains(vs.source, "ATTRIB6"));
    // COLOR1 is read but never written: zero.
    CHECK(Contains(vs.source, "fxOut.FxGenColor1 = float4(0, 0, 0, 0);"));
  }

  void TestUniformLiteralsAndFixedFunction()
  {
    const Built built = Build(kPrimBatcher);
    if (!built.emitter) {
      CHECK(false);
      return;
    }
    std::vector<Diagnostic> diagnostics;
    EmittedStage ps;
    CHECK(built.emitter->EmitPixelShader(Pass(built, 2).pixelShader, ps, diagnostics));
    CHECK(Contains(ps.source, "FlagPS(fxArg0, (true), (3))"));
    // tex1D samples a 2D texture at v = 0.5.
    CHECK(Contains(ps.source, "float2((float)((uv.x)), 0.5)"));
    // PointSampler (MIN/MAG left at D3D9's POINT default, MIPFILTER POINT): D3D9's texel choice at
    // the level of detail of the unsnapped coordinates.
    CHECK(Contains(ps.source, "float2 FxGenPointSnap2D(Texture2D fxTexture, float fxLevel, float2 fxUv)"));
    CHECK(Contains(ps.source, "Texture1.SampleGrad(PointSampler, FxGenPointSnap2D(Texture1, floor((Texture1.CalculateLevelOfDetail(PointSampler"));
    CHECK(ps.varyings == (kVaryingTexcoord0 << 1));

    CHECK(Pass(built, 2).vertexShader.kind == ShaderEntry::Kind::Null);
    VertexInputLayout layout;
    layout.kinds[kSlotPosition] = InputKind::PositionT;
    layout.kinds[kSlotTexcoord0] = InputKind::Float;
    layout.kinds[kSlotTexcoord0 + 1] = InputKind::Float;
    EmittedStage vs;
    CHECK(built.emitter->EmitFixedFunctionVertexShader(layout, ps.varyings, vs, diagnostics));
    CHECK(Contains(vs.source, "fxOut.FxGenTexcoord1 = fxIn.FxGenAttrib8;"));
    CHECK(Contains(vs.source, "float fxW = 1.0 / fxP.w;"));
    CHECK(vs.vertexSlots == ((1U << kSlotPosition) | (1U << (kSlotTexcoord0 + 1))));
  }

  void TestParameterWrites()
  {
    // A write to an effect parameter (water2.fx:443) works on a per-invocation copy, as the legacy
    // compiler's code does; reads elsewhere see the copy too.
    const Built built = Build(R"(
float4 Tint;
float Gain = 2;
float Scaled(float x) { return x * Tint.y; }
float4 WritePS(float2 uv : TEXCOORD0) : COLOR { if (Gain == 2) Tint.x = uv.x; return Tint * Scaled(Gain); }
technique T { pass P { PixelShader = compile ps_2_0 WritePS(); } }
)");
    if (!built.emitter) {
      CHECK(false);
      return;
    }
    std::vector<Diagnostic> diagnostics;
    EmittedStage ps;
    CHECK(built.emitter->EmitPixelShader(Pass(built, 0).pixelShader, ps, diagnostics));
    CHECK(Contains(ps.source, "static float4 FxGenW_Tint;"));
    CHECK(Contains(ps.source, "    FxGenW_Tint = Tint;"));
    CHECK(Contains(ps.source, "FxGenW_Tint.x = uv.x;"));
    CHECK(Contains(ps.source, "return x * FxGenW_Tint.y;"));
    // Gain is only read: no copy.
    CHECK(!Contains(ps.source, "FxGenW_Gain"));
    CHECK(Contains(ps.source, "float4 Tint : packoffset(c0);"));
  }

  void TestStructParameters()
  {
    // sky.fx's Cirrus aCirrus[4] (sky.fx:99-107): every member starts a register, the struct is
    // padded to match, and SetValue's tightly packed floats land in the right lanes.
    const Built built = Build(R"(
struct Cirrus { float2 frequency; float1 speed; float2 direction; };
float Before = 1;
Cirrus aCirrus[2];
float2 Coord(float t, Cirrus c) { return c.frequency * t + c.direction * c.speed; }
float4 SkyPS(float2 uv : TEXCOORD0) : COLOR { return float4(Coord(uv.x, aCirrus[1]), uv); }
technique T { pass P { PixelShader = compile ps_2_0 SkyPS(); } }
)");
    if (!built.emitter) {
      CHECK(false);
      return;
    }
    const ConstantLayout& layout = built.emitter->Constants();
    CHECK(layout.slots.size() == 2);
    CHECK(layout.slots[1].structure && layout.slots[1].elementRegisters == 3 && layout.slots[1].registerOffset == 1);
    CHECK(layout.registerCount == 7);
    // Element 1, `direction.y`: tight component 5 + 4 = 9 -> c1 + 3 + 2 = c6.y.
    std::uint32_t word = 0;
    ParameterType type = ParameterType::Void;
    CHECK(ConstantComponentWord(layout.slots[1], 9, word, type) && word == 6 * 4 + 1 && type == ParameterType::Float);
    // Element 0, `speed`: component 2 -> c2.x.
    CHECK(ConstantComponentWord(layout.slots[1], 2, word, type) && word == 2 * 4);
    CHECK(!ConstantComponentWord(layout.slots[1], 10, word, type));
    std::vector<Diagnostic> diagnostics;
    EmittedStage ps;
    CHECK(built.emitter->EmitPixelShader(Pass(built, 0).pixelShader, ps, diagnostics));
    CHECK(Contains(ps.source, "struct Cirrus\n{\n    float2 frequency;\n    float FxGenPad0;\n    float FxGenPad1;\n    float1 speed;"));
    CHECK(Contains(ps.source, "Cirrus aCirrus[2] : packoffset(c1);"));
  }

  void TestErrors()
  {
    // An effect-parameter struct with an array member is refused rather than miscompiled: HLSL
    // and std140 pad what follows an array differently.
    const Built built = Build(R"(
struct Layer { float2 frequency[2]; float1 speed; };
Layer aLayer[2];
float4 StructPS(float2 uv : TEXCOORD0) : COLOR { return float4(aLayer[1].speed, uv, 1); }
technique T { pass P { PixelShader = compile ps_2_0 StructPS(); } }
)");
    if (!built.emitter) {
      CHECK(false);
      return;
    }
    std::vector<Diagnostic> diagnostics;
    EmittedStage ps;
    CHECK(!built.emitter->EmitPixelShader(Pass(built, 0).pixelShader, ps, diagnostics));
    CHECK(!diagnostics.empty() && Contains(diagnostics[0].message, "'aLayer' is a struct"));
  }

} // namespace

int main()
{
  TestConstantLayout();
  TestPrimBatcherStages();
  TestDimensionsAndNullTexture();
  TestUniformLiteralsAndFixedFunction();
  TestParameterWrites();
  TestStructParameters();
  TestErrors();
  std::printf("fx_hlsl_tests: %d checks, %d failed\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
