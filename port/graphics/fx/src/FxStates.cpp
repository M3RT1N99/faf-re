#include "gpg/gal/fx/FxStates.h"

#include <cctype>
#include <string>

namespace gpg::gal::fx {

  namespace {

    bool EqualsNoCase(const std::string_view a, const char* b)
    {
      std::size_t i = 0;
      for (; i < a.size() && b[i] != '\0'; ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) {
          return false;
        }
      }
      return i == a.size() && b[i] == '\0';
    }

    // Value names, d3d9types.h values.
    constexpr StateEnumValue kBool[] = {{"TRUE", 1}, {"FALSE", 0}, {nullptr, 0}};
    constexpr StateEnumValue kZBuffer[] = {{"FALSE", 0}, {"TRUE", 1}, {"USEW", 2}, {nullptr, 0}};
    constexpr StateEnumValue kFill[] = {{"POINT", 1}, {"WIREFRAME", 2}, {"SOLID", 3}, {nullptr, 0}};
    constexpr StateEnumValue kShade[] = {{"FLAT", 1}, {"GOURAUD", 2}, {"PHONG", 3}, {nullptr, 0}};
    // D3DX accepts neither the D3D9Ex blend factors (SRCCOLOR2, INVSRCCOLOR2)
    // nor, for the separate alpha blend, the colour factors; it rejects
    // DebugMonitorToken's and CONVOLUTIONMONO's names too (measured value
    // name by value name; the rest of these tables match D3DX exactly).
    constexpr StateEnumValue kBlend[] = {
      {"ZERO", 1},          {"ONE", 2},           {"SRCCOLOR", 3},        {"INVSRCCOLOR", 4},
      {"SRCALPHA", 5},      {"INVSRCALPHA", 6},   {"DESTALPHA", 7},       {"INVDESTALPHA", 8},
      {"DESTCOLOR", 9},     {"INVDESTCOLOR", 10}, {"SRCALPHASAT", 11},    {"BOTHSRCALPHA", 12},
      {"BOTHINVSRCALPHA", 13}, {"BLENDFACTOR", 14}, {"INVBLENDFACTOR", 15}, {nullptr, 0}};
    constexpr StateEnumValue kBlendAlpha[] = {
      {"ZERO", 1},         {"ONE", 2},          {"SRCALPHA", 5},     {"INVSRCALPHA", 6},       {"DESTALPHA", 7},
      {"INVDESTALPHA", 8}, {"SRCALPHASAT", 11}, {"BLENDFACTOR", 14}, {"INVBLENDFACTOR", 15}, {nullptr, 0}};
    constexpr StateEnumValue kCull[] = {{"NONE", 1}, {"CW", 2}, {"CCW", 3}, {nullptr, 0}};
    constexpr StateEnumValue kCmp[] = {{"NEVER", 1},   {"LESS", 2},     {"EQUAL", 3},        {"LESSEQUAL", 4},
                                       {"GREATER", 5}, {"NOTEQUAL", 6}, {"GREATEREQUAL", 7}, {"ALWAYS", 8},
                                       {nullptr, 0}};
    constexpr StateEnumValue kStencilOp[] = {{"KEEP", 1},    {"ZERO", 2},   {"REPLACE", 3}, {"INCRSAT", 4},
                                             {"DECRSAT", 5}, {"INVERT", 6}, {"INCR", 7},    {"DECR", 8},
                                             {nullptr, 0}};
    constexpr StateEnumValue kFog[] = {{"NONE", 0}, {"EXP", 1}, {"EXP2", 2}, {"LINEAR", 3}, {nullptr, 0}};
    constexpr StateEnumValue kBlendOp[] = {{"ADD", 1}, {"SUBTRACT", 2}, {"REVSUBTRACT", 3},
                                           {"MIN", 4}, {"MAX", 5},      {nullptr, 0}};
    constexpr StateEnumValue kMaterialSource[] = {{"MATERIAL", 0}, {"COLOR1", 1}, {"COLOR2", 2}, {nullptr, 0}};
    constexpr StateEnumValue kVertexBlend[] = {{"DISABLE", 0}, {"TWEENING", 255}, {nullptr, 0}};
    constexpr StateEnumValue kPatchEdge[] = {{"DISCRETE", 0}, {"CONTINUOUS", 1}, {nullptr, 0}};
    constexpr StateEnumValue kDegree[] = {{"LINEAR", 1}, {"QUADRATIC", 2}, {"CUBIC", 3}, {"QUINTIC", 5}, {nullptr, 0}};
    constexpr StateEnumValue kColorWrite[] = {{"RED", 1}, {"GREEN", 2}, {"BLUE", 4}, {"ALPHA", 8}, {nullptr, 0}};
    constexpr StateEnumValue kWrap[] = {{"U", 1},      {"V", 2},      {"W", 4},      {"COORD0", 1},
                                        {"COORD1", 2}, {"COORD2", 4}, {"COORD3", 8}, {nullptr, 0}};
    constexpr StateEnumValue kAddress[] = {{"WRAP", 1},   {"MIRROR", 2},     {"CLAMP", 3},
                                           {"BORDER", 4}, {"MIRRORONCE", 5}, {nullptr, 0}};
    constexpr StateEnumValue kFilter[] = {{"NONE", 0},          {"POINT", 1},        {"LINEAR", 2},
                                          {"ANISOTROPIC", 3},   {"PYRAMIDALQUAD", 6}, {"GAUSSIANQUAD", 7},
                                          {nullptr, 0}};
    constexpr StateEnumValue kTextureOp[] = {
      {"DISABLE", 1},
      {"SELECTARG1", 2},
      {"SELECTARG2", 3},
      {"MODULATE", 4},
      {"MODULATE2X", 5},
      {"MODULATE4X", 6},
      {"ADD", 7},
      {"ADDSIGNED", 8},
      {"ADDSIGNED2X", 9},
      {"SUBTRACT", 10},
      {"ADDSMOOTH", 11},
      {"BLENDDIFFUSEALPHA", 12},
      {"BLENDTEXTUREALPHA", 13},
      {"BLENDFACTORALPHA", 14},
      {"BLENDTEXTUREALPHAPM", 15},
      {"BLENDCURRENTALPHA", 16},
      {"PREMODULATE", 17},
      {"MODULATEALPHA_ADDCOLOR", 18},
      {"MODULATECOLOR_ADDALPHA", 19},
      {"MODULATEINVALPHA_ADDCOLOR", 20},
      {"MODULATEINVCOLOR_ADDALPHA", 21},
      {"BUMPENVMAP", 22},
      {"BUMPENVMAPLUMINANCE", 23},
      {"DOTPRODUCT3", 24},
      {"MULTIPLYADD", 25},
      {"LERP", 26},
      {nullptr, 0}};
    constexpr StateEnumValue kTextureArg[] = {{"DIFFUSE", 0},  {"CURRENT", 1},     {"TEXTURE", 2},
                                              {"TFACTOR", 3},  {"SPECULAR", 4},    {"TEMP", 5},
                                              {"CONSTANT", 6}, {"COMPLEMENT", 16}, {"ALPHAREPLICATE", 32},
                                              {nullptr, 0}};
    constexpr StateEnumValue kTexCoordIndex[] = {{"PASSTHRU", 0x00000},
                                                 {"CAMERASPACENORMAL", 0x10000},
                                                 {"CAMERASPACEPOSITION", 0x20000},
                                                 {"CAMERASPACEREFLECTIONVECTOR", 0x30000},
                                                 {"SPHEREMAP", 0x40000},
                                                 {nullptr, 0}};
    constexpr StateEnumValue kTransformFlags[] = {{"DISABLE", 0}, {"COUNT1", 1}, {"COUNT2", 2},     {"COUNT3", 3},
                                                  {"COUNT4", 4},  {"PROJECTED", 256}, {nullptr, 0}};

