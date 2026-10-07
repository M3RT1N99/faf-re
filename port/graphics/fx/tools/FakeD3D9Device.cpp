#include "FakeD3D9Device.h"

#include <d3dx9shader.h>

#include <cstring>
#include <utility>

namespace fxd3dx {

  namespace {

    class FakeVertexDeclaration final : public IDirect3DVertexDeclaration9
    {
    public:
      explicit FakeVertexDeclaration(IDirect3DDevice9* device)
        : mDevice(device)
      {}

      HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
      {
        if (riid == IID_IUnknown || riid == IID_IDirect3DVertexDeclaration9) {
          *object = this;
          AddRef();
          return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
      }
      ULONG STDMETHODCALLTYPE AddRef() override { return ++mRefs; }
      ULONG STDMETHODCALLTYPE Release() override
      {
        const ULONG refs = --mRefs;
        if (refs == 0) {
          delete this;
        }
        return refs;
      }
      HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** device) override
      {
        *device = mDevice;
        mDevice->AddRef();
        return D3D_OK;
      }
      HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9*, UINT* count) override
      {
        if (count != nullptr) {
          *count = 0;
        }
        return D3DERR_INVALIDCALL;
      }

    private:
      ULONG mRefs = 1;
      IDirect3DDevice9* mDevice;
    };

    class FakeStateBlock final : public IDirect3DStateBlock9
    {
    public:
      explicit FakeStateBlock(IDirect3DDevice9* device)
        : mDevice(device)
      {}

      HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
      {
        if (riid == IID_IUnknown || riid == IID_IDirect3DStateBlock9) {
          *object = this;
          AddRef();
          return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
      }
      ULONG STDMETHODCALLTYPE AddRef() override { return ++mRefs; }
      ULONG STDMETHODCALLTYPE Release() override
      {
        const ULONG refs = --mRefs;
        if (refs == 0) {
          delete this;
        }
        return refs;
      }
      HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** device) override
      {
        *device = mDevice;
        mDevice->AddRef();
        return D3D_OK;
      }
      HRESULT STDMETHODCALLTYPE Capture() override { return D3D_OK; }
      HRESULT STDMETHODCALLTYPE Apply() override { return D3D_OK; }

