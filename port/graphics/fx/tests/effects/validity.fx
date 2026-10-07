// Technique validity per device profile, checked against D3DX by
// scripts/port/fx_metadata_gate.py. D3DX rejects a technique when the device
// refuses one of its shaders; the gate's fake device refuses shaders above a
// profile (all: 3.0, sm2: 2.0, sm1: vs 1.1 / ps 1.4). ps_2_a and ps_2_b
// compile to 2.1 bytecode, so sm2 rejects them.

float4x4 Transform;

float4 VS(float4 p : POSITION) : POSITION { return mul(p, Transform); }
float4 PS(float4 c : COLOR0) : COLOR { return c * 0.5; }

technique VS11 { pass P0 { VertexShader = compile vs_1_1 VS(); } }
technique VS20 { pass P0 { VertexShader = compile vs_2_0 VS(); } }
technique VS2A { pass P0 { VertexShader = compile vs_2_a VS(); } }
technique VS30 { pass P0 { VertexShader = compile vs_3_0 VS(); PixelShader = compile ps_3_0 PS(); } }
technique PS11 { pass P0 { PixelShader = compile ps_1_1 PS(); } }
technique PS12 { pass P0 { PixelShader = compile ps_1_2 PS(); } }
technique PS13 { pass P0 { PixelShader = compile ps_1_3 PS(); } }
technique PS14 { pass P0 { PixelShader = compile ps_1_4 PS(); } }
technique PS20 { pass P0 { PixelShader = compile ps_2_0 PS(); } }
technique PS2A { pass P0 { PixelShader = compile ps_2_a PS(); } }
technique PS2B { pass P0 { PixelShader = compile ps_2_b PS(); } }
technique FixedFunction { pass P0 { VertexShader = null; PixelShader = null; } }
technique NoShaders { pass P0 { ZEnable = false; } }
technique MixedPasses
{
    pass P0 { VertexShader = compile vs_1_1 VS(); PixelShader = compile ps_1_4 PS(); }
    pass P1 { VertexShader = compile vs_1_1 VS(); PixelShader = compile ps_2_a PS(); }
}
technique LastValid { pass P0 { VertexShader = compile vs_1_1 VS(); PixelShader = compile ps_1_1 PS(); } }
