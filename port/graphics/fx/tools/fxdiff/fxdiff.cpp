// fxdiff: does the SM5 code FxHlslEmitter generates compute what the game's D3D9 shaders compute?
//
//   fxdiff --compat d3d9states.compat [--out DIR] [--render] [--spirv] [--size N] [--seed S]
//          [--tolerance T] [--set NAME=v0,v1,...]... EFFECT.fx...
//
// --render draws every pass of every valid technique twice, on the same GPU:
//   - D3D9: the effect compiled as DeviceD3D9::CreateEffectFromSourceBuffer compiles it (D3DX with
//     D3DXSHADER_DEBUG | D3DXSHADER_USE_LEGACY_D3DX9_31_DLL, D3D9Interfaces.cpp:1529-1626) on a HAL
//     device created as the game creates it (flags 0x44, D3D9Interfaces.cpp:1772) on a window that is
//     never shown; ID3DXEffect::BeginPass applies the pass;
//   - Diligent-D3D11: the backend's own effect layer (port/graphics/diligent/EffectsDiligentGpu.cpp:
//     EffectGpu, PassBinding::GetProgram and Commit), so the generated shaders, the constant layout,
//     the setters, the immutable samplers and the half-pixel offset are the ones main.exe uses.
// Both get the same parameter values (random, seeded by name; matrices near identity so geometry
// stays on screen; --set overrides), the same random textures (full mip chains), the same vertices
// (a grid of triangles over the target with random attributes per vertex, in a vertex declaration
// holding exactly what the vertex shader reads; POSITIONT for passes without a vertex shader), with
// blending, depth, stencil and culling off. Two modes per pass:
//   float: A32B32G32R32F / RGBA32_FLOAT target, alpha test off: the shader arithmetic;
//   unorm: A8R8G8B8 / RGBA8_UNORM target with the pass's alpha test (D3D9's fixed-function test
//          against the generated `discard`): coverage and 8-bit results.
// The targets are read back and compared per channel: max |delta|, pixels over --tolerance, and
// pixels covered on one side only.
//
// --spirv runs every generated stage through Diligent's HLSL -> SPIR-V path for Vulkan
// (GLSLangUtils::HLSLtoSPIRV with ShaderVkImpl.cpp's VULKAN define) and writes the .spv files for
// spirv-val (scripts/port/fxdiff.py runs it).
//
// Writes DIR/fxdiff.json; with --render also DIR/<effect>.<technique>.p<n>.<mode>.{d3d9,dg}.f32
// (raw RGBA floats, size*size*4) for passes that differ. Exit code 0 when everything agrees, 1 if
// not, 2 on bad usage or setup failure.
//
// Diagnostics (environment): FXDIFF_MUTATE=swizzle|offset|alpharef breaks one D3D9 rule on the
// Diligent side (the comparison must then fail); FXDIFF_DUMP_ALL dumps every pass;
// FXDIFF_COORD_TEXTURE fills textures with their own texel coordinates and level;
// FXDIFF_ONE_LEVEL makes textures single-level; FXDIFF_DEBUG prints D3D9 shader state per pass.

#include <windows.h>

#include <d3d9.h>
#include <d3dx9.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "gpg/gal/fx/FxHlslEmitter.h"
#include "gpg/gal/fx/FxMetadata.h"
#include "port/graphics/diligent/EffectsDiligentGpu.h"
#include "port/graphics/diligent/PassBinding.h"

#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngineD3D11/interface/EngineFactoryD3D11.h"
#include "Primitives/interface/DebugOutput.h"
#include "GLSLangUtils.hpp"

namespace dg = Diligent;
using namespace gpg::gal;

namespace {

  // ---- small helpers --------------------------------------------------------------------------

