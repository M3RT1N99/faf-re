// Pass and sampler states, checked against D3DX by
// scripts/port/fx_metadata_gate.py: every render, texture stage and sampler
// state the front end knows, their value names, the conversions D3DX applies
// (float to DWORD truncates, int to float converts, float4 to D3DCOLOR),
// flag lists with |, values read from parameters at BeginPass, states
// assigned twice, and the CCW_StencilFail mapping.

float4x4 Transform;
float FogStartValue = 0.125;
static float FogEndValue = 0.875;
int RefValue = 0x40;
texture DiffuseTexture;
texture NormalTexture;
texture CubeTexture;

sampler2D Diffuse = sampler_state
{
    Texture = <DiffuseTexture>;
    AddressU = Wrap;
    AddressV = Mirror;
    AddressW = Clamp;
    BorderColor = float4(1, 0.5, 0.25, 0);
    MagFilter = Point;
    MinFilter = Anisotropic;
    MipFilter = None;
    MipMapLodBias = -1;
    MaxMipLevel = 2;
    MaxAnisotropy = 4.7;
    SRGBTexture = true;
    ElementIndex = 0;
    DMapOffset = 0;
};

sampler2D Normal = sampler_state
{
    Texture = (NormalTexture);
    AddressU = Border;
    AddressV = MirrorOnce;
    BorderColor = 0x80FF00FF;
    MinFilter = PyramidalQuad;
    MagFilter = GaussianQuad;
    MipFilter = Linear;
    AddressU = Clamp;
    Texture = <DiffuseTexture>;
};

samplerCUBE Cube = sampler_state
{
    Texture = <CubeTexture>;
    MinFilter = LINEAR;
    MagFilter = linear;
    MipFilter = LiNeAr;
};

float4 VS(float4 p : POSITION) : POSITION { return mul(p, Transform); }
float4 PS(float2 uv : TEXCOORD0, float3 n : TEXCOORD1) : COLOR
{
    return tex2D(Diffuse, uv) + tex2D(Normal, uv) + texCUBE(Cube, n);
}

technique RenderStates
{
    pass P0
    {
        ZEnable = USEW;
        FillMode = Wireframe;
        ShadeMode = Gouraud;
        ZWriteEnable = TRUE;
        AlphaTestEnable = 3;
        LastPixel = false;
        SrcBlend = BothInvSrcAlpha;
        DestBlend = InvBlendFactor;
        CullMode = CCW;
        ZFunc = GreaterEqual;
        AlphaRef = 0.5;
        AlphaFunc = 6;
        DitherEnable = true;
        AlphaBlendEnable = 1;
        FogEnable = false;
        SpecularEnable = true;
        FogColor = float4(1, 0, 0, 1);
        FogTableMode = Exp2;
        FogStart = (FogStartValue);
        FogEnd = (FogEndValue);
        FogDensity = (FogStartValue * 2);
        RangeFogEnable = false;
        StencilEnable = true;
        StencilFail = IncrSat;
        StencilZFail = DecrSat;
        StencilPass = Invert;
        StencilFunc = Never;
        StencilRef = (RefValue);
        StencilMask = 0xF0;
        StencilWriteMask = -1;
        TextureFactor = 0x80FFFFFF;
        Wrap0 = U | V;
        Wrap1 = W;
        Wrap15 = 0;
        Clipping = true;
        Lighting = false;
        Ambient = float4(0.25, 0.5, 0.75, 1);
        FogVertexMode = Linear;
        ColorVertex = true;
        LocalViewer = false;
        NormalizeNormals = true;
        DiffuseMaterialSource = Color1;
        SpecularMaterialSource = Color2;
        AmbientMaterialSource = Material;
        EmissiveMaterialSource = Color1;
        VertexBlend = Tweening;
        ClipPlaneEnable = 3;
        PointSize = 4;
        PointSize_Min = 1.5;
        PointSpriteEnable = true;
        PointScaleEnable = false;
        PointScale_A = 0.25;
        PointScale_B = 0.5;
        PointScale_C = 0.75;
        MultiSampleAntialias = true;
        MultiSampleMask = 0xFFFFFFFF;
        PatchEdgeStyle = Continuous;
        DebugMonitorToken = 1;
        PointSize_Max = 64;
        IndexedVertexBlendEnable = false;
        ColorWriteEnable = RED | GREEN | ALPHA;
        TweenFactor = 0.5;
        BlendOp = RevSubtract;
        PositionDegree = Cubic;
        NormalDegree = Linear;
        ScissorTestEnable = true;
        SlopeScaleDepthBias = -2;
        AntialiasedLineEnable = true;
        MinTessellationLevel = 1;
        MaxTessellationLevel = 8.5;
        AdaptiveTess_X = 0.1;
        AdaptiveTess_Y = 0.2;
        AdaptiveTess_Z = 0.3;
        AdaptiveTess_W = 0.4;
        EnableAdaptiveTessellation = false;
        TwoSidedStencilMode = true;
        CCW_StencilFail = Replace;
        CCW_StencilZFail = Incr;
        CCW_StencilPass = Decr;
        CCW_StencilFunc = LessEqual;
        ColorWriteEnable1 = BLUE;
        ColorWriteEnable2 = 0;
        ColorWriteEnable3 = 15;
        BlendFactor = float4(0.5, 0.5, 0.5, 0.5);
        SRGBWriteEnable = false;
        DepthBias = -0.00002f;
        SeparateAlphaBlendEnable = true;
        SrcBlendAlpha = SrcAlphaSat;
        DestBlendAlpha = InvDestAlpha;
        BlendOpAlpha = Max;
        VertexShader = compile vs_1_1 VS();
        PixelShader = compile ps_2_0 PS();
    }
}

technique StageAndSamplerStates
{
    pass P0
    {
        ColorOp[0] = Modulate2X;
        ColorArg0[0] = Temp;
        ColorArg1[0] = Texture | AlphaReplicate;
        ColorArg2[0] = Diffuse | Complement;
        AlphaOp[0] = SelectArg1;
        AlphaArg0[0] = Current;
        AlphaArg1[0] = TFactor;
        AlphaArg2[0] = Specular;
        ResultArg[0] = Temp;
        ColorOp[1] = DotProduct3;
        AlphaOp[1] = Disable;
        BumpEnvMat00[1] = 0.5;
        BumpEnvMat01[1] = -0.5;
        BumpEnvMat10[1] = 1;
        BumpEnvMat11[1] = 2;
        BumpEnvLScale[1] = 0.25;
        BumpEnvLOffset[1] = 0.75;
        TexCoordIndex[1] = CameraSpaceReflectionVector | 1;
        TextureTransformFlags[1] = Count3 | Projected;
        Constant[2] = float4(0, 0, 1, 1);
        ColorOp[7] = Lerp;
        Texture[0] = <DiffuseTexture>;
        Texture[1] = (NormalTexture);
        MinFilter[0] = Point;
        AddressU[3] = Border;
        BorderColor[3] = float4(0, 1, 0, 1);
        MipMapLodBias[3] = 0.5;
        VertexShader = null;
        PixelShader = null;
    }
}

technique Duplicates
{
    pass P0
    {
        ZEnable = true;
        CullMode = None;
        ZEnable = false;
        ColorOp[0] = Modulate;
        ColorOp[1] = Add;
        ColorOp[0] = Disable;
        StencilFail = Zero;
        CCW_StencilFail = Keep;
        StencilFail = Replace;
        PixelShader = compile ps_2_0 PS();
        PixelShader = null;
    }
}
