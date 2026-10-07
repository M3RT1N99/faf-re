// The glslang section of libfafdeviceprobe.so: how long the phone takes to turn HLSL into Vulkan
// SPIR-V the way Diligent does it at run time (Graphics/ShaderTools/src/GLSLangUtils.cpp,
// HLSLtoSPIRV: glslang's HLSL front end for Vulkan 1.0 / SPIR-V 1.0, then SPIRV-Tools' legalization
// and performance passes, SPIRVTools.cpp OptimizeSPIRV).
//
// PLACEHOLDER SHADER SET. No game content: the M1 renderer's two splash shaders
// (port/android/src/Renderer.cpp) and two larger synthetic ones written for this probe (a skinned,
// lit mesh and an eight-layer terrain with decals). Real FA shaders need M6b's HLSL emitter (the
// glslang front end rejects most of FA's effects as they are, docs/port/renderer.md); their timing
// comes in a later release.
//
// Linked with the glslang and SPIRV-Tools static libraries that libfaf_android.so's own build made
// (buildstage/android-native*/DiligentCore/ThirdParty), so the numbers are those of the compiler the
// app runs. Built without them (build_runner.py found none), the section reports "unsupported".
// Compiled like those libraries: -fno-rtti -fno-exceptions.

#include "Probe.h"

#include <stdio.h>

#include <algorithm>

#if !defined(FAF_PROBE_NO_GLSLANG)
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv-tools/optimizer.hpp>
#endif

namespace faf_probe
{
#if defined(FAF_PROBE_NO_GLSLANG)

  SectionResult RunGlslang(const Options& /*options*/)
  {
    SectionResult result;
    Json j;
    j.Str("status", "unsupported");
    j.Bool("placeholder", true);
    j.Str("error", "this build of the probe has no glslang (build_runner.py found no glslang libraries)");
    result.json = j.Text();
    result.summary = "glslang timing not built in";
    return result;
  }

#else

  namespace
  {
    // --- The M1 renderer's splash shaders (port/android/src/Renderer.cpp, kConstantsHlsl + ...) ---

    constexpr char kM1Constants[] = R"(
cbuffer Constants
{
    float4 g_Rotation;
    float4 g_ImageRect;
    float4 g_Background;
    float4 g_BarRect;
    float4 g_BarFill;
    float4 g_BarColor;
    float4 g_TrackColor;
};

struct PSInput
{
    float4 Pos     : SV_POSITION;
    float2 Logical : TEX_COORD;
};
)";

    constexpr char kM1Vertex[] = R"(
void main(in float2 Ndc : ATTRIB0, out PSInput PSIn)
{
    PSIn.Pos     = float4(Ndc, 0.0, 1.0);
    PSIn.Logical = float2(dot(Ndc, g_Rotation.xy), dot(Ndc, g_Rotation.zw));
}
)";

    constexpr char kM1Pixel[] = R"(
Texture2D    g_Texture;
SamplerState g_Texture_sampler;

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main(in PSInput PSIn, out PSOutput PSOut)
{
    float2 L = PSIn.Logical;
    float4 Color = g_Background;
    if (g_ImageRect.z > 0.5)
    {
        float2 Rel = L / g_ImageRect.xy;
        if (abs(Rel.x) <= 1.0 && abs(Rel.y) <= 1.0)
        {
            float2 UV = float2(Rel.x * 0.5 + 0.5, 0.5 - Rel.y * 0.5);
            Color = g_Texture.SampleLevel(g_Texture_sampler, UV, 0.0);
        }
    }
    if (L.x >= g_BarRect.x && L.x <= g_BarRect.z && L.y >= g_BarRect.y && L.y <= g_BarRect.w)
    {
        float T = (L.x - g_BarRect.x) / (g_BarRect.z - g_BarRect.x);
        Color = (T >= g_BarFill.x && T <= g_BarFill.y) ? g_BarColor : g_TrackColor;
    }
    PSOut.Color = float4(Color.rgb, 1.0);
}
)";

    // --- Synthetic shader 1: a skinned, normal-mapped mesh with sun, shadows, point lights, ---
    // --- environment reflection, emissive, fog and tone mapping (written for this probe).  ---

    constexpr char kMeshCommon[] = R"(