  bool ReadFile(const std::string& path, std::string& text)
  {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      return false;
    }
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
  }

  std::string Stem(const std::string& path)
  {
    const std::size_t slash = path.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    const std::size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
  }

  std::string Json(const std::string& text)
  {
    std::string out = "\"";
    for (const char c : text) {
      if (c == '"' || c == '\\') {
        out += '\\';
        out += c;
      } else if (c == '\n') {
        out += "\\n";
      } else if (static_cast<unsigned char>(c) < 0x20) {
        out += ' ';
      } else {
        out += c;
      }
    }
    return out + "\"";
  }

  // xorshift64*: the same numbers on every run and both sides.
  struct Random
  {
    std::uint64_t state;
    explicit Random(const std::uint64_t seed) : state(seed == 0 ? 0x9E3779B97F4A7C15ULL : seed) {}
    std::uint64_t Next()
    {
      state ^= state >> 12;
      state ^= state << 25;
      state ^= state >> 27;
      return state * 0x2545F4914F6CDD1DULL;
    }
    float Unit() { return static_cast<float>(Next() >> 40) / 16777216.0F; } // [0, 1), 24 bits exact
  };

  std::uint64_t Hash(const std::string& text, const std::uint64_t seed)
  {
    std::uint64_t hash = 1469598103934665603ULL ^ seed;
    for (const char c : text) {
      hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ULL;
    }
    return hash;
  }

  // ---- Diligent message counting -----------------------------------------------------------------

  int gDiligentErrors = 0;
  int gSetterFailures = 0;
  std::vector<std::string> gDiligentMessages;

  void DILIGENT_CALL_TYPE OnDiligentMessage(const dg::DEBUG_MESSAGE_SEVERITY severity, const dg::Char* const message,
                                            const char*, const char*, const int)
  {
    if (severity >= dg::DEBUG_MESSAGE_SEVERITY_ERROR) {
      ++gDiligentErrors;
    }
    if (severity >= dg::DEBUG_MESSAGE_SEVERITY_WARNING && gDiligentMessages.size() < 64) {
      gDiligentMessages.push_back(message != nullptr ? message : "");
    }
  }

  void OnEffectLog(const char* const message)
  {
    std::cerr << message << "\n";
  }

  // ---- textures as the effect layer binds them -----------------------------------------------------

  class TestTexture final : public diligent::ShaderResourceSource
  {
  public:
    dg::RefCntAutoPtr<dg::ITexture> texture;
    diligent::ShaderResourceDimension dimension = diligent::ShaderResourceDimension::Texture2D;
    dg::ITextureView* GetShaderResourceView() override
    {
      return texture ? texture->GetDefaultView(dg::TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
    }
    diligent::ShaderResourceDimension GetShaderResourceDimension() const override { return dimension; }
  };

  constexpr std::uint32_t kTextureSize = 16;

  // The height of level 0: 1 for a texture the effect samples through a sampler1D (D3D9 1D
  // textures are 2D textures one texel high, and tex1D leaves v undefined), else square.
  std::uint32_t TextureHeight(const bool oneD)
  {
    return oneD ? 1U : kTextureSize;
  }

  // The texels of one texture: level by level (a full chain from 16x16, or 16x1), face by face,
  // RGBA bytes.
  std::vector<std::vector<std::uint8_t>> TextureLevels(const std::string& name, const bool cube, const bool oneD, const std::uint64_t seed)
  {
    Random random(Hash(name + (cube ? "#cube" : "#2d"), seed));
    std::vector<std::vector<std::uint8_t>> levels;
    for (std::uint32_t face = 0; face < (cube ? 6U : 1U); ++face) {
      // FXDIFF_ONE_LEVEL: no mip chain (to separate mip selection from texel selection).
      const std::uint32_t last = std::getenv("FXDIFF_ONE_LEVEL") != nullptr ? kTextureSize : 1U;
      for (std::uint32_t size = kTextureSize; size >= last; size /= 2) {
        const std::uint32_t height = std::max<std::uint32_t>(1, TextureHeight(oneD) * size / kTextureSize);
        std::vector<std::uint8_t> texels(static_cast<std::size_t>(size) * height * 4);
        for (std::uint8_t& texel : texels) {
          texel = static_cast<std::uint8_t>(random.Next() >> 56);
        }
        if (std::getenv("FXDIFF_COORD_TEXTURE") != nullptr) {
          // Each texel names itself: R = x * 16, G = y * 16, B = level * 32 (diagnosing texel choice).
          for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < size; ++x) {
              std::uint8_t* const texel = &texels[(static_cast<std::size_t>(y) * size + x) * 4];
              texel[0] = static_cast<std::uint8_t>(x * 16);
              texel[1] = static_cast<std::uint8_t>(y * 16);
              texel[2] = static_cast<std::uint8_t>(levels.size() * 32);
              texel[3] = 255;
            }
          }
        }
        levels.push_back(std::move(texels));
      }
    }
    return levels;
  }

  // ---- vertices --------------------------------------------------------------------------------

  struct Element
  {
    std::uint32_t slot = 0;
    BYTE d3dType = D3DDECLTYPE_FLOAT4;
    BYTE usage = D3DDECLUSAGE_TEXCOORD;
    BYTE usageIndex = 0;
    std::uint32_t offset = 0;
    std::uint32_t bytes = 16;
  };

  struct VertexSet
  {
    std::vector<Element> elements;
    std::uint32_t stride = 0;
    std::vector<std::uint8_t> data; // triangle list
    std::uint32_t vertexCount = 0;
    diligent::VertexInputDesc input;
  };

  // A declaration with exactly the slots the vertex shader reads: POSITION as FLOAT3 (w comes from
  // the default, as in the engine's formats), COLOR0 as D3DCOLOR, BLENDINDICES as UBYTE4, the rest
  // FLOAT4. `positionT`: POSITIONT plus TEXCOORD0..1, the layout of formats 7/8.
  VertexSet MakeVertices(const std::uint32_t slots, const bool positionT, const std::uint32_t size, const std::uint64_t seed)
  {
    VertexSet set;
    auto add = [&](const std::uint32_t slot, const BYTE type, const BYTE usage, const BYTE index, const std::uint32_t bytes) {
      Element element;
      element.slot = slot;
      element.d3dType = type;
      element.usage = usage;
      element.usageIndex = index;
      element.offset = set.stride;
      element.bytes = bytes;
      set.stride += bytes;
      set.elements.push_back(element);
    };
    if (positionT) {
      add(0, D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITIONT, 0, 16);
      add(7, D3DDECLTYPE_FLOAT2, D3DDECLUSAGE_TEXCOORD, 0, 8);
      add(8, D3DDECLTYPE_FLOAT2, D3DDECLUSAGE_TEXCOORD, 1, 8);
    } else {
      for (std::uint32_t slot = 0; slot < diligent::kAttribSlotCount; ++slot) {
        if ((slots & (1U << slot)) == 0) {
          continue;
        }
        if (slot == 0) add(0, D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0, 12);
        else if (slot == 1) add(1, D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITION, 1, 16);
        else if (slot == 2) add(2, D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_NORMAL, 0, 16);
        else if (slot == 3) add(3, D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_TANGENT, 0, 16);
        else if (slot == 4) add(4, D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_BINORMAL, 0, 16);
        else if (slot == 5) add(5, D3DDECLTYPE_UBYTE4, D3DDECLUSAGE_BLENDINDICES, 0, 4);
        else if (slot == 6) add(6, D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 0, 4);
        else add(slot, D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_TEXCOORD, static_cast<BYTE>(slot - 7), 16);
      }
    }
    for (const Element& element : set.elements) {
      diligent::AttribKind kind = diligent::AttribKind::Float;
      if (element.usage == D3DDECLUSAGE_POSITIONT) kind = diligent::AttribKind::PositionT;
      if (element.d3dType == D3DDECLTYPE_D3DCOLOR) kind = diligent::AttribKind::UNormBgra;
      if (element.d3dType == D3DDECLTYPE_UBYTE4) kind = diligent::AttribKind::UInt;
      set.input.kinds[element.slot] = kind;
    }

    // An 8x8 grid of quads over the target, two triangles each, random attributes per grid vertex.
    constexpr std::uint32_t kGrid = 8;
    Random random(seed);
    std::vector<std::vector<std::uint8_t>> gridVertices((kGrid + 1) * (kGrid + 1), std::vector<std::uint8_t>(set.stride));
    for (std::uint32_t y = 0; y <= kGrid; ++y) {
      for (std::uint32_t x = 0; x <= kGrid; ++x) {
        std::uint8_t* const vertex = gridVertices[y * (kGrid + 1) + x].data();
        for (const Element& element : set.elements) {
          std::uint8_t* const at = vertex + element.offset;
          if (element.usage == D3DDECLUSAGE_POSITIONT) {
            const float rhw = 0.5F + random.Unit() * 1.5F; // perspective-correct interpolation
            const float values[4] = {static_cast<float>(x * size) / kGrid, static_cast<float>(y * size) / kGrid, 0.25F + 0.5F * random.Unit(), rhw};
            std::memcpy(at, values, 16);
          } else if (element.usage == D3DDECLUSAGE_POSITION && element.usageIndex == 0) {
            const float values[3] = {-1.0F + 2.0F * static_cast<float>(x) / kGrid, 1.0F - 2.0F * static_cast<float>(y) / kGrid,
                                     0.25F + 0.5F * random.Unit()};
            std::memcpy(at, values, 12);
          } else if (element.d3dType == D3DDECLTYPE_D3DCOLOR || element.d3dType == D3DDECLTYPE_UBYTE4) {
            for (std::uint32_t i = 0; i < 4; ++i) {
              // BLENDINDICES index bone palettes (mesh.fx transPalette[80]): keep them in range,
              // where an out-of-range read is undefined on both APIs.
              at[i] = element.usage == D3DDECLUSAGE_BLENDINDICES ? static_cast<std::uint8_t>(random.Next() % 4U)
                                                                  : static_cast<std::uint8_t>(random.Next() >> 56);
            }
          } else {
            const std::uint32_t count = element.bytes / 4;
            for (std::uint32_t i = 0; i < count; ++i) {
              const float value = -0.25F + 1.5F * random.Unit(); // texcoords beyond [0, 1]: address modes
              std::memcpy(at + i * 4, &value, 4);
            }
          }
        }
      }
    }
    for (std::uint32_t y = 0; y < kGrid; ++y) {
      for (std::uint32_t x = 0; x < kGrid; ++x) {
        const std::uint32_t a = y * (kGrid + 1) + x;
        const std::uint32_t b = a + 1;
        const std::uint32_t c = a + kGrid + 1;
        const std::uint32_t d = c + 1;
        for (const std::uint32_t index : {a, b, c, b, d, c}) {
          set.data.insert(set.data.end(), gridVertices[index].begin(), gridVertices[index].end());
          ++set.vertexCount;
        }
      }
    }
    return set;
  }

  // ---- parameter values --------------------------------------------------------------------------

  struct ParameterValues
  {
    std::vector<float> floats; // storage order
    std::vector<int> ints;
    bool matrix = false;
  };

  ParameterValues MakeValues(const fx::ParameterInfo& parameter, const fx::ConstantSlot& slot, const std::uint64_t seed,
                             const std::map<std::string, std::vector<float>>& overrides)
  {
    ParameterValues values;
    values.matrix = slot.matrix;
    const std::uint32_t elements = std::max<std::uint32_t>(1, slot.elements);
    const std::uint32_t count = elements * slot.rows * slot.columns;
    Random random(Hash(parameter.desc.name, seed));
    const auto it = overrides.find(parameter.desc.name);
    for (std::uint32_t i = 0; i < count; ++i) {
      float value = random.Unit();
      if (slot.matrix) {
        // Near identity: geometry stays on screen, a transposed upload still shows.
        const std::uint32_t row = (i % (slot.rows * slot.columns)) / slot.columns;
        const std::uint32_t column = i % slot.columns;
        value = (row == column ? 1.0F : 0.0F) + (row == 3 ? 0.0F : 0.1F * (value - 0.5F));
        if (row == 3 && column == 3) {
          value = 1.0F;
        }
      }
      if (it != overrides.end() && i < it->second.size()) {
        value = it->second[i];
      }
      values.floats.push_back(value);
      values.ints.push_back(slot.type == fx::ParameterType::Bool ? static_cast<int>(random.Next() >> 63)
                                                                  : static_cast<int>(random.Next() % 5U));
    }
    return values;
  }

  // ---- the D3D9 side -------------------------------------------------------------------------------

  struct D3D9Side
  {
    HWND window = nullptr;
    IDirect3D9* d3d = nullptr;
    IDirect3DDevice9* device = nullptr;

    bool Create(std::string& error)
    {
      WNDCLASSA wc{};
      wc.lpfnWndProc = DefWindowProcA;
      wc.hInstance = GetModuleHandleA(nullptr);
      wc.lpszClassName = "fxdiff";
      RegisterClassA(&wc);
      // Never shown (no WS_VISIBLE, no ShowWindow): D3D9 needs a focus window, not a visible one.
      window = CreateWindowExA(0, "fxdiff", "fxdiff", WS_POPUP, -20000, -20000, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
      d3d = Direct3DCreate9(D3D_SDK_VERSION);
      if (window == nullptr || d3d == nullptr) {
        error = "no window or IDirect3D9";
        return false;
      }
      D3DPRESENT_PARAMETERS pp{};
      pp.Windowed = TRUE;
      pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
      pp.BackBufferFormat = D3DFMT_UNKNOWN;
      pp.BackBufferWidth = 64;
      pp.BackBufferHeight = 64;
      pp.hDeviceWindow = window;
      // D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, as the game (D3D9Interfaces.cpp:1772).
      const HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, 0x44, &pp, &device);
      if (FAILED(hr)) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "CreateDevice HAL failed 0x%08lX", static_cast<unsigned long>(hr));
        error = buffer;
        return false;
      }
      return true;
    }

    void Destroy()
    {
      if (device) device->Release();
      if (d3d) d3d->Release();
      if (window) DestroyWindow(window);
      device = nullptr;
      d3d = nullptr;
      window = nullptr;
    }
  };

  ID3DXEffect* CreateD3DXEffect(IDirect3DDevice9* const device, const std::string& source, std::string& error)
  {
    ID3DXEffectCompiler* compiler = nullptr;
    ID3DXBuffer* errors = nullptr;
    ID3DXBuffer* compiled = nullptr;
    ID3DXEffect* effect = nullptr;
    HRESULT hr = D3DXCreateEffectCompiler(source.data(), static_cast<UINT>(source.size()), nullptr, nullptr,
                                          D3DXSHADER_DEBUG | D3DXSHADER_USE_LEGACY_D3DX9_31_DLL, &compiler, &errors);
    if (SUCCEEDED(hr)) {
      hr = compiler->CompileEffect(D3DXSHADER_DEBUG, &compiled, &errors);
    }
    if (SUCCEEDED(hr)) {
      hr = D3DXCreateEffect(device, compiled->GetBufferPointer(), compiled->GetBufferSize(), nullptr, nullptr, D3DXSHADER_DEBUG,
                            nullptr, &effect, &errors);
    }
    if (FAILED(hr)) {
      error = errors != nullptr ? static_cast<const char*>(errors->GetBufferPointer()) : "D3DX failed";
    }
    if (errors) errors->Release();
    if (compiled) compiled->Release();
    if (compiler) compiler->Release();
    return effect;
  }

  // ---- the Diligent side --------------------------------------------------------------------------

  struct DiligentSide
  {
    dg::RefCntAutoPtr<dg::IRenderDevice> device;
    dg::RefCntAutoPtr<dg::IDeviceContext> context;

    bool Create(std::string& error)
    {
      dg::SetDebugMessageCallback(OnDiligentMessage);
      dg::IEngineFactoryD3D11* const factory = dg::GetEngineFactoryD3D11();
      dg::EngineD3D11CreateInfo info;
      info.SetValidationLevel(dg::VALIDATION_LEVEL_1);
      dg::IDeviceContext* contexts[1] = {};
      factory->CreateDeviceAndContextsD3D11(info, &device, contexts);
      context.Attach(contexts[0]);
      if (!device || !context) {
        error = "Diligent D3D11 device not created";
        return false;
      }
      return true;
    }
  };

  // ---- one render -----------------------------------------------------------------------------------

  struct Image
  {
    std::vector<float> rgba; // size*size*4
  };

  struct Comparison
  {
    double maxDelta = 0.0;
    std::uint32_t overTolerance = 0;
    std::uint32_t coverageMismatch = 0;
    std::uint32_t covered = 0; // pixels the D3D9 draw changed
  };

  Comparison Compare(const Image& a, const Image& b, const float clear[4], const double tolerance, const bool unorm)
  {
    Comparison result;
    const std::size_t pixels = a.rgba.size() / 4;
    for (std::size_t p = 0; p < pixels; ++p) {
      const float* const pa = &a.rgba[p * 4];
      const float* const pb = &b.rgba[p * 4];
      const bool clearA = pa[0] == clear[0] && pa[1] == clear[1] && pa[2] == clear[2] && pa[3] == clear[3];
      const bool clearB = pb[0] == clear[0] && pb[1] == clear[1] && pb[2] == clear[2] && pb[3] == clear[3];
      result.covered += clearA ? 0U : 1U;
      if (clearA != clearB) {
        ++result.coverageMismatch;
      }
      bool over = false;
      for (int c = 0; c < 4; ++c) {
        double delta = std::fabs(static_cast<double>(pa[c]) - static_cast<double>(pb[c]));
        if (std::isnan(pa[c]) != std::isnan(pb[c])) {
          delta = 1e30;
        } else if (std::isnan(pa[c])) {
          delta = 0.0;
        }
        result.maxDelta = std::max(result.maxDelta, delta);
        over = over || delta > (unorm ? 1.0 / 255.0 + 1e-6 : tolerance);
      }
      result.overTolerance += over ? 1U : 0U;
    }
    return result;
  }

  struct Options
  {
    std::string compatPath;
    std::string outDir;
    bool render = false;
    bool spirv = false;
    std::uint32_t size = 64;
    std::uint64_t seed = 1;
    double tolerance = 1.0 / 255.0; // one step of the 8-bit targets the game draws into
    std::map<std::string, std::vector<float>> overrides;
  };

  // The render-state overrides that leave the shaders alone.
  void NeutralStates(IDirect3DDevice9* const device, const bool keepAlphaTest)
  {
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_DEPTHBIAS, 0);
    if (!keepAlphaTest) {
      device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    }
  }

  struct PassResult
  {
    std::string effect;
    std::string technique;
    int pass = 0;
    std::string mode;
    std::string vs;
    std::string ps;
    std::string status; // "agree", "differ", "skipped: ..."
    Comparison comparison;
  };

  class Runner
  {
  public:
    Runner(const Options& options, D3D9Side& d3d9, DiligentSide& dg)
      : mOptions(options),
        mD3D9(d3d9),
        mDg(dg)
    {}

    void RunEffect(const std::string& name, const std::string& source, std::vector<PassResult>& results)
    {
      std::string error;
      ID3DXEffect* const dx = CreateD3DXEffect(mD3D9.device, source, error);
      fx::EffectInput input;
      input.parts.emplace_back(name + ".fx", source);
      const std::shared_ptr<diligent::EffectGpu> gpu = diligent::EffectGpu::Create(name, input, &error);
      if (dx == nullptr || !gpu) {
        PassResult result;
        result.effect = name;
        result.status = "skipped: effect not created: " + error;
        results.push_back(result);
        if (dx) dx->Release();
        return;
      }
      const fx::EffectMetadata& metadata = gpu->GetMetadata();
      const fx::ConstantLayout layout = fx::BuildConstantLayout(metadata);
      std::vector<fx::Diagnostic> diagnostics;
      const std::unique_ptr<fx::HlslEmitter> emitter = fx::HlslEmitter::Create(input, metadata, diagnostics);

      // Parameters, the same on both sides.
      for (const fx::ConstantSlot& slot : layout.slots) {
        const fx::ParameterInfo& parameter = metadata.parameters[slot.parameter];
        const ParameterValues values = MakeValues(parameter, slot, mOptions.seed, mOptions.overrides);
        const D3DXHANDLE handle = dx->GetParameterByName(nullptr, parameter.desc.name.c_str());
        const int index = static_cast<int>(slot.parameter);
        const UINT count = static_cast<UINT>(values.floats.size());
        if (slot.structure) {
          // A struct parameter (sky.fx aCirrus): ID3DXEffect::SetValue with the members' values
          // packed tightly, as SkyDome.cpp:924 sets mCirrusData; the backend's SetRaw.
          std::vector<std::uint32_t> words;
          Random random(Hash(parameter.desc.name, mOptions.seed));
          for (std::uint32_t component = 0;; ++component) {
            std::uint32_t word = 0;
            fx::ParameterType type = fx::ParameterType::Float;
            if (!fx::ConstantComponentWord(slot, component, word, type)) {
              break;
            }
            const float value = random.Unit();
            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, 4);
            words.push_back(type == fx::ParameterType::Float ? bits : static_cast<std::uint32_t>(random.Next() % 3U));
          }
          const UINT bytes = static_cast<UINT>(words.size() * 4);
          ReportSetter(name, parameter.desc.name, dx->SetValue(handle, words.data(), bytes));
          gpu->SetRaw(index, words.data(), bytes);
        } else if (slot.type == fx::ParameterType::Float && slot.matrix) {
          std::vector<D3DXMATRIX> matrices(std::max<std::uint32_t>(1, slot.elements));
          std::vector<float> flat(matrices.size() * 16, 0.0F);
          for (std::size_t m = 0; m < matrices.size(); ++m) {
            for (std::uint32_t r = 0; r < 4; ++r) {
              for (std::uint32_t c = 0; c < 4; ++c) {
                float v = (r == c) ? 1.0F : 0.0F;
                if (r < slot.rows && c < slot.columns) {
                  v = values.floats[m * slot.rows * slot.columns + r * slot.columns + c];
                }
                matrices[m].m[r][c] = v;
                flat[m * 16 + r * 4 + c] = v;
              }
            }
          }
          // SetMatrix for a single matrix: D3DX refuses SetMatrixArray on a parameter that is no array.
          const HRESULT hr = slot.elements == 0 ? dx->SetMatrix(handle, &matrices[0])
                                                : dx->SetMatrixArray(handle, matrices.data(), static_cast<UINT>(matrices.size()));
          ReportSetter(name, parameter.desc.name, hr);
          gpu->SetMatrices(index, flat.data(), static_cast<std::uint32_t>(matrices.size()));
        } else if (slot.type == fx::ParameterType::Float) {
          ReportSetter(name, parameter.desc.name, dx->SetFloatArray(handle, values.floats.data(), count));
          gpu->SetFloats(index, values.floats.data(), count);
        } else if (slot.type == fx::ParameterType::Int) {
          ReportSetter(name, parameter.desc.name, dx->SetIntArray(handle, values.ints.data(), count));
          gpu->SetInts(index, values.ints.data(), count);
        } else {
          std::vector<BOOL> bools(values.ints.begin(), values.ints.end());
          ReportSetter(name, parameter.desc.name, dx->SetBoolArray(handle, bools.data(), count));
          gpu->SetBools(index, values.ints.data(), count);
        }
      }

      const fx::DeviceProfile& profile = fx::StandardDeviceProfiles().front();
      for (std::size_t t = 0; t < metadata.techniques.size(); ++t) {
        const fx::TechniqueInfo& technique = metadata.techniques[t];
        if (!fx::IsTechniqueValid(technique, profile)) {
          continue;
        }
        for (std::size_t p = 0; p < technique.passes.size(); ++p) {
          for (const bool unorm : {false, true}) {
            PassResult result;
            result.effect = name;
            result.technique = technique.name;
            result.pass = static_cast<int>(p);
            result.mode = unorm ? "unorm" : "float";
            RunPass(*gpu, dx, *emitter, static_cast<int>(t), static_cast<int>(p), unorm, result);
            results.push_back(result);
          }
        }
      }
      dx->Release();
    }

  private:
    // A D3DX setter that fails leaves the parameter at its default on the D3D9 side only: the
    // comparison would then test the harness, so it is reported.
    static void ReportSetter(const std::string& effect, const std::string& parameter, const HRESULT hr)
    {
      if (FAILED(hr)) {
        std::fprintf(stderr, "fxdiff: %s: D3DX setter for %s failed (0x%08lX)\n", effect.c_str(), parameter.c_str(),
                     static_cast<unsigned long>(hr));
        ++gSetterFailures;
      }
    }

    struct Target
    {
      IDirect3DSurface9* d3d9 = nullptr;
      IDirect3DSurface9* d3d9Readback = nullptr;
      dg::RefCntAutoPtr<dg::ITexture> dg;
      dg::RefCntAutoPtr<dg::ITexture> dgReadback;
    };

    bool MakeTarget(const bool unorm, Target& target)
    {
      const UINT size = mOptions.size;
      const D3DFORMAT format = unorm ? D3DFMT_A8R8G8B8 : D3DFMT_A32B32G32R32F;
      if (FAILED(mD3D9.device->CreateRenderTarget(size, size, format, D3DMULTISAMPLE_NONE, 0, FALSE, &target.d3d9, nullptr)) ||
          FAILED(mD3D9.device->CreateOffscreenPlainSurface(size, size, format, D3DPOOL_SYSTEMMEM, &target.d3d9Readback, nullptr))) {
        return false;
      }
      dg::TextureDesc desc;
      desc.Name = "fxdiff target";
      desc.Type = dg::RESOURCE_DIM_TEX_2D;
      desc.Width = size;
      desc.Height = size;
      desc.Format = unorm ? dg::TEX_FORMAT_RGBA8_UNORM : dg::TEX_FORMAT_RGBA32_FLOAT;
      desc.BindFlags = dg::BIND_RENDER_TARGET;
      mDg.device->CreateTexture(desc, nullptr, &target.dg);
      desc.Name = "fxdiff readback";
      desc.BindFlags = dg::BIND_NONE;
      desc.Usage = dg::USAGE_STAGING;
      desc.CPUAccessFlags = dg::CPU_ACCESS_READ;
      mDg.device->CreateTexture(desc, nullptr, &target.dgReadback);
      return target.dg && target.dgReadback;
    }

    static void ReleaseTarget(Target& target)
    {
      if (target.d3d9) target.d3d9->Release();
      if (target.d3d9Readback) target.d3d9Readback->Release();
      target = Target{};
    }

    Image ReadD3D9(Target& target, const bool unorm)
    {
      Image image;
      image.rgba.resize(static_cast<std::size_t>(mOptions.size) * mOptions.size * 4);
      mD3D9.device->GetRenderTargetData(target.d3d9, target.d3d9Readback);
      D3DLOCKED_RECT locked{};
      if (FAILED(target.d3d9Readback->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
        return image;
      }
      for (std::uint32_t y = 0; y < mOptions.size; ++y) {
        const auto* const row = static_cast<const std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * locked.Pitch;
        for (std::uint32_t x = 0; x < mOptions.size; ++x) {
          float* const out = &image.rgba[(static_cast<std::size_t>(y) * mOptions.size + x) * 4];
          if (unorm) {
            const std::uint8_t* const texel = row + x * 4; // B, G, R, A
            out[0] = texel[2] / 255.0F;
            out[1] = texel[1] / 255.0F;
            out[2] = texel[0] / 255.0F;
            out[3] = texel[3] / 255.0F;
          } else {
            std::memcpy(out, row + x * 16, 16); // A32B32G32R32F is R, G, B, A in memory
          }
        }
      }
      target.d3d9Readback->UnlockRect();
      return image;
    }

    Image ReadDiligent(Target& target, const bool unorm)
    {
      Image image;
      image.rgba.resize(static_cast<std::size_t>(mOptions.size) * mOptions.size * 4);
      dg::CopyTextureAttribs copy(target.dg, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, target.dgReadback,
                                  dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      mDg.context->CopyTexture(copy);
      mDg.context->WaitForIdle();
      dg::MappedTextureSubresource mapped;
      mDg.context->MapTextureSubresource(target.dgReadback, 0, 0, dg::MAP_READ, dg::MAP_FLAG_NONE, nullptr, mapped);
      if (mapped.pData == nullptr) {
        return image;
      }
      for (std::uint32_t y = 0; y < mOptions.size; ++y) {
        const auto* const row = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.Stride;
        for (std::uint32_t x = 0; x < mOptions.size; ++x) {
          float* const out = &image.rgba[(static_cast<std::size_t>(y) * mOptions.size + x) * 4];
          if (unorm) {
            for (int c = 0; c < 4; ++c) {
              out[c] = row[x * 4 + c] / 255.0F;
            }
          } else {
            std::memcpy(out, row + x * 16, 16);
          }
        }
      }
      mDg.context->UnmapTextureSubresource(target.dgReadback, 0, 0);
      return image;
    }

    // The textures a pass samples, made on both sides with the same texels.
    struct PassTextures
    {
      std::vector<IDirect3DBaseTexture9*> d3d9;
      std::vector<std::unique_ptr<TestTexture>> dg;
    };

    bool BindTextures(diligent::EffectGpu& gpu, ID3DXEffect* const dx, const fx::EmittedStage& vs, const fx::EmittedStage& ps,
                      PassTextures& textures, std::string& why)
    {
      std::map<int, std::set<fx::TextureDim>> wanted;
      for (const fx::EmittedStage* stage : {&vs, &ps}) {
        for (const fx::TextureResource& texture : stage->textures) {
          if (texture.textureParameter >= 0) {
            wanted[texture.textureParameter].insert(texture.dim);
          }
        }
      }
      const fx::EffectMetadata& metadata = gpu.GetMetadata();
      // Unbind everything first (no texture: D3D9 and the backend's null texture).
      for (std::size_t i = 0; i < metadata.parameters.size(); ++i) {
        const fx::ParameterType type = metadata.parameters[i].desc.type;
        if (type >= fx::ParameterType::Texture && type <= fx::ParameterType::TextureCube) {
          dx->SetTexture(dx->GetParameterByName(nullptr, metadata.parameters[i].desc.name.c_str()), nullptr);
          gpu.SetTexture(static_cast<int>(i), nullptr);
        }
      }
      for (const auto& [parameter, dims] : wanted) {
        if (dims.size() != 1 || *dims.begin() == fx::TextureDim::Tex3D) {
          why = "texture sampled with two dimensions, or 3D";
          return false;
        }
        const bool cube = *dims.begin() == fx::TextureDim::Cube;
        const std::string& name = metadata.parameters[static_cast<std::size_t>(parameter)].desc.name;
        bool oneD = false;
        for (const fx::ParameterInfo& sampler : metadata.parameters) {
          oneD = oneD || (sampler.desc.type == fx::ParameterType::Sampler1D && sampler.samplerTexture == name);
        }
        const std::uint32_t height = TextureHeight(oneD && !cube);
        const std::vector<std::vector<std::uint8_t>> levels = TextureLevels(name, cube, oneD && !cube, mOptions.seed);
        const UINT levelCount = static_cast<UINT>(levels.size() / (cube ? 6 : 1));
        IDirect3DBaseTexture9* d3dTexture = nullptr;
        if (cube) {
          IDirect3DCubeTexture9* texture = nullptr;
          mD3D9.device->CreateCubeTexture(kTextureSize, levelCount, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr);
          for (UINT face = 0; face < 6 && texture; ++face) {
            for (UINT level = 0; level < levelCount; ++level) {
              D3DLOCKED_RECT locked{};
              texture->LockRect(static_cast<D3DCUBEMAP_FACES>(face), level, &locked, nullptr, 0);
              CopyLevel(levels[face * levelCount + level], kTextureSize >> level, kTextureSize >> level, locked);
              texture->UnlockRect(static_cast<D3DCUBEMAP_FACES>(face), level);
            }
          }
          d3dTexture = texture;
        } else {
          IDirect3DTexture9* texture = nullptr;
          mD3D9.device->CreateTexture(kTextureSize, height, levelCount, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr);
          for (UINT level = 0; level < levelCount && texture; ++level) {
            D3DLOCKED_RECT locked{};
            texture->LockRect(level, &locked, nullptr, 0);
            CopyLevel(levels[level], kTextureSize >> level, std::max<std::uint32_t>(1, height >> level), locked);
            texture->UnlockRect(level);
          }
          d3dTexture = texture;
        }
        if (d3dTexture == nullptr) {
          why = "D3D9 texture not created";
          return false;
        }
        textures.d3d9.push_back(d3dTexture);
        dx->SetTexture(dx->GetParameterByName(nullptr, name.c_str()), d3dTexture);

        auto source = std::make_unique<TestTexture>();
        dg::TextureDesc desc;
        desc.Name = name.c_str();
        desc.Type = cube ? dg::RESOURCE_DIM_TEX_CUBE : dg::RESOURCE_DIM_TEX_2D;
        desc.Width = kTextureSize;
        desc.Height = height;
        desc.ArraySize = cube ? 6 : 1;
        desc.MipLevels = levelCount;
        desc.Format = dg::TEX_FORMAT_RGBA8_UNORM;
        desc.Usage = dg::USAGE_IMMUTABLE;
        desc.BindFlags = dg::BIND_SHADER_RESOURCE;
        std::vector<dg::TextureSubResData> subresources;
        for (std::size_t i = 0; i < levels.size(); ++i) {
          dg::TextureSubResData data;
          data.pData = levels[i].data();
          data.Stride = static_cast<dg::Uint64>(kTextureSize >> (i % levelCount)) * 4;
          subresources.push_back(data);
        }
        dg::TextureData data{subresources.data(), static_cast<dg::Uint32>(subresources.size())};
        mDg.device->CreateTexture(desc, &data, &source->texture);
        source->dimension = cube ? diligent::ShaderResourceDimension::TextureCube : diligent::ShaderResourceDimension::Texture2D;
        gpu.SetTexture(parameter, source.get());
        textures.dg.push_back(std::move(source));
      }
      return true;
    }

    static void CopyLevel(const std::vector<std::uint8_t>& rgba, const std::uint32_t size, const std::uint32_t height,
                          const D3DLOCKED_RECT& locked)
    {
      if (locked.pBits == nullptr) {
        return;
      }
      for (std::uint32_t y = 0; y < height; ++y) {
        auto* const row = static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * locked.Pitch;
        for (std::uint32_t x = 0; x < size; ++x) {
          const std::uint8_t* const texel = &rgba[(static_cast<std::size_t>(y) * size + x) * 4];
          row[x * 4 + 0] = texel[2]; // A8R8G8B8 is B, G, R, A in memory
          row[x * 4 + 1] = texel[1];
          row[x * 4 + 2] = texel[0];
          row[x * 4 + 3] = texel[3];
        }
      }
    }

    void RunPass(diligent::EffectGpu& gpu, ID3DXEffect* const dx, const fx::HlslEmitter& emitter, const int technique, const int pass,
                 const bool unorm, PassResult& result)
    {
      const fx::PassInfo& info = gpu.GetMetadata().techniques[static_cast<std::size_t>(technique)].passes[static_cast<std::size_t>(pass)];
      result.vs = info.vertexShader.kind == fx::ShaderEntry::Kind::Compile ? info.vertexShader.entry : "<fixed-function>";
      result.ps = info.pixelShader.kind == fx::ShaderEntry::Kind::Compile ? info.pixelShader.entry : "<fixed-function>";
      for (const std::string& argument : info.pixelShader.arguments) {
        result.ps += (result.ps.back() == ')' ? "" : "(") + argument + ",";
      }
      // Which vertex slots does the vertex shader read? (Emitted with every slot available.)
      std::vector<fx::Diagnostic> diagnostics;
      fx::EmittedStage ps;
      fx::EmittedStage vsProbe;
      if (info.pixelShader.kind == fx::ShaderEntry::Kind::Compile) {
        emitter.EmitPixelShader(info.pixelShader, ps, diagnostics);
      } else {
        emitter.EmitFixedFunctionPixelShader(ps, diagnostics);
      }
      const bool positionT = info.vertexShader.kind != fx::ShaderEntry::Kind::Compile;
      if (!positionT) {
        fx::VertexInputLayout full;
        for (fx::InputKind& kind : full.kinds) {
          kind = fx::InputKind::Float;
        }
        full.kinds[fx::kSlotColor0] = fx::InputKind::UNormBgra;
        full.kinds[fx::kSlotBlendIndices] = fx::InputKind::UInt;
        emitter.EmitVertexShader(info.vertexShader, full, ps.varyings, vsProbe, diagnostics);
      }
      const VertexSet vertices =
        MakeVertices(vsProbe.vertexSlots, positionT, mOptions.size, Hash(result.effect + result.technique, mOptions.seed + pass));

      PassTextures textures;
      std::string why;
      if (!BindTextures(gpu, dx, vsProbe, ps, textures, why)) {
        result.status = "skipped: " + why;
        return;
      }
      Target target;
      if (!MakeTarget(unorm, target)) {
        result.status = "skipped: render target not created";
        ReleaseTarget(target);
        return;
      }
      // D3DCOLOR 0xFF00FF00 = (0, 1, 0, 1): what both clears give exactly.
      const float clear[4] = {0.0F, 1.0F, 0.0F, 1.0F};
      std::uint32_t alphaTestEnable = 0;
      std::uint32_t alphaFunc = 8;
      std::uint32_t alphaRef = 0;
      for (const fx::PassState& state : info.states) {
        if (state.op == fx::StateOp::Render && state.state == 15) alphaTestEnable = state.value;
        if (state.op == fx::StateOp::Render && state.state == 25) alphaFunc = state.value;
        if (state.op == fx::StateOp::Render && state.state == 24) alphaRef = state.value;
      }
      if (!unorm) {
        alphaTestEnable = 0;
      }

      // ---- D3D9
      IDirect3DDevice9* const device = mD3D9.device;
      device->SetRenderTarget(0, target.d3d9);
      device->SetDepthStencilSurface(nullptr);
      D3DVIEWPORT9 viewport{0, 0, mOptions.size, mOptions.size, 0.0F, 1.0F};
      device->SetViewport(&viewport);
      device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF00FF00, 1.0F, 0);
      std::vector<D3DVERTEXELEMENT9> declaration;
      for (const Element& element : vertices.elements) {
        declaration.push_back({0, static_cast<WORD>(element.offset), element.d3dType, D3DDECLMETHOD_DEFAULT, element.usage, element.usageIndex});
      }
      declaration.push_back(D3DDECL_END());
      IDirect3DVertexDeclaration9* decl = nullptr;
      device->CreateVertexDeclaration(declaration.data(), &decl);
      device->BeginScene();
      // What PipelineStateD3D9::BeginTechnique resets (D3D9Interfaces.cpp:3955-3994) and the draw
      // path's DrawState defaults: no alpha test unless the pass enables it.
      device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
      device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);
      device->SetRenderState(D3DRS_ALPHAREF, 0);
      dx->SetTechnique(dx->GetTechnique(static_cast<UINT>(technique)));
      UINT passes = 0;
      HRESULT d3d9Result = dx->Begin(&passes, D3DXFX_DONOTSAVESTATE);
      const HRESULT beginPass = dx->BeginPass(static_cast<UINT>(pass));
      NeutralStates(device, unorm);
      if (!unorm) {
        device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
      }
      const HRESULT declResult = device->SetVertexDeclaration(decl);
      if (std::getenv("FXDIFF_DEBUG") != nullptr) {
        IDirect3DVertexShader9* vsObject = nullptr;
        IDirect3DPixelShader9* psObject = nullptr;
        device->GetVertexShader(&vsObject);
        device->GetPixelShader(&psObject);
        float c[16] = {};
        device->GetVertexShaderConstantF(0, c, 4);
        std::fprintf(stderr, "[debug] %s P%d vs %p ps %p c0 %g %g %g %g c1 %g %g %g %g c3 %g %g %g %g\n", result.technique.c_str(), pass,
                     static_cast<void*>(vsObject), static_cast<void*>(psObject), c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[12], c[13], c[14], c[15]);
        if (vsObject) vsObject->Release();
        if (psObject) psObject->Release();
      }
      const HRESULT drawResult = device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, vertices.vertexCount / 3, vertices.data.data(), vertices.stride);
      dx->EndPass();
      dx->End();
      if (FAILED(d3d9Result) || FAILED(beginPass) || FAILED(declResult) || FAILED(drawResult)) {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "skipped: D3D9 Begin %08lX BeginPass %08lX decl %08lX draw %08lX",
                      static_cast<unsigned long>(d3d9Result), static_cast<unsigned long>(beginPass),
                      static_cast<unsigned long>(declResult), static_cast<unsigned long>(drawResult));
        result.status = buffer;
      }
      device->EndScene();
      if (decl) decl->Release();
      const Image d3d9Image = ReadD3D9(target, unorm);

      // ---- Diligent (the backend's effect layer)
      diligent::PassBinding* const binding = gpu.BeginPass(technique, pass);
      diligent::PassProgram program;
      // FXDIFF_MUTATE breaks one D3D9 rule on the Diligent side, to show the comparison catches it:
      //   swizzle: D3DCOLOR inputs read as RGBA (no .bgra); offset: no half-pixel offset;
      //   alpharef: the alpha test reference one step higher.
      const char* const mutate = std::getenv("FXDIFF_MUTATE");
      const std::string mutation = mutate != nullptr ? mutate : "";
      diligent::VertexInputDesc input = vertices.input;
      if (mutation == "swizzle" && input.kinds[diligent::kAttribColor0] == diligent::AttribKind::UNormBgra) {
        input.kinds[diligent::kAttribColor0] = diligent::AttribKind::Float;
      }
      if (binding == nullptr || !binding->GetProgram(input, &program)) {
        result.status = "differ: the backend could not make the pass's shaders";
        ReleaseTarget(target);
        for (IDirect3DBaseTexture9* texture : textures.d3d9) texture->Release();
        return;
      }
      dg::GraphicsPipelineStateCreateInfo create;
      const std::string psoName = result.effect + "/" + result.technique;
      create.PSODesc.Name = psoName.c_str();
      create.PSODesc.PipelineType = dg::PIPELINE_TYPE_GRAPHICS;
      create.pVS = program.vertexShader;
      create.pPS = program.pixelShader;
      dg::IPipelineResourceSignature* signatures[1] = {program.signature};
      create.ppResourceSignatures = signatures;
      create.ResourceSignaturesCount = 1;
      create.GraphicsPipeline.NumRenderTargets = 1;
      create.GraphicsPipeline.RTVFormats[0] = unorm ? dg::TEX_FORMAT_RGBA8_UNORM : dg::TEX_FORMAT_RGBA32_FLOAT;
      create.GraphicsPipeline.DSVFormat = dg::TEX_FORMAT_UNKNOWN;
      create.GraphicsPipeline.PrimitiveTopology = dg::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      create.GraphicsPipeline.RasterizerDesc.CullMode = dg::CULL_MODE_NONE;
      create.GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = false;
      create.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
      create.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
      std::vector<dg::LayoutElement> layout;
      for (const Element& element : vertices.elements) {
        dg::LayoutElement le;
        le.InputIndex = element.slot;
        le.BufferSlot = 0;
        le.RelativeOffset = element.offset;
        le.Stride = vertices.stride;
        if (element.d3dType == D3DDECLTYPE_D3DCOLOR) {
          le.NumComponents = 4;
          le.ValueType = dg::VT_UINT8;
          le.IsNormalized = true;
        } else if (element.d3dType == D3DDECLTYPE_UBYTE4) {
          le.NumComponents = 4;
          le.ValueType = dg::VT_UINT8;
          le.IsNormalized = false;
        } else {
          le.NumComponents = element.bytes / 4;
          le.ValueType = dg::VT_FLOAT32;
          le.IsNormalized = false;
        }
        layout.push_back(le);
      }
      create.GraphicsPipeline.InputLayout.LayoutElements = layout.data();
      create.GraphicsPipeline.InputLayout.NumElements = static_cast<dg::Uint32>(layout.size());
      dg::RefCntAutoPtr<dg::IPipelineState> pso;
      mDg.device->CreateGraphicsPipelineState(create, &pso);
      dg::BufferDesc vbDesc;
      vbDesc.Name = "fxdiff vertices";
      vbDesc.Size = vertices.data.size();
      vbDesc.BindFlags = dg::BIND_VERTEX_BUFFER;
      vbDesc.Usage = dg::USAGE_IMMUTABLE;
      dg::BufferData vbData{vertices.data.data(), vertices.data.size()};
      dg::RefCntAutoPtr<dg::IBuffer> vb;
      mDg.device->CreateBuffer(vbDesc, &vbData, &vb);
      if (!pso || !vb) {
        result.status = "differ: the PSO or vertex buffer was not created";
        ReleaseTarget(target);
        for (IDirect3DBaseTexture9* texture : textures.d3d9) texture->Release();
        return;
      }
      dg::ITextureView* rtv[1] = {target.dg->GetDefaultView(dg::TEXTURE_VIEW_RENDER_TARGET)};
      mDg.context->SetRenderTargets(1, rtv, nullptr, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      mDg.context->ClearRenderTarget(rtv[0], clear, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      dg::Viewport dgViewport;
      dgViewport.Width = static_cast<float>(mOptions.size);
      dgViewport.Height = static_cast<float>(mOptions.size);
      mDg.context->SetViewports(1, &dgViewport, mOptions.size, mOptions.size);
      dg::IBuffer* buffers[1] = {vb};
      const dg::Uint64 offsets[1] = {0};
      mDg.context->SetVertexBuffers(0, 1, buffers, offsets, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, dg::SET_VERTEX_BUFFERS_FLAG_RESET);
      mDg.context->SetPipelineState(pso);
      diligent::DrawState state;
      state.viewport[2] = static_cast<float>(mOptions.size);
      state.viewport[3] = static_cast<float>(mOptions.size);
      state.positionOffset[0] = 1.0F / static_cast<float>(mOptions.size);
      state.positionOffset[1] = -1.0F / static_cast<float>(mOptions.size);
      state.alphaTestEnable = alphaTestEnable;
      state.alphaFunc = alphaFunc;
      state.alphaRef = alphaRef;
      state.frameIndex = ++mFrame;
      if (mutation == "offset") {
        state.positionOffset[0] = state.positionOffset[1] = 0.0F;
      } else if (mutation == "alpharef") {
        state.alphaRef = alphaRef + 1;
      }
      if (!binding->Commit(mDg.context, state)) {
        result.status = "differ: Commit failed";
      } else {
        dg::DrawAttribs draw(vertices.vertexCount, dg::DRAW_FLAG_VERIFY_ALL);
        mDg.context->Draw(draw);
      }
      const Image dgImage = ReadDiligent(target, unorm);

      if (result.status.empty()) {
        result.comparison = Compare(d3d9Image, dgImage, clear, mOptions.tolerance, unorm);
        const bool agree = result.comparison.overTolerance == 0 && result.comparison.coverageMismatch == 0;
        result.status = agree ? (result.comparison.covered == 0 ? "agree (no pixels drawn)" : "agree") : "differ";
        if ((!agree || std::getenv("FXDIFF_DUMP_ALL") != nullptr) && !mOptions.outDir.empty()) {
          const std::string stem = mOptions.outDir + "/" + result.effect + "." + result.technique + ".p" + std::to_string(pass) + "." + result.mode;
          std::ofstream(stem + ".d3d9.f32", std::ios::binary).write(reinterpret_cast<const char*>(d3d9Image.rgba.data()), d3d9Image.rgba.size() * 4);
          std::ofstream(stem + ".dg.f32", std::ios::binary).write(reinterpret_cast<const char*>(dgImage.rgba.data()), dgImage.rgba.size() * 4);
        }
      }
      ReleaseTarget(target);
      for (IDirect3DBaseTexture9* texture : textures.d3d9) {
        texture->Release();
      }
    }

    const Options& mOptions;
    D3D9Side& mD3D9;
    DiligentSide& mDg;
    std::uint64_t mFrame = 0;
  };

  // ---- SPIR-V through Diligent's glslang path -----------------------------------------------------------

  struct SpirvResult
  {
    std::string effect;
    std::string file;
    std::string stage;
    std::string entry;
    bool ok = false;
    std::string messages;
  };

  constexpr char kVulkanDefine[] = "#ifndef VULKAN\n#   define VULKAN 1\n#endif\n"; // ShaderVkImpl.cpp:57-60

  void SpirvEffect(const std::string& name, const std::string& source, const Options& options, std::vector<SpirvResult>& results)
  {
    fx::EffectInput input;
    input.parts.emplace_back(name + ".fx", source);
    const fx::FrontEndResult front = fx::BuildEffectMetadata(input);
    std::vector<fx::Diagnostic> diagnostics;
    const std::unique_ptr<fx::HlslEmitter> emitter = front.ok ? fx::HlslEmitter::Create(input, front.metadata, diagnostics) : nullptr;
    if (!emitter) {
      SpirvResult result;
      result.effect = name;
      result.messages = "effect not parsed";
      results.push_back(result);
      return;
    }
    std::set<std::string> done;
    const fx::DeviceProfile& profile = fx::StandardDeviceProfiles().front();
    for (const fx::TechniqueInfo& technique : front.metadata.techniques) {
      if (!fx::IsTechniqueValid(technique, profile)) {
        continue;
      }
      for (const fx::PassInfo& pass : technique.passes) {
        fx::EmittedStage ps;
        fx::EmittedStage vs;
        std::string psKey = "ps:" + pass.pixelShader.entry;
        for (const std::string& argument : pass.pixelShader.arguments) {
          psKey += "|" + argument;
        }
        bool okPs = pass.pixelShader.kind == fx::ShaderEntry::Kind::Compile ? emitter->EmitPixelShader(pass.pixelShader, ps, diagnostics)
                                                                            : emitter->EmitFixedFunctionPixelShader(ps, diagnostics);
        fx::VertexInputLayout layout;
        bool okVs = false;
        std::string vsKey;
        if (pass.vertexShader.kind == fx::ShaderEntry::Kind::Compile) {
          for (fx::InputKind& kind : layout.kinds) {
            kind = fx::InputKind::Float;
          }
          layout.kinds[fx::kSlotColor0] = fx::InputKind::UNormBgra;
          okVs = okPs && emitter->EmitVertexShader(pass.vertexShader, layout, ps.varyings, vs, diagnostics);
          vsKey = "vs:" + pass.vertexShader.entry + ":" + std::to_string(ps.varyings);
        } else {
          layout.kinds[fx::kSlotPosition] = fx::InputKind::PositionT;
          layout.kinds[fx::kSlotTexcoord0] = fx::InputKind::Float;
          layout.kinds[fx::kSlotTexcoord0 + 1] = fx::InputKind::Float;
          okVs = okPs && emitter->EmitFixedFunctionVertexShader(layout, ps.varyings, vs, diagnostics);
          vsKey = "vs:<fixed-function>:" + std::to_string(ps.varyings);
        }
        const std::pair<std::string, const fx::EmittedStage*> stages[2] = {{vsKey, &vs}, {psKey, &ps}};
        for (const auto& [key, stage] : stages) {
          if (!done.insert(key).second) {
            continue;
          }
          SpirvResult result;
          result.effect = name;
          result.stage = key.substr(0, 2);
          result.entry = key.substr(3);
          result.file = name + "." + result.stage + std::to_string(done.size() - 1) + ".spv";
          if (!okPs || !okVs) {
            result.messages = "not generated";
            results.push_back(result);
            continue;
          }
          dg::ShaderCreateInfo info;
          info.Source = stage->source.c_str();
          info.SourceLength = stage->source.size();
          info.EntryPoint = "main";
          info.SourceLanguage = dg::SHADER_SOURCE_LANGUAGE_HLSL;
          info.HLSLVersion = dg::ShaderVersion{5, 0};
          info.Desc.ShaderType = result.stage == "vs" ? dg::SHADER_TYPE_VERTEX : dg::SHADER_TYPE_PIXEL;
          info.Desc.Name = result.file.c_str();
          dg::RefCntAutoPtr<dg::IDataBlob> output;
          const std::vector<unsigned int> spirv =
            dg::GLSLangUtils::HLSLtoSPIRV(info, dg::GLSLangUtils::SpirvVersion::Vk100, kVulkanDefine, &output);
          result.ok = !spirv.empty();
          if (output && output->GetSize() != 0) {
            result.messages.assign(static_cast<const char*>(output->GetConstDataPtr()), output->GetSize());
            const std::size_t marker = result.messages.find('\0');
            if (marker != std::string::npos) {
              result.messages.resize(marker); // the compiler log; the source follows
            }
          }
          if (result.ok && !options.outDir.empty()) {
            std::ofstream(options.outDir + "/" + result.file, std::ios::binary)
              .write(reinterpret_cast<const char*>(spirv.data()), static_cast<std::streamsize>(spirv.size() * 4));
            std::ofstream(options.outDir + "/" + result.file + ".hlsl", std::ios::binary) << stage->source;
          }
          results.push_back(result);
        }
      }
    }
  }

  int Usage()
  {
    std::cerr << "usage: fxdiff --compat FILE [--out DIR] [--render] [--spirv] [--size N] [--seed S] [--tolerance T] "
                 "[--set NAME=v0,v1,...]... EFFECT.fx...\n";
    return 2;
  }

} // namespace

