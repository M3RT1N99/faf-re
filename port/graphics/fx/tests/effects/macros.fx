// gate-macros: FAF_BONE_TEXTURE=1 SCALE=2.5 NAME=Bones
//
// Macros given by the caller (EffectContext::mMacros, D3DXMACRO), checked
// against D3DX by scripts/port/fx_metadata_gate.py, which passes the macros
// named on the first line to both tools. FAF_BONE_TEXTURE is the one the
// engine adds (CD3DEffectTechnique.cpp:491-513); the D3DX dumper then also
// compiles with D3DXSHADER_AVOID_FLOW_CONTROL, as D3D9Interfaces.cpp:1544-1545
// does.

#ifdef FAF_BONE_TEXTURE
float BoneTexture = FAF_BONE_TEXTURE;
#else
float NoBoneTexture = 0;
#endif

#if defined(SCALE) && !defined(UNSET)
float Scaled = SCALE * 2;
#endif

#define QUOTE(x) #x
#define EXPAND_QUOTE(x) QUOTE(x)
string MacroName = EXPAND_QUOTE(NAME);

float4 PS(float4 c : COLOR0) : COLOR { return c * SCALE; }

technique NAME { pass P0 { PixelShader = compile ps_2_0 PS(); } }
