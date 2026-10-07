// Effect grammar and parameter reflection, checked against D3DX by
// scripts/port/fx_metadata_gate.py: parameter types, modifiers, semantics,
// annotations, structs, typedefs, arrays, object types, technique and pass
// annotations, unnamed techniques and passes, and the preprocessor.

#define CONCAT(a, b) a##b
#define STRINGIZE(x) #x
#define TWICE(x) ((x) + (x))
#define DECLARE_PAIR(name, type) \
    type name##First;            \
    type name##Second = TWICE(1.5);
#define EMPTY
#define VERSION 3

#if VERSION > 2 && defined(CONCAT)
float VersionIsThree = VERSION;
#elif VERSION == 2
float VersionIsTwo;
#else
float VersionIsOld;
#endif

#ifdef UNDEFINED_NAME
float NeverDeclared;
#endif

#undef VERSION
#ifndef VERSION
float VersionUndefined = 1;
#endif

#if 0
this text is skipped entirely, including unbalanced ( { [ tokens
#endif

DECLARE_PAIR(Pair, float)
DECLARE_PAIR(IntPair, int)
float CONCAT(Pasted, Name) = 2;
string Stringized = STRINGIZE(hello world);
EMPTY float AfterEmpty = 3;

typedef float4 color_t;
typedef color_t tint_t;

struct Light
{
    float3 direction;
    float1 intensity;
    color_t color : COLOR0;
    int2 flags;
    bool enabled;
    float2x2 transform;
};

struct Pair2 { float a, b; };

float4x4 WorldViewProjection : WORLDVIEWPROJECTION;
row_major float3x4 RowMajor;
column_major float4x3 ColumnMajor;
matrix Plain;
matrix<float, 2, 2> Templated = { 1, 2, 3, 4 };
vector Vector4;
vector<int, 3> IntTemplate = { 1, 2, 3 };
float1x1 Tiny = 5;
float1 OneVector;
half4 Halves = { 0.5, 0.25, 0.125, 0.0625 };
double Doubled = 2.5;
dword Dword = 9;
bool4 Flags = { true, false, 1, 0 };
int Integer = -3;
tint_t Tint = { 0.1, 0.2, 0.3, 0.4 };

shared float SharedValue;
uniform float UniformValue = 1;
extern float ExternValue = 2;
const float ConstValue = 3;
static float StaticValue = 4;
static const float StaticConst = 5;

Light Lights[2] =
{
    { 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, true, 1, 0, 0, 1 },
    { 1, 0, 0, 0.5, 0, 0, 0, 1, 2, 3, false, 0, 1, 1, 0 },
};
Light OneLight;
Pair2 Pairs[3];

float Values[4] = { 1, 2, 3, 4 };
float4 Colors[] = { float4(1, 0, 0, 1), float4(0, 1, 0, 1) };
float3x3 Rotations[2];

string Name = "grammar" "joined";
string Escapes = "tab\tquote\"backslash\\";

texture BaseTexture
<
    string ResourceName = "base.dds";
    string ResourceType = "2D";
    int Levels = 4;
    float Scale = 0.5 * 3;
    bool Linear = true;
    float4 Border = { 1, 0.5, 0.25, 0 };
    int2 Size = { 512, 256 };
>;
texture1D Ramp;
texture2D Plane : DIFFUSE;
texture3D Volume;
textureCUBE Cube;
Texture Uppercase;

sampler PlainSampler;
sampler1D RampSampler = sampler_state { Texture = (Ramp); };
sampler2D PlaneSampler = sampler_state { Texture = <Plane>; MinFilter = Linear; };
sampler3D VolumeSampler = sampler_state { Texture = (Volume); AddressW = Mirror; };
samplerCUBE CubeSampler = sampler_state { Texture = <Cube>; MagFilter = Anisotropic; MaxAnisotropy = 8; };

float Annotated < string UIName = "Annotated value"; float UIMin = -1; float UIMax = 1; > = 0.25;
float4 SemanticAndAnnotation : POSITION < int Index = 7; > = { 1, 2, 3, 4 };

float4 VS(float4 position : POSITION) : POSITION
{
    // A body the parser skips: braces { } in comments, "strings }" too.
    return mul(position, WorldViewProjection);
}

float4 PS(float2 uv : TEXCOORD0, uniform float scale, uniform bool flip) : COLOR
{
    float4 c = tex2D(PlaneSampler, uv) * scale;
    if (flip) { c = c.bgra; }
    return c;
}

float4 PSPlain() : COLOR0;

float4 PSPlain() : COLOR0
{
    return 1;
}

// Shader parameters, and a pass that sets them; an asm block.
float4 PSConstant(uniform float k) : COLOR { return k; }
VertexShader SharedVS = compile vs_1_1 VS();
PixelShader PixelShaders[2] = { compile ps_2_0 PSConstant(0.25), compile ps_2_0 PSConstant(0.75) };

technique ShaderParameters
{
    pass P0 { VertexShader = (SharedVS); PixelShader = (PixelShaders[1]); }
    pass P1
    {
        VertexShader = <SharedVS>;
        PixelShader = asm
        {
            ps_1_1
            def c0, 1, 0, 0, 1
            mov r0, c0
        };
    }
}

technique Annotated
<
    string Group = "first";
    int Order = 1 + 2 * 3;
    float Weight = 0.75;
    bool Enabled = false;
    float3 Axis = { 0, 1, 0 };
>
{
    pass First < string Note = "pass annotation"; int Count = 2; >
    {
        VertexShader = compile vs_1_1 VS();
        PixelShader = compile ps_2_0 PS(1.5, true);
    }
    pass
    {
        PixelShader = compile ps_2_0 PSPlain();
    }
}

technique
{
    pass Only
    {
        VertexShader = compile vs_2_0 VS();
        PixelShader = compile ps_2_0 PS(-2, false);
    }
}

Technique Capitalized
{
    Pass P0 { VertexShader = null; PixelShader = NULL; }
}