cbuffer Frame
{
    float4x4 g_ViewProj;
    float4x4 g_ShadowMatrix;
    float4   g_CameraPos;
    float4   g_SunDir;
    float4   g_SunColor;
    float4   g_Ambient;
    float4   g_FogColor;
    float4   g_Time;
};

cbuffer Lights
{
    float4 g_LightPos[8];
    float4 g_LightColor[8];
    int4   g_LightCount;
};

cbuffer Object
{
    float4x4 g_World;
    float4x4 g_Bones[48];
    float4   g_TeamColor;
    float4   g_Material;
};

struct PSInput
{
    float4 Pos       : SV_POSITION;
    float3 WorldPos  : WORLD_POS;
    float3 Normal    : NORMAL0;
    float3 Tangent   : TANGENT0;
    float3 Bitangent : BINORMAL0;
    float4 UV        : TEX_COORD0;
    float4 ShadowPos : TEX_COORD1;
    float  Depth     : TEX_COORD2;
};
)";

    constexpr char kMeshVertex[] = R"(
struct VSInput
{
    float3 Pos     : ATTRIB0;
    float3 Normal  : ATTRIB1;
    float3 Tangent : ATTRIB2;
    float2 UV0     : ATTRIB3;
    float2 UV1     : ATTRIB4;
    uint4  Bones   : ATTRIB5;
    float4 Weights : ATTRIB6;
};

float4x4 SkinMatrix(uint4 bones, float4 weights)
{
    return g_Bones[bones.x] * weights.x + g_Bones[bones.y] * weights.y +
           g_Bones[bones.z] * weights.z + g_Bones[bones.w] * weights.w;
}

void main(in VSInput VSIn, out PSInput PSIn)
{
    float4x4 skin = SkinMatrix(VSIn.Bones, VSIn.Weights);
    float4x4 world = mul(skin, g_World);
    float4 worldPos = mul(float4(VSIn.Pos, 1.0), world);
    PSIn.Pos = mul(worldPos, g_ViewProj);
    PSIn.WorldPos = worldPos.xyz;
    float3x3 world3 = (float3x3)world;
    float3 n = normalize(mul(VSIn.Normal, world3));
    float3 t = normalize(mul(VSIn.Tangent, world3));
    t = normalize(t - n * dot(n, t));
    PSIn.Normal = n;
    PSIn.Tangent = t;
    PSIn.Bitangent = cross(n, t);
    float wave = sin(g_Time.x * 2.0 + worldPos.x * 0.1) * g_Material.x;
    PSIn.UV = float4(VSIn.UV0 + float2(wave, 0.0) * 0.01, VSIn.UV1);
    PSIn.ShadowPos = mul(worldPos, g_ShadowMatrix);
    PSIn.Depth = PSIn.Pos.w;
}
)";

    constexpr char kMeshPixel[] = R"(
Texture2D              g_Albedo;
Texture2D              g_NormalMap;
Texture2D              g_SpecularMap;
Texture2D              g_Emissive;
Texture2D              g_ShadowMap;
TextureCube            g_Environment;
SamplerState           g_Albedo_sampler;
SamplerState           g_NormalMap_sampler;
SamplerState           g_SpecularMap_sampler;
SamplerState           g_Emissive_sampler;
SamplerComparisonState g_ShadowMap_sampler;
SamplerState           g_Environment_sampler;

static const float2 kPoisson[12] =
{
    float2(-0.326, -0.406), float2(-0.840, -0.074), float2(-0.696,  0.457), float2(-0.203,  0.621),
    float2( 0.962, -0.195), float2( 0.473, -0.480), float2( 0.519,  0.767), float2( 0.185, -0.893),
    float2( 0.507,  0.064), float2( 0.896,  0.412), float2(-0.322, -0.933), float2(-0.792, -0.598)
};

float ShadowTerm(float4 shadowPos)
{
    float3 p = shadowPos.xyz / shadowPos.w;
    float2 uv = p.xy * float2(0.5, -0.5) + 0.5;
    float sum = 0.0;
    [unroll] for (int i = 0; i < 12; ++i)
    {
        sum += g_ShadowMap.SampleCmpLevelZero(g_ShadowMap_sampler, uv + kPoisson[i] * (1.0 / 2048.0), p.z - 0.0015);
    }
    return sum / 12.0;
}

