// fxd3dx_dump: what D3DX reports about an effect, as JSON in the schema
// fxmeta writes (gpg/gal/fx/FxJson.h). This is the oracle the front end is
// checked against (scripts/port/fx_metadata_gate.py).
//
//   fxd3dx_dump [--compat d3d9states.compat] [-D NAME[=VALUE]]... [--out FILE] [--hal-caps] [--x87 24|53|64]
//               effect.fx
//
// The effect goes through the same calls as DeviceD3D9::CreateEffectFromSourceBuffer
// (0x008F09A0, D3D9Interfaces.cpp:1522-1600) in main.exe, with the same
// d3dx9_43 (DXSDK_D3DX d3dx9.lib) and flags:
//   D3DXCreateEffectCompiler(compat + fx, macros, no include handler,
//                            D3DXSHADER_DEBUG | D3DXSHADER_USE_LEGACY_D3DX9_31_DLL
//                            [| D3DXSHADER_AVOID_FLOW_CONTROL with FAF_BONE_TEXTURE])
//   ->CompileEffect(D3DXSHADER_DEBUG [| AVOID_FLOW_CONTROL])
//   D3DXCreateEffect(device, blob, macros, NULL, D3DXSHADER_DEBUG, NULL pool)
// on a FakeD3D9Device (FakeD3D9Device.h) instead of a real device. Then:
// - parameters: GetParameterDesc, GetValue, GetString, annotations, struct members;
// - techniques/passes: descs, annotations; the shaders' bytecode version and
//   entry point name (the D3DXSHADER_DEBUG info in the bytecode);
// - pass states: Begin(D3DXFX_DONOTSAVESTATE, as D3D9Interfaces.cpp:4102) and
//   BeginPass through an ID3DXEffectStateManager that records every call;
//   SetTexture/SetSamplerState calls for the registers a pass shader's
//   constant table assigns to a sampler parameter are that sampler's states;
// - validity: ValidateTechnique and the FindNextValidTechnique order, once per
//   device profile (the fake device refuses shaders above the profile).
//
// --hal-caps reports this PC's adapter caps to D3DX and makes the "all"
// profile accept what the adapter accepts (IDirect3D9::GetDeviceCaps only, no
// device). --x87 sets the x87 precision D3DX runs under, _PC_24 by default as
// in main.exe; it made no difference on any effect of the corpus.
//
// Exit code 0 on success, 1 when D3DX rejects the effect (its messages on
// stderr), 2 on bad usage or unreadable files.

// This TU defines the D3D9 and D3DX interface GUIDs (selectany), so no
// dxguid.lib is needed.
#include <initguid.h>

#include <d3dx9.h>

#include <float.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "FakeD3D9Device.h"
#include "gpg/gal/fx/FxJson.h"
#include "gpg/gal/fx/FxMetadata.h"

namespace {

  using gpg::gal::fx::JsonWriter;