int main(int argc, char** argv)
{
  Options options;
  std::vector<std::string> effects;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--compat" && i + 1 < argc) {
      options.compatPath = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      options.outDir = argv[++i];
    } else if (arg == "--render") {
      options.render = true;
    } else if (arg == "--spirv") {
      options.spirv = true;
    } else if (arg == "--size" && i + 1 < argc) {
      options.size = static_cast<std::uint32_t>(std::stoul(argv[++i]));
    } else if (arg == "--seed" && i + 1 < argc) {
      options.seed = std::stoull(argv[++i]);
    } else if (arg == "--tolerance" && i + 1 < argc) {
      options.tolerance = std::stod(argv[++i]);
    } else if (arg == "--set" && i + 1 < argc) {
      const std::string assignment = argv[++i];
      const std::size_t equals = assignment.find('=');
      if (equals == std::string::npos) {
        return Usage();
      }
      std::vector<float> values;
      std::stringstream list(assignment.substr(equals + 1));
      std::string item;
      while (std::getline(list, item, ',')) {
        values.push_back(std::stof(item));
      }
      options.overrides[assignment.substr(0, equals)] = values;
    } else if (!arg.empty() && arg[0] == '-') {
      return Usage();
    } else {
      effects.push_back(arg);
    }
  }
  if (effects.empty() || (!options.render && !options.spirv)) {
    return Usage();
  }
  std::string compat;
  if (!options.compatPath.empty() && !ReadFile(options.compatPath, compat)) {
    std::cerr << "fxdiff: cannot read " << options.compatPath << "\n";
    return 2;
  }
  diligent::SetEffectLog(&OnEffectLog);

  std::vector<PassResult> passResults;
  std::vector<SpirvResult> spirvResults;
  bool setupFailed = false;
  if (options.spirv) {
    dg::GLSLangUtils::InitializeGlslang();
    for (const std::string& path : effects) {
      std::string text;
      if (!ReadFile(path, text)) {
        std::cerr << "fxdiff: cannot read " << path << "\n";
        return 2;
      }
      SpirvEffect(Stem(path), compat + text, options, spirvResults);
    }
    dg::GLSLangUtils::FinalizeGlslang();
  }
  if (options.render) {
    D3D9Side d3d9;
    DiligentSide dgSide;
    std::string error;
    if (!d3d9.Create(error) || !dgSide.Create(error)) {
      std::cerr << "fxdiff: " << error << "\n";
      setupFailed = true;
    } else {
      diligent::SetEffectRenderDevice(dgSide.device);
      Runner runner(options, d3d9, dgSide);
      for (const std::string& path : effects) {
        std::string text;
        if (!ReadFile(path, text)) {
          std::cerr << "fxdiff: cannot read " << path << "\n";
          return 2;
        }
        runner.RunEffect(Stem(path), compat + text, passResults);
      }
      diligent::SetEffectRenderDevice(nullptr);
    }
    d3d9.Destroy();
  }

  // Report.
  std::size_t differ = 0;
  std::size_t agree = 0;
  std::size_t skipped = 0;
  std::size_t vacuous = 0;
  for (const PassResult& result : passResults) {
    if (result.status.rfind("agree", 0) == 0) {
      ++agree;
      vacuous += result.status != "agree" ? 1U : 0U;
    } else if (result.status.rfind("skipped", 0) == 0) {
      ++skipped;
    } else {
      ++differ;
    }
    std::printf("%-12s %-32s P%d %-5s %-8s max|d| %.3g over %u coverage %u drawn %u  vs %s ps %s\n", result.effect.c_str(),
                result.technique.c_str(), result.pass, result.mode.c_str(), result.status.c_str(), result.comparison.maxDelta,
                result.comparison.overTolerance, result.comparison.coverageMismatch, result.comparison.covered, result.vs.c_str(),
                result.ps.c_str());
  }
  std::size_t spirvOk = 0;
  for (const SpirvResult& result : spirvResults) {
    spirvOk += result.ok ? 1U : 0U;
    if (!result.ok) {
      std::printf("SPIR-V FAILED %s %s %s: %s\n", result.effect.c_str(), result.stage.c_str(), result.entry.c_str(), result.messages.c_str());
    }
  }
  const diligent::EffectGpuStats stats = diligent::GetEffectGpuStats();
  std::printf("D3DX setter failures: %d\n", gSetterFailures);
  std::printf("render: %zu agree (%zu with no pixels drawn), %zu differ, %zu skipped; spirv: %zu/%zu; diligent errors: %d; "
              "programs %llu, shaders %llu, generation failures %llu\n",
              agree, vacuous, differ, skipped, spirvOk, spirvResults.size(), gDiligentErrors,
              static_cast<unsigned long long>(stats.programsCompiled), static_cast<unsigned long long>(stats.shadersCompiled),
              static_cast<unsigned long long>(stats.generationFailures));

  if (!options.outDir.empty()) {
    std::ofstream json(options.outDir + "/fxdiff.json", std::ios::binary);
    json << "{\n  \"size\": " << options.size << ", \"seed\": " << options.seed << ", \"tolerance\": " << options.tolerance
         << ",\n  \"diligentErrors\": " << gDiligentErrors << ", \"setterFailures\": " << gSetterFailures
         << ",\n  \"diligentMessages\": [";
    for (std::size_t i = 0; i < gDiligentMessages.size(); ++i) {
      json << (i != 0 ? ", " : "") << Json(gDiligentMessages[i]);
    }
    json << "],\n  \"passes\": [\n";
    for (std::size_t i = 0; i < passResults.size(); ++i) {
      const PassResult& r = passResults[i];
      json << "    {\"effect\": " << Json(r.effect) << ", \"technique\": " << Json(r.technique) << ", \"pass\": " << r.pass
           << ", \"mode\": " << Json(r.mode) << ", \"vs\": " << Json(r.vs) << ", \"ps\": " << Json(r.ps) << ", \"status\": " << Json(r.status)
           << ", \"maxDelta\": " << r.comparison.maxDelta << ", \"overTolerance\": " << r.comparison.overTolerance
           << ", \"coverageMismatch\": " << r.comparison.coverageMismatch << ", \"drawn\": " << r.comparison.covered << "}"
           << (i + 1 < passResults.size() ? "," : "") << "\n";
    }
    json << "  ],\n  \"spirv\": [\n";
    for (std::size_t i = 0; i < spirvResults.size(); ++i) {
      const SpirvResult& r = spirvResults[i];
      json << "    {\"effect\": " << Json(r.effect) << ", \"file\": " << Json(r.file) << ", \"stage\": " << Json(r.stage)
           << ", \"entry\": " << Json(r.entry) << ", \"ok\": " << (r.ok ? "true" : "false") << ", \"messages\": " << Json(r.messages) << "}"
           << (i + 1 < spirvResults.size() ? "," : "") << "\n";
    }
    json << "  ]\n}\n";
  }
  if (setupFailed) {
    return 2;
  }
  return (differ == 0 && spirvOk == spirvResults.size() && gDiligentErrors == 0 && gSetterFailures == 0) ? 0 : 1;
}