float3 DecodeNormal(float4 texel, float3 n, float3 t, float3 b)
{
    float2 xy = texel.ag * 2.0 - 1.0;
    float z = sqrt(saturate(1.0 - dot(xy, xy)));
    return normalize(xy.x * t + xy.y * b + z * n);
}

float3 Fresnel(float3 f0, float cosTheta)
{
    return f0 + (1.0 - f0) * pow(1.0 - cosTheta, 5.0);
}

float3 PointLight(int i, float3 worldPos, float3 n, float3 v, float3 albedo, float3 spec, float power)
{
    float3 toLight = g_LightPos[i].xyz - worldPos;
    float dist = length(toLight);
    float3 l = toLight / max(dist, 0.0001);
    float atten = saturate(1.0 - dist / g_LightPos[i].w);
    atten *= atten;
    float ndl = saturate(dot(n, l));
    float3 h = normalize(l + v);
    float s = pow(saturate(dot(n, h)), power);
    return (albedo * ndl + spec * s) * g_LightColor[i].rgb * g_LightColor[i].w * atten;
}

float3 ToneMap(float3 c)
{
    c *= 1.2;
    return saturate((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14));
}

float4 main(in PSInput PSIn) : SV_TARGET
{
    float4 albedo = g_Albedo.Sample(g_Albedo_sampler, PSIn.UV.xy);
    clip(albedo.a - g_Material.w);
    float4 specTex = g_SpecularMap.Sample(g_SpecularMap_sampler, PSIn.UV.xy);
    albedo.rgb = lerp(albedo.rgb, albedo.rgb * g_TeamColor.rgb * 2.0, specTex.g);
    float3 n = DecodeNormal(g_NormalMap.Sample(g_NormalMap_sampler, PSIn.UV.xy), normalize(PSIn.Normal),
                            normalize(PSIn.Tangent), normalize(PSIn.Bitangent));
    float3 v = normalize(g_CameraPos.xyz - PSIn.WorldPos);
    float shadow = ShadowTerm(PSIn.ShadowPos);
    float ndl = saturate(dot(n, -g_SunDir.xyz));
    float3 h = normalize(-g_SunDir.xyz + v);
    float power = g_Material.y * specTex.a + 1.0;
    float3 spec = specTex.rgb * pow(saturate(dot(n, h)), power);
    float3 color = (albedo.rgb * ndl + spec) * g_SunColor.rgb * shadow + albedo.rgb * g_Ambient.rgb;
    [loop] for (int i = 0; i < g_LightCount.x; ++i)
    {
        color += PointLight(i, PSIn.WorldPos, n, v, albedo.rgb, specTex.rgb, power);
    }
    float3 r = reflect(-v, n);
    float3 env = g_Environment.SampleLevel(g_Environment_sampler, r, specTex.a * 6.0).rgb;
    color += env * Fresnel(specTex.rgb * g_Material.z, saturate(dot(n, v)));
    color += g_Emissive.Sample(g_Emissive_sampler, PSIn.UV.zw).rgb * g_Material.x;
    float fog = 1.0 - exp(-PSIn.Depth * g_FogColor.w);
    color = lerp(color, g_FogColor.rgb, fog);
    return float4(ToneMap(color), albedo.a);
}
)";

    // --- Synthetic shader 2: an eight-layer splatted terrain with per-layer normals, decals, ---
    // --- shadows and a water tint (written for this probe).                                 ---

    constexpr char kTerrainPixel[] = R"(
cbuffer Terrain
{
    float4x4 g_ViewProj;
    float4x4 g_ShadowMatrix;
    float4   g_CameraPos;
    float4   g_SunDir;
    float4   g_SunColor;
    float4   g_Ambient;
    float4   g_LayerScale[8];
    float4   g_LayerTint[8];
    float4   g_Water;
    float4   g_WaterColor;
    float4   g_DecalRect[16];
    float4   g_DecalParams[16];
    int4     g_DecalCount;
};

Texture2D              g_Mask0;
Texture2D              g_Mask1;
Texture2DArray         g_Layers;
Texture2DArray         g_LayerNormals;
Texture2D              g_HeightNormal;
Texture2D              g_ShadowMap;
Texture2DArray         g_DecalAtlas;
SamplerState           g_Mask0_sampler;
SamplerState           g_Mask1_sampler;
SamplerState           g_Layers_sampler;
SamplerState           g_LayerNormals_sampler;
SamplerState           g_HeightNormal_sampler;
SamplerComparisonState g_ShadowMap_sampler;
SamplerState           g_DecalAtlas_sampler;

