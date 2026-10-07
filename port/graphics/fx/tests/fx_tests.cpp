// Unit tests for the effect front end. Each expectation that concerns D3DX
// behaviour was measured with d3dx9_43 + the legacy compiler (see README.md);
// the gate (scripts/port/fx_metadata_gate.py) checks the same against D3DX
// on the game's effects and on tests/effects/*.fx.

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "gpg/gal/fx/FxJson.h"
#include "gpg/gal/fx/FxLexer.h"
#include "gpg/gal/fx/FxMetadata.h"
#include "gpg/gal/fx/FxPreprocessor.h"

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

  template <class A, class B>
  void CheckEqual(const A& actual, const B& expected, const char* what, const int line)
  {
    ++gChecks;
    if (!(actual == expected)) {
      ++gFailures;
      std::printf("FAILED (line %d): %s\n", line, what);
    }
  }

#define CHECK_EQ(actual, expected) CheckEqual((actual), (expected), #actual " == " #expected, __LINE__)

  std::string Pp(const std::string& text, const std::vector<MacroDefinition>& macros = {})
  {
    std::vector<Diagnostic> diagnostics;
    const std::vector<Token> lexed = LexText(text, diagnostics);
    const std::vector<Token> tokens = Preprocess(lexed, macros, diagnostics);
    std::string out;
    for (const Token& token : tokens) {
      if (token.kind == TokenKind::End) {
        break;
      }
      if (!out.empty()) {
        out += ' ';
      }
      out += token.text;
    }
    for (const Diagnostic& diagnostic : diagnostics) {
      if (diagnostic.severity == Diagnostic::Severity::Error) {
        out += " <error: " + diagnostic.message + ">";
      }
    }
    return out;
  }

  FrontEndResult Meta(const std::string& text)
  {
    EffectInput input;
    input.parts.emplace_back("test.fx", text);
    return BuildEffectMetadata(input);
  }

  std::string Hex(const std::vector<std::uint32_t>& words)
  {
    return WordsToHex(words);
  }

  const ParameterInfo* FindParameter(const EffectMetadata& metadata, const char* name)
  {
    for (const ParameterInfo& parameter : metadata.parameters) {
      if (parameter.desc.name == name) {
        return &parameter;
      }
    }
    return nullptr;
  }

  void TestLexer()
  {
    std::vector<Diagnostic> diagnostics;
    const std::vector<Token> tokens = LexText("a/*x\ny*/b // c\n#define X \\\n 1\n.5e-3f 0x7F 1.5h", diagnostics);
    std::vector<std::string> texts;
    for (const Token& token : tokens) {
      texts.push_back(token.kind == TokenKind::Newline ? "\\n" : token.text);
    }
    const std::vector<std::string> expected = {"a", "b", "\\n", "#", "define", "X", "1", "\\n",
                                               ".5e-3f", "0x7F", "1.5h", "\\n", ""};
    CHECK(texts == expected);
    CHECK(diagnostics.empty());
    // The spliced line keeps counting physical lines.
    CHECK_EQ(tokens[6].where.line, 4U);
  }

  void TestPreprocessor()
  {
    CHECK_EQ(Pp("#define A 1\nA"), "1");
    CHECK_EQ(Pp("#define F(x) x+x\nF(2)"), "2 + 2");
    // terrain.fx DECLARE_STRATUM: ## with a parameter on either side.
    CHECK_EQ(Pp("#define D(n) float4 n##Tile; Texture = <n##Texture>;\nD(Lower)"),
             "float4 LowerTile ; Texture = < LowerTexture > ;");
    CHECK_EQ(Pp("#define S(x) #x\nS(a  b)"), "\"a b\"");
    // A macro does not expand inside its own expansion.
    CHECK_EQ(Pp("#define X X+1\nX"), "X + 1");
    CHECK_EQ(Pp("#define f(a) a*g\n#define g(a) f(a)\nf(2)(9)"), "2 * 9 * g");
    CHECK_EQ(Pp("#if 0\na\n#elif defined(B) || 1+1 == 2\nb\n#else\nc\n#endif"), "b");
    CHECK_EQ(Pp("#ifdef DIRECT3D10\na\n#else\nb\n#endif\n#ifndef DIRECT3D10\nc\n#endif"), "b c");
    CHECK_EQ(Pp("#if X == 2\nyes\n#endif", {{"X", "2"}}), "yes");
    // d3d9states.compat: a comment in a macro body is whitespace.
    CHECK_EQ(Pp("#define CompatSwizzle(pixel) /* nop */\nCompatSwizzle(Color);"), ";");
    // An object-like macro whose body is a list of state assignments.
    CHECK_EQ(Pp("#define AB \\\n\tA = 1; \\\n\tB = 2; \\\n\t\nx AB y"), "x A = 1 ; B = 2 ; y");
    CHECK(Pp("#include \"x.fxh\"").find("<error:") != std::string::npos);
  }

  void TestParameters()
  {
    const FrontEndResult result = Meta(R"(
      float4x4 M = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
      column_major float2x3 N;
      float4 V = 0.5;
      float1 F1 = 4;
      bool B = 2;
      int I = 2.7;
      float D = 7/2;
      float E = 10 % 3.5;
      int W = 2147483647 + 1;
      float Arr[] = { -3, -2, -1, 0, 1, 2, 3, };
      struct S { float2 a; float1 b; int c; bool d; };
      S Ss[2] = { 1,2,3,4,5,6, 7,8,9,10 };
      shared float Sh;
      static float NotAParameter = 1;
      const float PI = 3.14159265359;
      string Str = "hello";
      texture T < string ResourceName = "a.dds"; int n = 1 + 2; >;
      sampler2D Samp = sampler_state { Texture = <T>; MipFilter = LINEAR; AddressU = Clamp; BorderColor = float4(1,0.5,0,1); MaxAnisotropy = 4.7; };
      float4 PS() : COLOR { return 0; }
      technique T0 { pass P0 { PixelShader = compile ps_2_0 PS(); } }
    )");
    CHECK(result.ok);
    const EffectMetadata& m = result.metadata;
    CHECK_EQ(m.parameters.size(), 16U);
    CHECK(FindParameter(m, "NotAParameter") == nullptr);

    const ParameterInfo* p = FindParameter(m, "M");
    CHECK(p != nullptr && p->desc.parameterClass == ParameterClass::MatrixRows && p->desc.bytes == 64U);
    CHECK(p != nullptr && p->value.size() == 16 && p->value[1] == 0x40000000U); // row-major as written
    p = FindParameter(m, "N");
    CHECK(p != nullptr && p->desc.parameterClass == ParameterClass::MatrixRows && p->desc.rows == 2U);
    p = FindParameter(m, "V");
    CHECK(p != nullptr && Hex(p->value) == "3f000000 3f000000 3f000000 3f000000");
    p = FindParameter(m, "F1");
    CHECK(p != nullptr && p->desc.parameterClass == ParameterClass::Vector && p->desc.columns == 1U);
    p = FindParameter(m, "B");
    CHECK(p != nullptr && Hex(p->value) == "00000001");
    p = FindParameter(m, "I");
    CHECK(p != nullptr && Hex(p->value) == "00000002");
    p = FindParameter(m, "D");
    CHECK(p != nullptr && Hex(p->value) == "40400000"); // 3.0: integer division
    p = FindParameter(m, "E");
    CHECK(p != nullptr && Hex(p->value) == "40400000"); // fmod
    p = FindParameter(m, "W");
    CHECK(p != nullptr && Hex(p->value) == "80000000");
    p = FindParameter(m, "Arr");
    CHECK(p != nullptr && p->desc.elements == 7U && p->desc.bytes == 28U);
    p = FindParameter(m, "Ss");
    CHECK(p != nullptr && p->desc.parameterClass == ParameterClass::Struct && p->desc.bytes == 40U &&
          p->desc.structMembers == 4U && p->members.size() == 4U);
    CHECK(p != nullptr && Hex(p->value) ==
                            "3f800000 40000000 40400000 00000004 00000001 40c00000 40e00000 41000000 00000009 00000001");
    p = FindParameter(m, "Sh");
    CHECK(p != nullptr && p->desc.flags == kParameterShared);
    p = FindParameter(m, "Str");
    CHECK(p != nullptr && p->string == "hello" && p->desc.bytes == 4U);
    p = FindParameter(m, "T");
    CHECK(p != nullptr && p->annotations.size() == 2U && p->annotations[0].string == "a.dds" &&
          Hex(p->annotations[1].value) == "00000003");
    p = FindParameter(m, "Samp");
    CHECK(p != nullptr && p->desc.bytes == 0U && p->samplerTexture == "T" && p->samplerStates.size() == 4U);
    if (p != nullptr && p->samplerStates.size() == 4U) {
      CHECK_EQ(p->samplerStates[0].state, 7U); // D3DSAMP_MIPFILTER
      CHECK_EQ(p->samplerStates[0].value, 2U); // D3DTEXF_LINEAR
      CHECK_EQ(p->samplerStates[1].value, 3U); // D3DTADDRESS_CLAMP
      CHECK_EQ(p->samplerStates[2].value, 0xFFFF8000U);
      CHECK_EQ(p->samplerStates[3].value, 4U);
    }
  }

  void TestTechniques()
  {
    const FrontEndResult result = Meta(R"(
      #define STAGE_DEPTH 0x01
      #define STAGE_REFLECTION 0x02
      float P2 = 0.125;
      float4 PS(uniform bool b, uniform int f) : COLOR { return 0; }
      float4 VS() : POSITION { return 0; }
      technique Flat < int renderStage = STAGE_DEPTH + STAGE_REFLECTION; string depthTechnique = "DepthClip"; >
      {
        pass P0
        {
          AlphaBlendEnable = false; ColorWriteEnable = RED|GREEN; CullMode = cw; ZFunc = LessEqual;
          AlphaRef = 0.5; DepthBias = -0.02; FogStart = (P2); StencilFail = replace; FogColor = float4(1,0,0,1);
          VertexShader = compile vs_1_1 VS();
          PixelShader = compile ps_2_a PS(true, 3);
        }
        pass { VertexShader = null; }
      }
      technique { pass { PixelShader = compile ps_2_0 PS(false, 0); } }
    )");
    CHECK(result.ok);
    const EffectMetadata& m = result.metadata;
    CHECK_EQ(m.techniques.size(), 2U);
    if (m.techniques.size() != 2U) {
      return;
    }
    const TechniqueInfo& flat = m.techniques[0];
    CHECK_EQ(flat.name, "Flat");
    CHECK(flat.annotations.size() == 2U && Hex(flat.annotations[0].value) == "00000003");
    CHECK(flat.annotations.size() == 2U && flat.annotations[1].string == "DepthClip");
    CHECK_EQ(flat.passes.size(), 2U);
    const PassInfo& p0 = flat.passes[0];
    CHECK_EQ(p0.states.size(), 9U);
    if (p0.states.size() == 9U) {
      CHECK(p0.states[0].state == 27U && p0.states[0].value == 0U);
      CHECK(p0.states[1].state == 168U && p0.states[1].value == 3U);
      CHECK(p0.states[2].state == 22U && p0.states[2].value == 2U);
      CHECK(p0.states[3].state == 23U && p0.states[3].value == 4U);
      CHECK(p0.states[4].state == 24U && p0.states[4].value == 0U); // AlphaRef = 0.5 truncates
      CHECK(p0.states[5].state == 195U && p0.states[5].value == 0xBCA3D70AU);
      CHECK(p0.states[6].state == 36U && p0.states[6].value == 0x3E000000U && p0.states[6].dynamic);
      CHECK(p0.states[7].state == 53U && p0.states[7].value == 3U);
      CHECK(p0.states[8].state == 34U && p0.states[8].value == 0xFFFF0000U);
    }
    CHECK(p0.vertexShader.kind == ShaderEntry::Kind::Compile && p0.vertexShader.entry == "VS" &&
          p0.vertexShader.versionToken == 0xFFFE0101U);
    CHECK(p0.pixelShader.kind == ShaderEntry::Kind::Compile && p0.pixelShader.versionToken == 0xFFFF0201U &&
          p0.pixelShader.arguments == std::vector<std::string>({"true", "3"}));
    CHECK(flat.passes[1].vertexShader.kind == ShaderEntry::Kind::Null);
    CHECK(flat.passes[1].pixelShader.kind == ShaderEntry::Kind::Unassigned);
    CHECK(!m.techniques[1].named && !m.techniques[1].passes[0].named);

    const std::vector<DeviceProfile>& profiles = StandardDeviceProfiles();
    CHECK(IsTechniqueValid(flat, profiles[0]));
    CHECK(!IsTechniqueValid(flat, profiles[1])); // ps_2_a is 2.1
    CHECK(IsTechniqueValid(m.techniques[1], profiles[1]));
    CHECK(!IsTechniqueValid(m.techniques[1], profiles[2]));
  }

  // Behaviour of the legacy compiler / d3dx9_43 that the front end mirrors,
  // each measured against D3DX (see README.md).
  void TestD3DXQuirks()
  {
    const FrontEndResult result = Meta(R"(
      static const float SA = 16777217.0;
      float DoubleFold = 16777216.0 + 1.0 - 16777216.0;
      float NotEqual = 0.1 + 0.2 == 0.3;
      float StaticRef = SA - 16777216.0;
      float3 Sun = normalize(float3(1.2, 0.7, 0.5));
      float3 Length = length(float3(-3, 4, 12));
      float Distance = distance(float2(0, 0), float2(3, 4));
      float Round[2] = { round(2.5), round(-2.5) };
      float Radians = radians(-1.6);
      technique T < int n = 1; >
      {
        pass P0
        {
          StencilFail = Zero; CCW_StencilFail = Keep;
          BlendOp = Add; BlendOpAlpha = Max;
          ZEnable = true; CullMode = None; ZEnable = false;
        }
      }
    )");
    CHECK(result.ok);
    const EffectMetadata& m = result.metadata;
    const ParameterInfo* p = FindParameter(m, "DoubleFold");
    CHECK(p != nullptr && Hex(p->value) == "3f800000"); // double precision: 1, not 0
    p = FindParameter(m, "NotEqual");
    CHECK(p != nullptr && Hex(p->value) == "00000000");
    p = FindParameter(m, "StaticRef");
    CHECK(p != nullptr && Hex(p->value) == "3f800000"); // the referenced static is not rounded to float
    p = FindParameter(m, "Sun");
    CHECK(p != nullptr && Hex(p->value) == "3f500fe5 3ef2bd36 3ead6294"); // water2.fx SunColor
    p = FindParameter(m, "Length");
    CHECK(p != nullptr && Hex(p->value) == "40400000 40400000 40400000"); // |x|, broadcast
    p = FindParameter(m, "Distance");
    CHECK(p != nullptr && Hex(p->value) == "40400000");
    p = FindParameter(m, "Round");
    CHECK(p != nullptr && Hex(p->value) == "40400000 c0000000"); // floor(x + 0.5)
    p = FindParameter(m, "Radians");
    CHECK(p != nullptr && Hex(p->value) == "bce4c389"); // pi rounded to float
    CHECK(!m.techniques.empty() && m.techniques[0].annotations.size() == 1 &&
          m.techniques[0].annotations[0].desc.flags == kParameterAnnotation);
    if (!m.techniques.empty() && !m.techniques[0].passes.empty()) {
      const std::vector<PassState>& states = m.techniques[0].passes[0].states;
      CHECK_EQ(states.size(), 6U);
      if (states.size() == 6U) {
        CHECK(states[0].state == 53U && states[0].value == 2U);
        CHECK(states[1].state == 53U && states[1].value == 1U); // CCW_StencilFail sets D3DRS_STENCILFAIL
        CHECK(states[2].state == 171U && states[2].value == 1U);
        CHECK(states[3].state == 171U && states[3].value == 5U); // BlendOpAlpha sets D3DRS_BLENDOP
        CHECK(states[4].state == 22U); // the first ZEnable is replaced...
        CHECK(states[5].state == 7U && states[5].value == 0U); // ...by the second, at its position
      }
    }
    // Names D3DX does not accept for a state are errors, as in D3DX.
    CHECK(!Meta("technique T { pass P { DestBlendAlpha = DestColor; } }").ok);
    CHECK(!Meta("float x = sqrt(-1);").ok);
  }

  void TestErrors()
  {
    CHECK(!Meta("float x = ;").ok);
    CHECK(!Meta("technique T { pass P { NoSuchState = 1; } }").ok);
    CHECK(!Meta("technique T { pass P { PixelShader = compile ps_2_0 Missing(); } }").ok);
    const FrontEndResult result = Meta("\n\nfloat y = undefinedName;");
    CHECK(!result.ok && !result.diagnostics.empty() && result.diagnostics[0].where.line == 3U);
  }

} // namespace

int main()
{
  TestLexer();
  TestPreprocessor();
  TestParameters();
  TestTechniques();
  TestD3DXQuirks();
  TestErrors();
  std::printf("%d checks, %d failed\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