  bool ReadFile(const std::string& path, std::string& text)
  {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      return false;
    }
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
  }

  std::string BufferText(ID3DXBuffer* buffer)
  {
    if (buffer == nullptr) {
      return {};
    }
    const char* text = static_cast<const char*>(buffer->GetBufferPointer());
    return std::string(text, strnlen(text, buffer->GetBufferSize()));
  }

  template <class T>
  void SafeRelease(T*& object)
  {
    if (object != nullptr) {
      object->Release();
      object = nullptr;
    }
  }

  // ---- shader bytecode ----------------------------------------------------------

  struct ShaderInfo
  {
    DWORD version = 0;
    std::string entry; // from the D3DXSHADER_DEBUG info
    std::map<UINT, std::string> samplers; // sampler register -> parameter name
  };

  // The comment blocks follow the version token. With D3DXSHADER_DEBUG the
  // compiler writes a 'DBUG' block whose header is ten DWORDs; the tenth is
  // the offset of the entry point name from the start of the header
  // (measured: "PrimBatcherVS", "LifeBarPS" ... for primbatcher.fx).
  ShaderInfo InspectShader(const DWORD* function)
  {
    ShaderInfo info;
    if (function == nullptr) {
      return info;
    }
    info.version = function[0];
    const UINT size = D3DXGetShaderSize(function) / sizeof(DWORD);
    for (UINT i = 1; i < size;) {
      const DWORD token = function[i];
      if ((token & 0xFFFFu) != 0xFFFEu) {
        break;
      }
      const UINT length = (token >> 16) & 0x7FFFu;
      if (length >= 11 && function[i + 1] == MAKEFOURCC('D', 'B', 'U', 'G')) {
        const char* header = reinterpret_cast<const char*>(&function[i + 2]);
        const UINT headerBytes = (length - 1) * sizeof(DWORD);
        const DWORD entryOffset = function[i + 2 + 9];
        if (entryOffset < headerBytes) {
          info.entry.assign(header + entryOffset, strnlen(header + entryOffset, headerBytes - entryOffset));
        }
      }
      i += 1 + length;
    }
    ID3DXConstantTable* constants = nullptr;
    if (SUCCEEDED(D3DXGetShaderConstantTable(function, &constants))) {
      D3DXCONSTANTTABLE_DESC tableDesc{};
      constants->GetDesc(&tableDesc);
      for (UINT c = 0; c < tableDesc.Constants; ++c) {
        const D3DXHANDLE handle = constants->GetConstant(nullptr, c);
        D3DXCONSTANT_DESC desc{};
        UINT count = 1;
        if (FAILED(constants->GetConstantDesc(handle, &desc, &count)) || desc.RegisterSet != D3DXRS_SAMPLER) {
          continue;
        }
        for (UINT r = 0; r < desc.RegisterCount; ++r) {
          info.samplers[desc.RegisterIndex + r] = desc.Name;
        }
      }
      constants->Release();
    }
    return info;
  }

  // ---- state capture --------------------------------------------------------------

  struct Event
  {
    enum class Kind
    {
      RenderState,
      TextureStageState,
      SamplerState,
      Texture,
      VertexShader,
      PixelShader,
      Constant,
      Other
    };

    Kind kind = Kind::Other;
    DWORD a = 0;
    DWORD b = 0;
    DWORD c = 0;
    const void* object = nullptr;
    std::string other;
  };

  class CaptureStateManager final : public ID3DXEffectStateManager
  {
  public:
    std::vector<Event> events;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
    {
      if (riid == IID_IUnknown || riid == IID_ID3DXEffectStateManager) {
        *object = this;
        return S_OK;
      }
      *object = nullptr;
      return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE, CONST D3DMATRIX*) override { return Other("Transform"); }
    HRESULT STDMETHODCALLTYPE SetMaterial(CONST D3DMATERIAL9*) override { return Other("Material"); }
    HRESULT STDMETHODCALLTYPE SetLight(DWORD, CONST D3DLIGHT9*) override { return Other("Light"); }
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD, BOOL) override { return Other("LightEnable"); }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE state, DWORD value) override
    {
      return Add(Event::Kind::RenderState, 0, state, value);
    }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage, LPDIRECT3DBASETEXTURE9 texture) override
    {
      Event event;
      event.kind = Event::Kind::Texture;
      event.a = stage;
      event.object = texture;
      events.push_back(event);
      return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) override
    {
      return Add(Event::Kind::TextureStageState, stage, type, value);
    }
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value) override
    {
      return Add(Event::Kind::SamplerState, sampler, type, value);
    }
    HRESULT STDMETHODCALLTYPE SetNPatchMode(FLOAT) override { return Other("NPatchMode"); }
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD) override { return Other("FVF"); }
    HRESULT STDMETHODCALLTYPE SetVertexShader(LPDIRECT3DVERTEXSHADER9 shader) override
    {
      Event event;
      event.kind = Event::Kind::VertexShader;
      event.object = shader;
      events.push_back(event);
      return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT, CONST FLOAT*, UINT) override { return Constant(); }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT, CONST INT*, UINT) override { return Constant(); }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT, CONST BOOL*, UINT) override { return Constant(); }
    HRESULT STDMETHODCALLTYPE SetPixelShader(LPDIRECT3DPIXELSHADER9 shader) override
    {
      Event event;
      event.kind = Event::Kind::PixelShader;
      event.object = shader;
      events.push_back(event);
      return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT, CONST FLOAT*, UINT) override { return Constant(); }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT, CONST INT*, UINT) override { return Constant(); }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT, CONST BOOL*, UINT) override { return Constant(); }

  private:
    HRESULT Add(const Event::Kind kind, const DWORD a, const DWORD b, const DWORD c)
    {
      Event event;
      event.kind = kind;
      event.a = a;
      event.b = b;
      event.c = c;
      events.push_back(event);
      return D3D_OK;
    }
    HRESULT Constant()
    {
      Event event;
      event.kind = Event::Kind::Constant;
      events.push_back(event);
      return D3D_OK;
    }
    HRESULT Other(const char* name)
    {
      Event event;
      event.kind = Event::Kind::Other;
      event.other = name;
      events.push_back(event);
      return D3D_OK;
    }
  };

  // ---- reflection ---------------------------------------------------------------------

  std::string WordsHex(const void* data, const UINT bytes)
  {
    std::vector<std::uint32_t> words(bytes / 4);
    if (!words.empty()) {
      std::memcpy(words.data(), data, words.size() * 4);
    }
    return gpg::gal::fx::WordsToHex(words);
  }

  void WriteDesc(JsonWriter& json, const D3DXPARAMETER_DESC& desc)
  {
    json.Key("name");
    json.String(desc.Name != nullptr ? desc.Name : "");
    json.Key("semantic");
    if (desc.Semantic != nullptr) {
      json.String(desc.Semantic);
    } else {
      json.Null();
    }
    json.Key("class");
    json.Number(desc.Class);
    json.Key("type");
    json.Number(desc.Type);
    json.Key("rows");
    json.Number(desc.Rows);
    json.Key("columns");
    json.Number(desc.Columns);
    json.Key("elements");
    json.Number(desc.Elements);
    json.Key("structMembers");
    json.Number(desc.StructMembers);
    json.Key("flags");
    json.Number(desc.Flags);
    json.Key("bytes");
    json.Number(desc.Bytes);
  }

  void WriteAnnotations(JsonWriter& json, ID3DXEffect* effect, const D3DXHANDLE owner, const UINT count)
  {
    json.Key("annotations");
    json.BeginArray();
    for (UINT i = 0; i < count; ++i) {
      const D3DXHANDLE annotation = effect->GetAnnotation(owner, i);
      D3DXPARAMETER_DESC desc{};
      effect->GetParameterDesc(annotation, &desc);
      json.BeginObject();
      WriteDesc(json, desc);
      if (desc.Type == D3DXPT_STRING) {
        LPCSTR text = nullptr;
        effect->GetString(annotation, &text);
        json.Key("string");
        json.String(text != nullptr ? text : "");
      } else {
        std::vector<BYTE> data(desc.Bytes);
        effect->GetValue(annotation, data.data(), desc.Bytes);
        json.Key("value");
        json.String(WordsHex(data.data(), desc.Bytes));
      }
      json.EndObject();
    }
    json.EndArray();
  }

  struct SamplerObservation
  {
    std::string texture; // empty: NULL texture
    std::vector<std::pair<DWORD, DWORD>> states;
  };

  struct PassCapture
  {
    std::vector<Event> states; // render / stage / pass-level sampler and texture
    std::optional<const void*> vertexShader; // unset: not assigned
    std::optional<const void*> pixelShader;
  };

  struct Dump
  {
    std::map<const void*, std::string> textureNames;
    std::map<std::string, SamplerObservation> samplers;
    std::set<std::string> inconsistentSamplers;
  };

  // Splits one BeginPass's calls into pass states and the states of the
  // sampler parameters its shaders use.
  PassCapture SplitPass(const std::vector<Event>& events, const ShaderInfo& vs, const ShaderInfo& ps, Dump& dump)
  {
    PassCapture pass;
    const std::map<UINT, std::string>* samplers = nullptr;
    UINT registerBase = 0;
    std::map<std::string, SamplerObservation> observed;
    for (const Event& event : events) {
      switch (event.kind) {
      case Event::Kind::VertexShader:
        pass.vertexShader = event.object;
        samplers = &vs.samplers;
        registerBase = D3DVERTEXTEXTURESAMPLER0;
        continue;
      case Event::Kind::PixelShader:
        pass.pixelShader = event.object;
        samplers = &ps.samplers;
        registerBase = 0;
        continue;
      case Event::Kind::Constant:
        continue;
      case Event::Kind::Texture:
      case Event::Kind::SamplerState: {
        if (samplers != nullptr && event.a >= registerBase) {
          const auto found = samplers->find(event.a - registerBase);
          if (found != samplers->end()) {
            SamplerObservation& sampler = observed[found->second];
            if (event.kind == Event::Kind::Texture) {
              const auto name = dump.textureNames.find(event.object);
              sampler.texture = (name != dump.textureNames.end()) ? name->second : std::string();
            } else {
              sampler.states.emplace_back(event.b, event.c);
            }
            continue;
          }
        }
        pass.states.push_back(event);
        continue;
      }
      default:
        pass.states.push_back(event);
        continue;
      }
    }
    for (const auto& [name, sampler] : observed) {
      const auto previous = dump.samplers.find(name);
      if (previous == dump.samplers.end()) {
        dump.samplers[name] = sampler;
      } else if (previous->second.texture != sampler.texture || previous->second.states != sampler.states) {
        dump.inconsistentSamplers.insert(name);
      }
    }
    return pass;
  }

  void WriteShader(JsonWriter& json, const char* key, const std::optional<const void*>& set, const ShaderInfo& info)
  {
    if (!set) {
      return;
    }
    json.Key(key);
    if (*set == nullptr) {
      json.Null();
      return;
    }
    json.BeginObject();
    json.Key("version");
    json.String(gpg::gal::fx::VersionTokenName(info.version));
    json.Key("entry");
    json.String(info.entry);
    // The sampler parameters the shader's constant table binds; a sampler
    // passed as a uniform argument (primbatcher.fx PrimBatcherPS(LinearSampler))
    // shows up here under the parameter's name.
    json.Key("samplers");
    json.BeginArray();
    std::set<std::string> names;
    for (const auto& [reg, name] : info.samplers) {
      if (names.insert(name).second) {
        json.String(name);
      }
    }
    json.EndArray();
    json.EndObject();
  }

  struct Validity
  {
    const char* profile;
    std::vector<bool> valid;
    std::vector<std::string> order;
    std::vector<bool> orderNamed;
  };

  int Usage()
  {
    std::cerr << "usage: fxd3dx_dump [--compat FILE] [-D NAME[=VALUE]]... [--out FILE] [--hal-caps] [--x87 24|53|64]"
                 " EFFECT.fx\n";
    return 2;
  }

} // namespace