    private:
      ULONG mRefs = 1;
      IDirect3DDevice9* mDevice;
    };

  } // namespace

  // ---- FakeShader -------------------------------------------------------------

  template <class Interface>
  FakeShader<Interface>::FakeShader(IDirect3DDevice9* device, const DWORD* function, const std::size_t bytes)
    : mDevice(device)
    , mBytecode(function, function + bytes / sizeof(DWORD))
  {}

  template <class Interface>
  HRESULT STDMETHODCALLTYPE FakeShader<Interface>::QueryInterface(REFIID riid, void** object)
  {
    if (riid == IID_IUnknown || riid == __uuidof(Interface)) {
      *object = this;
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }

  template <class Interface>
  ULONG STDMETHODCALLTYPE FakeShader<Interface>::Release()
  {
    const ULONG refs = --mRefs;
    if (refs == 0) {
      delete this;
    }
    return refs;
  }

  template <class Interface>
  HRESULT STDMETHODCALLTYPE FakeShader<Interface>::GetDevice(IDirect3DDevice9** device)
  {
    *device = mDevice;
    mDevice->AddRef();
    return D3D_OK;
  }

  // D3DXCreateEffect reads the function back (measured: it calls GetFunction
  // for every shader it creates), so this must return the bytecode.
  template <class Interface>
  HRESULT STDMETHODCALLTYPE FakeShader<Interface>::GetFunction(void* data, UINT* bytes)
  {
    const UINT size = static_cast<UINT>(mBytecode.size() * sizeof(DWORD));
    if (data != nullptr) {
      if (*bytes < size) {
        return D3DERR_INVALIDCALL;
      }
      std::memcpy(data, mBytecode.data(), size);
    }
    *bytes = size;
    return D3D_OK;
  }

  template class FakeShader<IDirect3DVertexShader9>;
  template class FakeShader<IDirect3DPixelShader9>;

  // ---- FakeTexture ------------------------------------------------------------

  FakeTexture::FakeTexture(IDirect3DDevice9* device, std::string name, const D3DRESOURCETYPE type)
    : mDevice(device)
    , mName(std::move(name))
    , mType(type)
  {}

  HRESULT STDMETHODCALLTYPE FakeTexture::QueryInterface(REFIID riid, void** object)
  {
    if (riid == IID_IUnknown || riid == IID_IDirect3DResource9 || riid == IID_IDirect3DBaseTexture9 ||
        riid == IID_IDirect3DTexture9) {
      *object = this;
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }

  HRESULT STDMETHODCALLTYPE FakeTexture::GetDevice(IDirect3DDevice9** device)
  {
    *device = mDevice;
    mDevice->AddRef();
    return D3D_OK;
  }

  HRESULT STDMETHODCALLTYPE FakeTexture::GetSurfaceLevel(UINT, IDirect3DSurface9** surface)
  {
    *surface = nullptr;
    return D3DERR_INVALIDCALL;
  }

  // ---- FakeD3D9Device ---------------------------------------------------------

  FakeD3D9Device::FakeD3D9Device(const DeviceLimits limits)
    : mLimits(limits)
  {}

  HRESULT STDMETHODCALLTYPE FakeD3D9Device::QueryInterface(REFIID riid, void** object)
  {
    if (riid == IID_IUnknown || riid == IID_IDirect3DDevice9) {
      *object = this;
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }

  // ValidateTechnique asks for the caps. Without real ones, report a shader
  // model 3 device with every capability bit set and generous limits, so
  // that only shader creation decides validity (see the header).
  HRESULT STDMETHODCALLTYPE FakeD3D9Device::GetDeviceCaps(D3DCAPS9* caps)
  {
    if (mLimits.caps != nullptr) {
      *caps = *mLimits.caps;
      return D3D_OK;
    }
    std::memset(caps, 0xFF, sizeof(*caps)); // every capability bit
    caps->DeviceType = D3DDEVTYPE_HAL;
    caps->AdapterOrdinal = 0;
    caps->MaxTextureWidth = 16384;
    caps->MaxTextureHeight = 16384;
    caps->MaxVolumeExtent = 2048;
    caps->MaxTextureRepeat = 8192;
    caps->MaxTextureAspectRatio = 16384;
    caps->MaxAnisotropy = 16;
    caps->MaxVertexW = 1e10f;
    caps->GuardBandLeft = -1e8f;
    caps->GuardBandTop = -1e8f;
    caps->GuardBandRight = 1e8f;
    caps->GuardBandBottom = 1e8f;
    caps->ExtentsAdjust = 0.0f;
    caps->MaxTextureBlendStages = 8;
    caps->MaxSimultaneousTextures = 8;
    caps->MaxActiveLights = 8;
    caps->MaxUserClipPlanes = 6;
    caps->MaxVertexBlendMatrices = 4;
    caps->MaxVertexBlendMatrixIndex = 255;
    caps->MaxPointSize = 8192.0f;
    caps->MaxPrimitiveCount = 0xFFFFFF;
    caps->MaxVertexIndex = 0xFFFFFF;
    caps->MaxStreams = 16;
    caps->MaxStreamStride = 508;
    caps->VertexShaderVersion = D3DVS_VERSION(mLimits.maxVertexShader >> 8, mLimits.maxVertexShader & 0xFF);
    caps->MaxVertexShaderConst = 256;
    caps->PixelShaderVersion = D3DPS_VERSION(mLimits.maxPixelShader >> 8, mLimits.maxPixelShader & 0xFF);
    caps->PixelShader1xMaxValue = 3.4e38f;
    caps->MaxNpatchTessellationLevel = 0.0f;
    caps->MasterAdapterOrdinal = 0;
    caps->AdapterOrdinalInGroup = 0;
    caps->NumberOfAdaptersInGroup = 1;
    caps->NumSimultaneousRTs = 4;
    caps->VS20Caps.DynamicFlowControlDepth = D3DVS20_MAX_DYNAMICFLOWCONTROLDEPTH;
    caps->VS20Caps.NumTemps = D3DVS20_MAX_NUMTEMPS;
    caps->VS20Caps.StaticFlowControlDepth = D3DVS20_MAX_STATICFLOWCONTROLDEPTH;
    caps->PS20Caps.DynamicFlowControlDepth = D3DPS20_MAX_DYNAMICFLOWCONTROLDEPTH;
    caps->PS20Caps.NumTemps = D3DPS20_MAX_NUMTEMPS;
    caps->PS20Caps.StaticFlowControlDepth = D3DPS20_MAX_STATICFLOWCONTROLDEPTH;
    caps->PS20Caps.NumInstructionSlots = D3DPS20_MAX_NUMINSTRUCTIONSLOTS;
    caps->MaxVShaderInstructionsExecuted = 0xFFFF;
    caps->MaxPShaderInstructionsExecuted = 0xFFFF;
    caps->MaxVertexShader30InstructionSlots = D3DMAX30SHADERINSTRUCTIONS;
    caps->MaxPixelShader30InstructionSlots = D3DMAX30SHADERINSTRUCTIONS;
    return D3D_OK;
  }

  HRESULT STDMETHODCALLTYPE FakeD3D9Device::CreateVertexShader(
    CONST DWORD* function, IDirect3DVertexShader9** shader
  )
  {
    *shader = nullptr;
    if ((function[0] & 0xFFFFu) > mLimits.maxVertexShader) {
      ++mRefused;
      return D3DERR_INVALIDCALL;
    }
    *shader = new FakeVertexShader(this, function, D3DXGetShaderSize(function));
    return D3D_OK;
  }

  HRESULT STDMETHODCALLTYPE FakeD3D9Device::CreatePixelShader(CONST DWORD* function, IDirect3DPixelShader9** shader)
  {
    *shader = nullptr;
    if ((function[0] & 0xFFFFu) > mLimits.maxPixelShader) {
      ++mRefused;
      return D3DERR_INVALIDCALL;
    }
    *shader = new FakePixelShader(this, function, D3DXGetShaderSize(function));
    return D3D_OK;
  }

  HRESULT STDMETHODCALLTYPE FakeD3D9Device::CreateVertexDeclaration(
    CONST D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9** declaration
  )
  {
    *declaration = new FakeVertexDeclaration(this);
    return D3D_OK;
  }

  HRESULT STDMETHODCALLTYPE FakeD3D9Device::EndStateBlock(IDirect3DStateBlock9** block)
  {
    *block = new FakeStateBlock(this);
    return D3D_OK;
  }

  HRESULT STDMETHODCALLTYPE FakeD3D9Device::CreateStateBlock(D3DSTATEBLOCKTYPE, IDirect3DStateBlock9** block)
  {
    *block = new FakeStateBlock(this);
    return D3D_OK;
  }

  // ValidateTechnique calls this after recording a pass into a state block.
  HRESULT STDMETHODCALLTYPE FakeD3D9Device::ValidateDevice(DWORD* passes)
  {
    *passes = 1;
    return D3D_OK;
  }

} // namespace fxd3dx