    using K = StateValueKind;
    constexpr StateOp R = StateOp::Render;
    constexpr StateOp T = StateOp::TextureStage;
    constexpr StateOp S = StateOp::Sampler;

    // D3DRENDERSTATETYPE, D3DTEXTURESTAGESTATETYPE, D3DSAMPLERSTATETYPE.
    constexpr StateInfo kStates[] = {
      {"ZENABLE", R, 7, K::Enum, kZBuffer},
      {"FILLMODE", R, 8, K::Enum, kFill},
      {"SHADEMODE", R, 9, K::Enum, kShade},
      {"ZWRITEENABLE", R, 14, K::Bool, kBool},
      {"ALPHATESTENABLE", R, 15, K::Bool, kBool},
      {"LASTPIXEL", R, 16, K::Bool, kBool},
      {"SRCBLEND", R, 19, K::Enum, kBlend},
      {"DESTBLEND", R, 20, K::Enum, kBlend},
      {"CULLMODE", R, 22, K::Enum, kCull},
      {"ZFUNC", R, 23, K::Enum, kCmp},
      {"ALPHAREF", R, 24, K::Dword, nullptr},
      {"ALPHAFUNC", R, 25, K::Enum, kCmp},
      {"DITHERENABLE", R, 26, K::Bool, kBool},
      {"ALPHABLENDENABLE", R, 27, K::Bool, kBool},
      {"FOGENABLE", R, 28, K::Bool, kBool},
      {"SPECULARENABLE", R, 29, K::Bool, kBool},
      {"FOGCOLOR", R, 34, K::Color, nullptr},
      {"FOGTABLEMODE", R, 35, K::Enum, kFog},
      {"FOGSTART", R, 36, K::Float, nullptr},
      {"FOGEND", R, 37, K::Float, nullptr},
      {"FOGDENSITY", R, 38, K::Float, nullptr},
      {"RANGEFOGENABLE", R, 48, K::Bool, kBool},
      {"STENCILENABLE", R, 52, K::Bool, kBool},
      {"STENCILFAIL", R, 53, K::Enum, kStencilOp},
      {"STENCILZFAIL", R, 54, K::Enum, kStencilOp},
      {"STENCILPASS", R, 55, K::Enum, kStencilOp},
      {"STENCILFUNC", R, 56, K::Enum, kCmp},
      {"STENCILREF", R, 57, K::Dword, nullptr},
      {"STENCILMASK", R, 58, K::Dword, nullptr},
      {"STENCILWRITEMASK", R, 59, K::Dword, nullptr},
      {"TEXTUREFACTOR", R, 60, K::Color, nullptr},
      {"WRAP0", R, 128, K::Enum, kWrap},
      {"WRAP1", R, 129, K::Enum, kWrap},
      {"WRAP2", R, 130, K::Enum, kWrap},
      {"WRAP3", R, 131, K::Enum, kWrap},
      {"WRAP4", R, 132, K::Enum, kWrap},
      {"WRAP5", R, 133, K::Enum, kWrap},
      {"WRAP6", R, 134, K::Enum, kWrap},
      {"WRAP7", R, 135, K::Enum, kWrap},
      {"CLIPPING", R, 136, K::Bool, kBool},
      {"LIGHTING", R, 137, K::Bool, kBool},
      {"AMBIENT", R, 139, K::Color, nullptr},
      {"FOGVERTEXMODE", R, 140, K::Enum, kFog},
      {"COLORVERTEX", R, 141, K::Bool, kBool},
      {"LOCALVIEWER", R, 142, K::Bool, kBool},
      {"NORMALIZENORMALS", R, 143, K::Bool, kBool},
      {"DIFFUSEMATERIALSOURCE", R, 145, K::Enum, kMaterialSource},
      {"SPECULARMATERIALSOURCE", R, 146, K::Enum, kMaterialSource},
      {"AMBIENTMATERIALSOURCE", R, 147, K::Enum, kMaterialSource},
      {"EMISSIVEMATERIALSOURCE", R, 148, K::Enum, kMaterialSource},
      {"VERTEXBLEND", R, 151, K::Enum, kVertexBlend},
      {"CLIPPLANEENABLE", R, 152, K::Dword, nullptr},
      {"POINTSIZE", R, 154, K::Float, nullptr},
      {"POINTSIZE_MIN", R, 155, K::Float, nullptr},
      {"POINTSPRITEENABLE", R, 156, K::Bool, kBool},
      {"POINTSCALEENABLE", R, 157, K::Bool, kBool},
      {"POINTSCALE_A", R, 158, K::Float, nullptr},
      {"POINTSCALE_B", R, 159, K::Float, nullptr},
      {"POINTSCALE_C", R, 160, K::Float, nullptr},
      {"MULTISAMPLEANTIALIAS", R, 161, K::Bool, kBool},
      {"MULTISAMPLEMASK", R, 162, K::Dword, nullptr},
      {"PATCHEDGESTYLE", R, 163, K::Enum, kPatchEdge},
      {"DEBUGMONITORTOKEN", R, 165, K::Dword, nullptr},
      {"POINTSIZE_MAX", R, 166, K::Float, nullptr},
      {"INDEXEDVERTEXBLENDENABLE", R, 167, K::Bool, kBool},
      {"COLORWRITEENABLE", R, 168, K::Enum, kColorWrite},
      {"TWEENFACTOR", R, 170, K::Float, nullptr},
      {"BLENDOP", R, 171, K::Enum, kBlendOp},
      {"POSITIONDEGREE", R, 172, K::Enum, kDegree},
      {"NORMALDEGREE", R, 173, K::Enum, kDegree},
      {"SCISSORTESTENABLE", R, 174, K::Bool, kBool},
      {"SLOPESCALEDEPTHBIAS", R, 175, K::Float, nullptr},
      {"ANTIALIASEDLINEENABLE", R, 176, K::Bool, kBool},
      {"MINTESSELLATIONLEVEL", R, 178, K::Float, nullptr},
      {"MAXTESSELLATIONLEVEL", R, 179, K::Float, nullptr},
      {"ADAPTIVETESS_X", R, 180, K::Float, nullptr},
      {"ADAPTIVETESS_Y", R, 181, K::Float, nullptr},
      {"ADAPTIVETESS_Z", R, 182, K::Float, nullptr},
      {"ADAPTIVETESS_W", R, 183, K::Float, nullptr},
      {"ENABLEADAPTIVETESSELLATION", R, 184, K::Bool, kBool},
      {"TWOSIDEDSTENCILMODE", R, 185, K::Bool, kBool},
      // d3dx9 maps CCW_StencilFail to D3DRS_STENCILFAIL (53), not
      // D3DRS_CCW_STENCILFAIL (186): measured on range.fx and vision.fx, where
      // `StencilFail = zero; ... CCW_StencilFail = keep;` reaches the state
      // manager as SetRenderState(53, 2) then SetRenderState(53, 1). The D3D9
      // backend is the reference, so the metadata keeps that mapping.
      {"CCW_STENCILFAIL", R, 53, K::Enum, kStencilOp},
      {"CCW_STENCILZFAIL", R, 187, K::Enum, kStencilOp},
      {"CCW_STENCILPASS", R, 188, K::Enum, kStencilOp},
      {"CCW_STENCILFUNC", R, 189, K::Enum, kCmp},
      {"COLORWRITEENABLE1", R, 190, K::Enum, kColorWrite},
      {"COLORWRITEENABLE2", R, 191, K::Enum, kColorWrite},
      {"COLORWRITEENABLE3", R, 192, K::Enum, kColorWrite},
      {"BLENDFACTOR", R, 193, K::Color, nullptr},
      {"SRGBWRITEENABLE", R, 194, K::Bool, kBool},
      {"DEPTHBIAS", R, 195, K::Float, nullptr},
      {"WRAP8", R, 198, K::Enum, kWrap},
      {"WRAP9", R, 199, K::Enum, kWrap},
      {"WRAP10", R, 200, K::Enum, kWrap},
      {"WRAP11", R, 201, K::Enum, kWrap},
      {"WRAP12", R, 202, K::Enum, kWrap},
      {"WRAP13", R, 203, K::Enum, kWrap},
      {"WRAP14", R, 204, K::Enum, kWrap},
      {"WRAP15", R, 205, K::Enum, kWrap},
      {"SEPARATEALPHABLENDENABLE", R, 206, K::Bool, kBool},
      {"SRCBLENDALPHA", R, 207, K::Enum, kBlendAlpha},
      {"DESTBLENDALPHA", R, 208, K::Enum, kBlendAlpha},
      // Like CCW_StencilFail: d3dx9 sends BlendOpAlpha to D3DRS_BLENDOP (171),
      // not D3DRS_BLENDOPALPHA (209) (measured, tests/effects/states.fx).
      {"BLENDOPALPHA", R, 171, K::Enum, kBlendOp},

      {"COLOROP", T, 1, K::Enum, kTextureOp},
      {"COLORARG1", T, 2, K::Enum, kTextureArg},
      {"COLORARG2", T, 3, K::Enum, kTextureArg},
      {"ALPHAOP", T, 4, K::Enum, kTextureOp},
      {"ALPHAARG1", T, 5, K::Enum, kTextureArg},
      {"ALPHAARG2", T, 6, K::Enum, kTextureArg},
      {"BUMPENVMAT00", T, 7, K::Float, nullptr},
      {"BUMPENVMAT01", T, 8, K::Float, nullptr},
      {"BUMPENVMAT10", T, 9, K::Float, nullptr},
      {"BUMPENVMAT11", T, 10, K::Float, nullptr},
      {"TEXCOORDINDEX", T, 11, K::Enum, kTexCoordIndex},
      {"BUMPENVLSCALE", T, 22, K::Float, nullptr},
      {"BUMPENVLOFFSET", T, 23, K::Float, nullptr},
      {"TEXTURETRANSFORMFLAGS", T, 24, K::Enum, kTransformFlags},
      {"COLORARG0", T, 26, K::Enum, kTextureArg},
      {"ALPHAARG0", T, 27, K::Enum, kTextureArg},
      {"RESULTARG", T, 28, K::Enum, kTextureArg},
      {"CONSTANT", T, 32, K::Color, nullptr},

      {"ADDRESSU", S, 1, K::Enum, kAddress},
      {"ADDRESSV", S, 2, K::Enum, kAddress},
      {"ADDRESSW", S, 3, K::Enum, kAddress},
      {"BORDERCOLOR", S, 4, K::Color, nullptr},
      {"MAGFILTER", S, 5, K::Enum, kFilter},
      {"MINFILTER", S, 6, K::Enum, kFilter},
      {"MIPFILTER", S, 7, K::Enum, kFilter},
      {"MIPMAPLODBIAS", S, 8, K::Float, nullptr},
      {"MAXMIPLEVEL", S, 9, K::Dword, nullptr},
      {"MAXANISOTROPY", S, 10, K::Dword, nullptr},
      {"SRGBTEXTURE", S, 11, K::Bool, kBool},
      {"ELEMENTINDEX", S, 12, K::Dword, nullptr},
      {"DMAPOFFSET", S, 13, K::Dword, nullptr},

      {"TEXTURE", StateOp::Texture, 0, K::Dword, nullptr},
      {"VERTEXSHADER", StateOp::VertexShader, 0, K::Dword, nullptr},
      {"PIXELSHADER", StateOp::PixelShader, 0, K::Dword, nullptr},
    };

  } // namespace

  const StateInfo* FindPassState(const std::string_view name)
  {
    for (const StateInfo& state : kStates) {
      if (EqualsNoCase(name, state.name)) {
        return &state;
      }
    }
    return nullptr;
  }

  const StateInfo* FindSamplerState(const std::string_view name)
  {
    const StateInfo* state = FindPassState(name);
    if (state != nullptr && (state->op == StateOp::Sampler || state->op == StateOp::Texture)) {
      return state;
    }
    return nullptr;
  }

  std::optional<std::uint32_t> FindStateValue(const StateInfo& state, const std::string_view name)
  {
    if (state.values == nullptr) {
      return std::nullopt;
    }
    for (const StateEnumValue* value = state.values; value->name != nullptr; ++value) {
      if (EqualsNoCase(name, value->name)) {
        return value->value;
      }
    }
    return std::nullopt;
  }

  const char* StateName(const StateOp op, const std::uint32_t id)
  {
    for (const StateInfo& state : kStates) {
      if (state.op == op && state.id == id) {
        return state.name;
      }
    }
    return "?";
  }

} // namespace gpg::gal::fx