struct PSInput
{
    float4 Pos       : SV_POSITION;
    float3 WorldPos  : WORLD_POS;
    float2 UV        : TEX_COORD0;
    float4 ShadowPos : TEX_COORD1;
};

float Shadow(float4 shadowPos)
{
    float3 p = shadowPos.xyz / shadowPos.w;
    float2 uv = p.xy * float2(0.5, -0.5) + 0.5;
    float sum = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            sum += g_ShadowMap.SampleCmpLevelZero(g_ShadowMap_sampler, uv + float2(x, y) * (1.0 / 4096.0), p.z - 0.001);
        }
    }
    return sum / 9.0;
}

float3 BlendNormal(float3 base, float3 detail)
{
    base += float3(0.0, 0.0, 1.0);
    detail *= float3(-1.0, -1.0, 1.0);
    return normalize(base * dot(base, detail) / base.z - detail);
}

float4 main(in PSInput PSIn) : SV_TARGET
{
    float4 m0 = g_Mask0.Sample(g_Mask0_sampler, PSIn.UV);
    float4 m1 = g_Mask1.Sample(g_Mask1_sampler, PSIn.UV);
    float weights[8];
    weights[0] = 1.0;
    weights[1] = m0.r;
    weights[2] = m0.g;
    weights[3] = m0.b;
    weights[4] = m0.a;
    weights[5] = m1.r;
    weights[6] = m1.g;
    weights[7] = m1.b;
    float3 albedo = float3(0.0, 0.0, 0.0);
    float3 detail = float3(0.0, 0.0, 0.0);
    float total = 0.0;
    [unroll] for (int i = 0; i < 8; ++i)
    {
        float2 luv = PSIn.WorldPos.xz * g_LayerScale[i].x;
        float4 c = g_Layers.Sample(g_Layers_sampler, float3(luv, i));
        float4 nn = g_LayerNormals.Sample(g_LayerNormals_sampler, float3(luv, i));
        float w = weights[i] * (0.5 + c.a);
        for (int j = i + 1; j < 8; ++j)
        {
            w *= 1.0 - weights[j] * 0.5;
        }
        albedo += c.rgb * g_LayerTint[i].rgb * w;
        detail += (nn.xyz * 2.0 - 1.0) * w;
        total += w;
    }
    albedo /= max(total, 0.0001);
    detail = normalize(detail);
    float3 baseNormal = g_HeightNormal.Sample(g_HeightNormal_sampler, PSIn.UV).xzy * 2.0 - 1.0;
    float3 n = BlendNormal(baseNormal, detail);

    [loop] for (int d = 0; d < g_DecalCount.x; ++d)
    {
        float2 rel = (PSIn.WorldPos.xz - g_DecalRect[d].xy) / (g_DecalRect[d].zw - g_DecalRect[d].xy);
        if (all(rel >= 0.0) && all(rel <= 1.0))
        {
            float s, c;
            sincos(g_DecalParams[d].x, s, c);
            float2 centred = rel - 0.5;
            float2 ruv = float2(dot(centred, float2(c, -s)), dot(centred, float2(s, c))) + 0.5;
            float4 dc = g_DecalAtlas.SampleLevel(g_DecalAtlas_sampler, float3(ruv, g_DecalParams[d].z), 0.0);
            albedo = lerp(albedo, dc.rgb, dc.a * g_DecalParams[d].y);
            n = normalize(lerp(n, float3(0.0, 1.0, 0.0), dc.a * g_DecalParams[d].w));
        }
    }

    float shadow = Shadow(PSIn.ShadowPos);
    float ndl = saturate(dot(n, -g_SunDir.xyz));
    float3 v = normalize(g_CameraPos.xyz - PSIn.WorldPos);
    float3 h = normalize(v - g_SunDir.xyz);
    float spec = pow(saturate(dot(n, h)), 24.0) * 0.15;
    float3 color = albedo * (g_SunColor.rgb * ndl * shadow + g_Ambient.rgb) + spec * g_SunColor.rgb * shadow;

    float depth = g_Water.x - PSIn.WorldPos.y;
    if (depth > 0.0)
    {
        float t = saturate(depth * g_Water.y);
        color = lerp(color, g_WaterColor.rgb * (g_Ambient.rgb + ndl * g_SunColor.rgb), t);
    }
    float shore = saturate(1.0 - abs(depth) / max(g_Water.z, 0.0001));
    color = lerp(color, color * 1.15 + 0.05, shore * 0.3);
    return float4(color, 1.0);
}
)";

    struct ShaderSource
    {
      const char* name;
      EShLanguage stage;
      std::string text;
    };

    std::vector<ShaderSource> Shaders()
    {
      return {
        {"m1_splash_vs", EShLangVertex, std::string(kM1Constants) + kM1Vertex},
        {"m1_splash_ps", EShLangFragment, std::string(kM1Constants) + kM1Pixel},
        {"synthetic_mesh_vs", EShLangVertex, std::string(kMeshCommon) + kMeshVertex},
        {"synthetic_mesh_ps", EShLangFragment, std::string(kMeshCommon) + kMeshPixel},
        {"synthetic_terrain_ps", EShLangFragment, std::string(kTerrainPixel)},
      };
    }

    struct Timing
    {
      bool ok = false;
      std::string error;
      double parseMs = 0.0;
      double linkMs = 0.0;
      double spirvMs = 0.0;
      double optimizeMs = 0.0;
      size_t spirvWords = 0;
      size_t optimizedWords = 0;
    };

    // One compile as Diligent's HLSLtoSPIRV does it (Vulkan 1.0, SPIR-V 1.0).
    Timing Compile(const ShaderSource& source)
    {
      Timing t;
      const EShMessages messages =
        static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl | EShMsgHlslLegalization);
      double t0 = NowMs();
      glslang::TShader shader(source.stage);
      const char* text = source.text.c_str();
      const int length = static_cast<int>(source.text.size());
      shader.setStringsWithLengths(&text, &length, 1);
      shader.setPreamble("#define GLSLANG\n\n");
      shader.setEnvInput(glslang::EShSourceHlsl, source.stage, glslang::EShClientVulkan, 100);
      shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
      shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
      shader.setHlslIoMapping(true);
      shader.setEntryPoint("main");
      shader.setEnvTargetHlslFunctionality1();
      shader.setAutoMapBindings(true);
      shader.setAutoMapLocations(true);
      if (!shader.parse(GetDefaultResources(), 100, ENoProfile, false, false, messages)) {
        t.error = std::string("parse: ") + shader.getInfoLog();
        return t;
      }
      double t1 = NowMs();
      t.parseMs = t1 - t0;
      glslang::TProgram program;
      program.addShader(&shader);
      if (!program.link(messages)) {
        t.error = std::string("link: ") + program.getInfoLog();
        return t;
      }
      program.mapIO();
      double t2 = NowMs();
      t.linkMs = t2 - t1;
      std::vector<unsigned int> spirv;
      glslang::GlslangToSpv(*program.getIntermediate(source.stage), spirv);
      double t3 = NowMs();
      t.spirvMs = t3 - t2;
      t.spirvWords = spirv.size();

      spvtools::Optimizer optimizer(SPV_ENV_VULKAN_1_0);
      std::string optimizerLog;
      optimizer.SetMessageConsumer([&optimizerLog](spv_message_level_t level, const char*, const spv_position_t&,
                                                   const char* message) {
        if (level <= SPV_MSG_ERROR && optimizerLog.size() < 300) {
          optimizerLog += message;
          optimizerLog += "; ";
        }
      });
      spvtools::OptimizerOptions options;
      options.set_run_validator(false);
      optimizer.RegisterLegalizationPasses();
      spvtools::ValidatorOptions validatorOptions;
      validatorOptions.SetBeforeHlslLegalization(true);
      options.set_validator_options(validatorOptions);
      optimizer.RegisterPerformancePasses();
      std::vector<uint32_t> optimized;
      const bool optimizedOk = optimizer.Run(spirv.data(), spirv.size(), &optimized, options);
      t.optimizeMs = NowMs() - t3;
      if (!optimizedOk) {
        t.error = "spirv-opt: " + optimizerLog;
        return t;
      }
      t.optimizedWords = optimized.size();
      t.ok = true;
      return t;
    }

    double Median(std::vector<double> v)
    {
      if (v.empty()) {
        return -1.0;
      }
      std::sort(v.begin(), v.end());
      return v.size() % 2 == 1 ? v[v.size() / 2] : (v[v.size() / 2 - 1] + v[v.size() / 2]) * 0.5;
    }
  } // namespace

  SectionResult RunGlslang(const Options& options)
  {
    SectionResult result;
    Json j;
    const double t0 = NowMs();
    const int repeats = options.glslangRepeats < 1 ? 1 : options.glslangRepeats;
    const double initStart = NowMs();
    glslang::InitializeProcess();
    const double initMs = NowMs() - initStart;
    const glslang::Version version = glslang::GetVersion();

    Json shaders('[');
    double coldTotal = 0.0;
    double warmTotal = 0.0;
    int failed = 0;
    std::string firstError;
    for (const ShaderSource& source : Shaders()) {
      std::vector<double> warm;
      Timing cold;
      Timing last;
      for (int i = 0; i < repeats; ++i) {
        Timing t = Compile(source);
        const double total = t.parseMs + t.linkMs + t.spirvMs + t.optimizeMs;
        if (i == 0) {
          cold = t;
        } else {
          warm.push_back(total);
        }
        last = t;
        if (!t.ok) {
          break;
        }
      }
      Json s;
      s.Str("name", source.name);
      s.Str("stage", source.stage == EShLangVertex ? "vertex" : "pixel");
      s.Num("source_bytes", static_cast<long long>(source.text.size()));
      s.Bool("ok", last.ok);
      if (!last.ok) {
        ++failed;
        s.Str("error", last.error.substr(0, 600));
        if (firstError.empty()) {
          firstError = std::string(source.name) + ": " + last.error.substr(0, 200);
        }
      }
      const double coldMs = cold.parseMs + cold.linkMs + cold.spirvMs + cold.optimizeMs;
      Json c;
      c.Real("parse_ms", cold.parseMs, 2);
      c.Real("link_ms", cold.linkMs, 2);
      c.Real("spirv_ms", cold.spirvMs, 2);
      c.Real("optimize_ms", cold.optimizeMs, 2);
      c.Real("total_ms", coldMs, 2);
      s.Raw("first", c.Text());
      const double warmMs = Median(warm);
      s.Real("warm_median_ms", warmMs, 2);
      s.Num("warm_runs", static_cast<long long>(warm.size()));
      s.Num("spirv_words", static_cast<long long>(last.spirvWords));
      s.Num("optimized_words", static_cast<long long>(last.optimizedWords));
      shaders.Add(s.Text());
      coldTotal += coldMs;
      warmTotal += warmMs > 0.0 ? warmMs : 0.0;
    }
    glslang::FinalizeProcess();

    j.Str("status", failed == 0 ? "ok" : "error");
    j.Bool("placeholder", true);
    j.Str("note",
          "Placeholder shader set: the M1 splash shaders and two synthetic shaders, no FA shaders. Real FA shader "
          "timing needs M6b's HLSL emitter and comes in a later release.");
    j.Str("pipeline", "HLSL -> glslang (Vulkan 1.0, SPIR-V 1.0) -> SPIRV-Tools legalization + performance passes, as "
                      "Diligent's HLSLtoSPIRV");
    j.Str("glslang_version", std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
                               std::to_string(version.patch) + (version.flavor != nullptr && version.flavor[0] != '\0'
                                                                    ? std::string("-") + version.flavor
                                                                    : std::string()));
    j.Real("initialize_ms", initMs, 2);
    j.Num("repeats", repeats);
    j.Real("first_total_ms", coldTotal, 1);
    j.Real("warm_total_ms", warmTotal, 1);
    if (!firstError.empty()) {
      j.Str("error", firstError);
    }
    j.Raw("shaders", shaders.Text());
    j.Real("ms", NowMs() - t0, 1);
    result.json = j.Text();
    char line[512];
    snprintf(line, sizeof(line),
             "glslang %d.%d.%d (placeholder shader set, 5 shaders): init %.0f ms, first compile of all %.0f ms, warm %.0f ms%s%s",
             version.major, version.minor, version.patch, initMs, coldTotal, warmTotal, failed == 0 ? "" : "; FAILED: ",
             firstError.c_str());
    result.summary = line;
    return result;
  }

#endif
} // namespace faf_probe