int main(int argc, char** argv)
{
  std::string compatPath;
  std::string effectPath;
  std::string outPath;
  bool halCaps = false;
  unsigned int precision = _PC_24;
  std::vector<std::pair<std::string, std::string>> macroText;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--hal-caps") {
      halCaps = true;
    } else if (arg == "--x87" && i + 1 < argc) {
      const std::string bits = argv[++i];
      precision = (bits == "24") ? _PC_24 : (bits == "53") ? _PC_53 : (bits == "64") ? _PC_64 : 0xFFFFFFFFu;
      if (precision == 0xFFFFFFFFu) {
        return Usage();
      }
    } else if (arg == "--compat" && i + 1 < argc) {
      compatPath = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      outPath = argv[++i];
    } else if (arg.rfind("-D", 0) == 0) {
      std::string definition = arg.substr(2);
      if (definition.empty() && i + 1 < argc) {
        definition = argv[++i];
      }
      const std::size_t equals = definition.find('=');
      macroText.emplace_back(definition.substr(0, equals),
                             (equals == std::string::npos) ? "1" : definition.substr(equals + 1));
    } else if (!arg.empty() && arg[0] == '-') {
      return Usage();
    } else if (effectPath.empty()) {
      effectPath = arg;
    } else {
      return Usage();
    }
  }
  if (effectPath.empty()) {
    return Usage();
  }

  // The x87 precision D3DX runs under. main.exe creates its D3D9 device
  // without D3DCREATE_FPU_PRESERVE (behaviour flags 0x44,
  // D3D9Interfaces.cpp:1772), so D3D9 switches the thread to single
  // precision, and the main loop sets _PC_24 itself (WinApp.cpp:2773). The
  // legacy compiler folds constants on the x87, so its results depend on it.
  unsigned int controlWord = 0;
  if (_controlfp_s(&controlWord, precision, _MCW_PC) != 0) {
    std::cerr << "fxd3dx_dump: cannot set the x87 precision\n";
    return 2;
  }

  // compat + fx, byte for byte (CD3DEffectTechnique.cpp:459-476).
  std::string source;
  if (!compatPath.empty() && !ReadFile(compatPath, source)) {
    std::cerr << "fxd3dx_dump: cannot read " << compatPath << "\n";
    return 2;
  }
  std::string effectText;
  if (!ReadFile(effectPath, effectText)) {
    std::cerr << "fxd3dx_dump: cannot read " << effectPath << "\n";
    return 2;
  }
  source += effectText;

  std::vector<D3DXMACRO> macros;
  bool boneTexture = false;
  for (const auto& [name, value] : macroText) {
    macros.push_back({name.c_str(), value.c_str()});
    boneTexture = boneTexture || name == "FAF_BONE_TEXTURE";
  }
  macros.push_back({nullptr, nullptr});
  const D3DXMACRO* defines = (macros.size() > 1) ? macros.data() : nullptr;

  // D3D9Interfaces.cpp:1544-1559.
  const DWORD flowControl = boneTexture ? D3DXSHADER_AVOID_FLOW_CONTROL : 0;
  ID3DXEffectCompiler* compiler = nullptr;
  ID3DXBuffer* errors = nullptr;
  HRESULT hr = D3DXCreateEffectCompiler(source.data(), static_cast<UINT>(source.size()), defines, nullptr,
                                        D3DXSHADER_DEBUG | D3DXSHADER_USE_LEGACY_D3DX9_31_DLL | flowControl,
                                        &compiler, &errors);
  if (FAILED(hr)) {
    std::cerr << BufferText(errors);
    std::cerr << "fxd3dx_dump: D3DXCreateEffectCompiler failed: 0x" << std::hex << static_cast<unsigned>(hr) << "\n";
    return 1;
  }
  SafeRelease(errors);
  ID3DXBuffer* compiled = nullptr;
  hr = compiler->CompileEffect(D3DXSHADER_DEBUG | flowControl, &compiled, &errors);
  if (FAILED(hr)) {
    std::cerr << BufferText(errors);
    std::cerr << "fxd3dx_dump: CompileEffect failed: 0x" << std::hex << static_cast<unsigned>(hr) << "\n";
    return 1;
  }
  const std::string warnings = BufferText(errors);
  SafeRelease(errors);
  SafeRelease(compiler);

  const std::vector<gpg::gal::fx::DeviceProfile>& profiles = gpg::gal::fx::StandardDeviceProfiles();
  std::set<std::string> unexpected;

  // --hal-caps: the caps of this PC's adapter, as the D3D9 backend's device
  // would report them. Only IDirect3D9::GetDeviceCaps is called; no device
  // or window is created.
  D3DCAPS9 adapterCaps{};
  std::string capsSource = "synthetic shader model 3 device";
  if (halCaps) {
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    D3DADAPTER_IDENTIFIER9 identifier{};
    if (d3d == nullptr || FAILED(d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &adapterCaps)) ||
        FAILED(d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &identifier))) {
      std::cerr << "fxd3dx_dump: cannot read the adapter's D3D9 caps\n";
      return 2;
    }
    d3d->Release();
    char versions[64];
    std::snprintf(versions, sizeof(versions), ", vs %u.%u ps %u.%u",
                  static_cast<unsigned>((adapterCaps.VertexShaderVersion >> 8) & 0xFF),
                  static_cast<unsigned>(adapterCaps.VertexShaderVersion & 0xFF),
                  static_cast<unsigned>((adapterCaps.PixelShaderVersion >> 8) & 0xFF),
                  static_cast<unsigned>(adapterCaps.PixelShaderVersion & 0xFF));
    capsSource = std::string("HAL caps of ") + identifier.Description + versions;
  }

  // Validity per device profile: the device refuses shaders above the
  // profile when D3DXCreateEffect creates them, so each profile gets its own
  // effect.
  std::vector<Validity> validity;
  for (const gpg::gal::fx::DeviceProfile& profile : profiles) {
    fxd3dx::DeviceLimits limits;
    limits.maxVertexShader = profile.maxVertexShader;
    limits.maxPixelShader = profile.maxPixelShader;
    if (halCaps) {
      limits.caps = &adapterCaps;
      if (&profile == &profiles.front()) {
        // "all" becomes "what this adapter accepts".
        limits.maxVertexShader = adapterCaps.VertexShaderVersion & 0xFFFFu;
        limits.maxPixelShader = adapterCaps.PixelShaderVersion & 0xFFFFu;
      }
    }
    fxd3dx::FakeD3D9Device device(limits);
    ID3DXEffect* effect = nullptr;
    hr = D3DXCreateEffect(&device, compiled->GetBufferPointer(), compiled->GetBufferSize(), defines, nullptr,
                          D3DXSHADER_DEBUG, nullptr, &effect, &errors);
    if (FAILED(hr)) {
      std::cerr << BufferText(errors);
      std::cerr << "fxd3dx_dump: D3DXCreateEffect failed for profile " << profile.name << ": 0x" << std::hex
                << static_cast<unsigned>(hr) << "\n";
      return 1;
    }
    SafeRelease(errors);
    D3DXEFFECT_DESC effectDesc{};
    effect->GetDesc(&effectDesc);
    Validity entry;
    entry.profile = profile.name;
    for (UINT t = 0; t < effectDesc.Techniques; ++t) {
      entry.valid.push_back(SUCCEEDED(effect->ValidateTechnique(effect->GetTechnique(t))));
    }
    D3DXHANDLE current = nullptr;
    for (UINT guard = 0; guard <= effectDesc.Techniques; ++guard) {
      D3DXHANDLE next = nullptr;
      if (FAILED(effect->FindNextValidTechnique(current, &next)) || next == nullptr) {
        break;
      }
      D3DXTECHNIQUE_DESC desc{};
      effect->GetTechniqueDesc(next, &desc);
      entry.order.push_back(desc.Name != nullptr ? desc.Name : "");
      entry.orderNamed.push_back(desc.Name != nullptr);
      current = next;
    }
    validity.push_back(entry);
    unexpected.insert(device.UnexpectedCalls().begin(), device.UnexpectedCalls().end());
    effect->Release();
  }

  // The full dump, on a device that accepts every shader.
  fxd3dx::FakeD3D9Device device(fxd3dx::DeviceLimits{});
  ID3DXEffect* effect = nullptr;
  hr = D3DXCreateEffect(&device, compiled->GetBufferPointer(), compiled->GetBufferSize(), defines, nullptr,
                        D3DXSHADER_DEBUG, nullptr, &effect, &errors);
  if (FAILED(hr)) {
    std::cerr << BufferText(errors);
    std::cerr << "fxd3dx_dump: D3DXCreateEffect failed: 0x" << std::hex << static_cast<unsigned>(hr) << "\n";
    return 1;
  }
  SafeRelease(errors);
  D3DXEFFECT_DESC effectDesc{};
  effect->GetDesc(&effectDesc);

  JsonWriter json;
  Dump dump;
  std::vector<std::unique_ptr<fxd3dx::FakeTexture>> textures;
  json.BeginObject();
  json.Key("schema");
  json.String("faf-fx-metadata/1");
  json.Key("producer");
  json.String("d3dx9_43");
  json.Key("deviceCaps");
  json.String(capsSource);
  json.Key("x87Precision");
  json.Number(precision == _PC_24 ? 24 : precision == _PC_53 ? 53 : 64);
  if (!warnings.empty()) {
    json.Key("compilerMessages");
    json.String(warnings);
  }

  // One fake texture per texture parameter, so SetTexture names its source.
  std::vector<D3DXPARAMETER_DESC> parameterDescs;
  for (UINT i = 0; i < effectDesc.Parameters; ++i) {
    const D3DXHANDLE handle = effect->GetParameter(nullptr, i);
    D3DXPARAMETER_DESC desc{};
    effect->GetParameterDesc(handle, &desc);
    parameterDescs.push_back(desc);
    if (desc.Type >= D3DXPT_TEXTURE && desc.Type <= D3DXPT_TEXTURECUBE) {
      const D3DRESOURCETYPE type = (desc.Type == D3DXPT_TEXTURECUBE) ? D3DRTYPE_CUBETEXTURE
                                   : (desc.Type == D3DXPT_TEXTURE3D) ? D3DRTYPE_VOLUMETEXTURE
                                                                     : D3DRTYPE_TEXTURE;
      textures.push_back(std::make_unique<fxd3dx::FakeTexture>(&device, desc.Name, type));
      dump.textureNames[textures.back().get()] = desc.Name;
      effect->SetTexture(handle, textures.back().get());
    }
  }

  CaptureStateManager capture;
  effect->SetStateManager(&capture);
  std::vector<std::vector<PassCapture>> captures(effectDesc.Techniques);
  std::vector<std::vector<std::pair<ShaderInfo, ShaderInfo>>> shaders(effectDesc.Techniques);
  for (UINT t = 0; t < effectDesc.Techniques; ++t) {
    const D3DXHANDLE technique = effect->GetTechnique(t);
    D3DXTECHNIQUE_DESC techniqueDesc{};
    effect->GetTechniqueDesc(technique, &techniqueDesc);
    for (UINT k = 0; k < techniqueDesc.Passes; ++k) {
      D3DXPASS_DESC passDesc{};
      effect->GetPassDesc(effect->GetPass(technique, k), &passDesc);
      shaders[t].emplace_back(InspectShader(passDesc.pVertexShaderFunction),
                              InspectShader(passDesc.pPixelShaderFunction));
    }
    effect->SetTechnique(technique);
    UINT passes = 0;
    if (FAILED(effect->Begin(&passes, D3DXFX_DONOTSAVESTATE))) {
      std::cerr << "fxd3dx_dump: Begin failed for technique " << t << "\n";
      return 1;
    }
    for (UINT k = 0; k < passes; ++k) {
      capture.events.clear();
      if (FAILED(effect->BeginPass(k))) {
        std::cerr << "fxd3dx_dump: BeginPass failed for technique " << t << " pass " << k << "\n";
        return 1;
      }
      captures[t].push_back(SplitPass(capture.events, shaders[t][k].first, shaders[t][k].second, dump));
      effect->EndPass();
    }
    effect->End();
  }
  effect->SetStateManager(nullptr);

  json.Key("parameters");
  json.BeginArray();
  for (UINT i = 0; i < effectDesc.Parameters; ++i) {
    const D3DXHANDLE handle = effect->GetParameter(nullptr, i);
    const D3DXPARAMETER_DESC& desc = parameterDescs[i];
    json.BeginObject();
    WriteDesc(json, desc);
    WriteAnnotations(json, effect, handle, desc.Annotations);
    if (desc.Class != D3DXPC_OBJECT) {
      std::vector<BYTE> data(desc.Bytes);
      effect->GetValue(handle, data.data(), desc.Bytes);
      json.Key("value");
      json.String(WordsHex(data.data(), desc.Bytes));
    }
    if (desc.Type == D3DXPT_STRING) {
      LPCSTR text = nullptr;
      effect->GetString(handle, &text);
      json.Key("string");
      json.String(text != nullptr ? text : "");
    }
    if (desc.Class == D3DXPC_STRUCT) {
      const D3DXHANDLE element = (desc.Elements > 0) ? effect->GetParameterElement(handle, 0) : handle;
      json.Key("members");
      json.BeginArray();
      for (UINT m = 0; m < desc.StructMembers; ++m) {
        D3DXPARAMETER_DESC member{};
        effect->GetParameterDesc(effect->GetParameter(element, m), &member);
        json.BeginObject();
        WriteDesc(json, member);
        json.EndObject();
      }
      json.EndArray();
    }
    const auto sampler = dump.samplers.find(desc.Name);
    if (desc.Type >= D3DXPT_SAMPLER && desc.Type <= D3DXPT_SAMPLERCUBE && sampler != dump.samplers.end()) {
      json.Key("sampler");
      json.BeginObject();
      json.Key("texture");
      if (sampler->second.texture.empty()) {
        json.Null();
      } else {
        json.String(sampler->second.texture);
      }
      json.Key("states");
      json.BeginArray();
      for (const auto& [state, value] : sampler->second.states) {
        json.BeginArray();
        json.Number(state);
        json.Number(value);
        json.EndArray();
      }
      json.EndArray();
      if (dump.inconsistentSamplers.count(desc.Name) != 0) {
        json.Key("inconsistent");
        json.Bool(true);
      }
      json.EndObject();
    }
    json.EndObject();
  }
  json.EndArray();

  json.Key("techniques");
  json.BeginArray();
  for (UINT t = 0; t < effectDesc.Techniques; ++t) {
    const D3DXHANDLE technique = effect->GetTechnique(t);
    D3DXTECHNIQUE_DESC techniqueDesc{};
    effect->GetTechniqueDesc(technique, &techniqueDesc);
    json.BeginObject();
    json.Key("name");
    if (techniqueDesc.Name != nullptr) {
      json.String(techniqueDesc.Name);
    } else {
      json.Null();
    }
    WriteAnnotations(json, effect, technique, techniqueDesc.Annotations);
    json.Key("valid");
    json.BeginObject();
    for (const Validity& entry : validity) {
      json.Key(entry.profile);
      json.Bool(t < entry.valid.size() && entry.valid[t]);
    }
    json.EndObject();
    json.Key("passes");
    json.BeginArray();
    for (UINT k = 0; k < techniqueDesc.Passes; ++k) {
      const D3DXHANDLE passHandle = effect->GetPass(technique, k);
      D3DXPASS_DESC passDesc{};
      effect->GetPassDesc(passHandle, &passDesc);
      json.BeginObject();
      json.Key("name");
      if (passDesc.Name != nullptr) {
        json.String(passDesc.Name);
      } else {
        json.Null();
      }
      WriteAnnotations(json, effect, passHandle, passDesc.Annotations);
      json.Key("states");
      json.BeginArray();
      if (k < captures[t].size()) {
        for (const Event& event : captures[t][k].states) {
          json.BeginObject();
          json.Key("op");
          switch (event.kind) {
          case Event::Kind::RenderState: json.String("render"); break;
          case Event::Kind::TextureStageState: json.String("stage"); break;
          case Event::Kind::SamplerState: json.String("sampler"); break;
          case Event::Kind::Texture: json.String("texture"); break;
          default: json.String("other:" + event.other); break;
          }
          json.Key("index");
          json.Number(event.kind == Event::Kind::RenderState ? 0 : event.a);
          json.Key("state");
          json.Number(event.kind == Event::Kind::Texture ? 0 : event.b);
          if (event.kind == Event::Kind::Texture) {
            const auto name = dump.textureNames.find(event.object);
            json.Key("texture");
            if (name == dump.textureNames.end()) {
              json.Null();
            } else {
              json.String(name->second);
            }
          } else {
            json.Key("value");
            json.Number(event.c);
          }
          json.EndObject();
        }
      }
      json.EndArray();
      if (k < captures[t].size()) {
        WriteShader(json, "vertexShader", captures[t][k].vertexShader, shaders[t][k].first);
        WriteShader(json, "pixelShader", captures[t][k].pixelShader, shaders[t][k].second);
      }
      json.EndObject();
    }
    json.EndArray();
    json.EndObject();
  }
  json.EndArray();
  unexpected.insert(device.UnexpectedCalls().begin(), device.UnexpectedCalls().end());
  effect->Release();
  SafeRelease(compiled);

  json.Key("validTechniques");
  json.BeginObject();
  for (const Validity& entry : validity) {
    json.Key(entry.profile);
    json.BeginArray();
    for (std::size_t i = 0; i < entry.order.size(); ++i) {
      if (entry.orderNamed[i]) {
        json.String(entry.order[i]);
      } else {
        json.Null();
      }
    }
    json.EndArray();
  }
  json.EndObject();
  json.Key("unexpectedDeviceCalls");
  json.BeginArray();
  for (const std::string& name : unexpected) {
    json.String(name);
  }
  json.EndArray();
  json.EndObject();
  const std::string text = json.Take();

  if (outPath.empty()) {
    std::cout << text;
  } else {
    std::ofstream file(outPath, std::ios::binary);
    file << text;
    if (!file) {
      std::cerr << "fxd3dx_dump: cannot write " << outPath << "\n";
      return 2;
    }
  }
  return 0;
}
