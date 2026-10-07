#pragma once

// A D3D9 device that renders nothing, for asking D3DX about effects without
// a GPU.
//
// D3DXCreateEffect needs an IDirect3DDevice9. The effect's reflection
// (descs, values, annotations) does not use it, and pass states can be read
// through an ID3DXEffectStateManager instead of the device. What does reach
// the device is shader creation at D3DXCreateEffect and, from
// ValidateTechnique / FindNextValidTechnique: vertex declarations, state
// blocks, GetDeviceCaps and ValidateDevice. Measured with d3dx9_43 on FAF's
// mesh.fx: the shader versions in the caps do not decide validity (caps
// saying ps_2_0 with every shader accepted: 180 of 180 techniques valid);
// shader creation does (ps_2_x shaders refused, caps saying ps_3_0: 109 of
// 180). So this device accepts or refuses shaders by a DeviceProfile's
// highest vertex/pixel shader version - the validity rule the front end
// reproduces (FxMetadata.h IsTechniqueValid) - and reports caps of a device
// that supports everything else, or a real adapter's caps.
//
// No real device is created, so the tool needs no desktop session or GPU and
// cannot disturb other D3D applications (in the M6 effect inventory run
// neither a HAL nor a NULLREF device could be created). Every method D3DX calls that is
// not implemented below is recorded (UnexpectedCalls()) so a run can prove it
// stayed on the paths measured here.

#include <d3d9.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace fxd3dx {

  struct DeviceLimits
  {
    std::uint32_t maxVertexShader = 0x0300; // 0xMMmm
    std::uint32_t maxPixelShader = 0x0300;
    const D3DCAPS9* caps = nullptr; // a real adapter's caps (--hal-caps); null: a generous SM3 device
  };

  /// Shader object returned by CreateVertexShader / CreatePixelShader; keeps
  /// a copy of the bytecode so the state manager can tell which pass shader
  /// was set.
  template <class Interface>
  class FakeShader final : public Interface
  {
  public:
    FakeShader(IDirect3DDevice9* device, const DWORD* function, std::size_t bytes);

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++mRefs; }
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** device) override;
    HRESULT STDMETHODCALLTYPE GetFunction(void* data, UINT* bytes) override;

    [[nodiscard]] const std::vector<DWORD>& Bytecode() const { return mBytecode; }

  private:
    ULONG mRefs = 1;
    IDirect3DDevice9* mDevice;
    std::vector<DWORD> mBytecode;
  };

  using FakeVertexShader = FakeShader<IDirect3DVertexShader9>;
  using FakePixelShader = FakeShader<IDirect3DPixelShader9>;

  /// An opaque texture: the dumper binds one per texture parameter so that
  /// SetTexture calls name the parameter they came from.
  class FakeTexture final : public IDirect3DTexture9
  {
  public:
    FakeTexture(IDirect3DDevice9* device, std::string name, D3DRESOURCETYPE type);

    [[nodiscard]] const std::string& Name() const { return mName; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++mRefs; }
    ULONG STDMETHODCALLTYPE Release() override { return --mRefs; } // owned by the dumper
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** device) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, CONST void*, DWORD, DWORD) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return D3DERR_INVALIDCALL; }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return mType; }
    DWORD STDMETHODCALLTYPE SetLOD(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return 0; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return 1; }
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) override { return D3DERR_INVALIDCALL; }
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { return D3DTEXF_NONE; }
    void STDMETHODCALLTYPE GenerateMipSubLevels() override {}
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT, D3DSURFACE_DESC*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT, IDirect3DSurface9** surface) override;
    HRESULT STDMETHODCALLTYPE LockRect(UINT, D3DLOCKED_RECT*, CONST RECT*, DWORD) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE AddDirtyRect(CONST RECT*) override { return D3DERR_INVALIDCALL; }

  private:
    ULONG mRefs = 1;
    IDirect3DDevice9* mDevice;
    std::string mName;
    D3DRESOURCETYPE mType;
  };

  class FakeD3D9Device final : public IDirect3DDevice9
  {
  public:
    explicit FakeD3D9Device(DeviceLimits limits);

    /// Device methods D3DX called that this class does not implement.
    [[nodiscard]] const std::set<std::string>& UnexpectedCalls() const { return mUnexpected; }
    /// Shaders refused because of the limits.
    [[nodiscard]] std::uint32_t RefusedShaders() const { return mRefused; }

    // Implemented.
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++mRefs; }
    ULONG STDMETHODCALLTYPE Release() override { return --mRefs; } // owned by the dumper
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* caps) override;
    HRESULT STDMETHODCALLTYPE CreateVertexShader(CONST DWORD* function, IDirect3DVertexShader9** shader) override;
    HRESULT STDMETHODCALLTYPE CreatePixelShader(CONST DWORD* function, IDirect3DPixelShader9** shader) override;
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(
      CONST D3DVERTEXELEMENT9* elements, IDirect3DVertexDeclaration9** declaration
    ) override;
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9** block) override;
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE type, IDirect3DStateBlock9** block) override;
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* passes) override;

    // Generated from the Windows SDK's d3d9.h (IDirect3DDevice9): setters
    // succeed and change nothing, everything else is recorded and fails.
    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override
    {
      return Unexpected("TestCooperativeLevel");
    }

    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override
    {
      Note("GetAvailableTextureMem"); return 0;
    }

    HRESULT STDMETHODCALLTYPE EvictManagedResources() override
    {
      return Unexpected("EvictManagedResources");
    }

    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** ppD3D9) override
    {
      if (ppD3D9 != nullptr) { *ppD3D9 = nullptr; }
      return Unexpected("GetDirect3D");
    }

    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE* pMode) override
    {
      (void)iSwapChain; (void)pMode;
      return Unexpected("GetDisplayMode");
    }

    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters) override
    {
      (void)pParameters;
      return Unexpected("GetCreationParameters");
    }

    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap) override
    {
      (void)XHotSpot; (void)YHotSpot; (void)pCursorBitmap;
      return Accept("SetCursorProperties");
    }

    void STDMETHODCALLTYPE SetCursorPosition(int X, int Y, DWORD Flags) override
    {
      (void)X; (void)Y; (void)Flags;
      Note("SetCursorPosition");
    }

    BOOL STDMETHODCALLTYPE ShowCursor(BOOL bShow) override
    {
      (void)bShow;
      Note("ShowCursor"); return 0;
    }

    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain) override
    {
      (void)pPresentationParameters;
      if (pSwapChain != nullptr) { *pSwapChain = nullptr; }
      return Unexpected("CreateAdditionalSwapChain");
    }

    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT iSwapChain, IDirect3DSwapChain9** pSwapChain) override
    {
      (void)iSwapChain;
      if (pSwapChain != nullptr) { *pSwapChain = nullptr; }
      return Unexpected("GetSwapChain");
    }

    UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override
    {
      Note("GetNumberOfSwapChains"); return 0;
    }

    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) override
    {
      (void)pPresentationParameters;
      return Unexpected("Reset");
    }

    HRESULT STDMETHODCALLTYPE Present(CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion) override
    {
      (void)pSourceRect; (void)pDestRect; (void)hDestWindowOverride; (void)pDirtyRegion;
      return Unexpected("Present");
    }

    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) override
    {
      (void)iSwapChain; (void)iBackBuffer; (void)Type;
      if (ppBackBuffer != nullptr) { *ppBackBuffer = nullptr; }
      return Unexpected("GetBackBuffer");
    }

    HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus) override
    {
      (void)iSwapChain; (void)pRasterStatus;
      return Unexpected("GetRasterStatus");
    }

    HRESULT STDMETHODCALLTYPE SetDialogBoxMode(BOOL bEnableDialogs) override
    {
      (void)bEnableDialogs;
      return Accept("SetDialogBoxMode");
    }

    void STDMETHODCALLTYPE SetGammaRamp(UINT iSwapChain, DWORD Flags, CONST D3DGAMMARAMP* pRamp) override
    {
      (void)iSwapChain; (void)Flags; (void)pRamp;
      Note("SetGammaRamp");
    }

    void STDMETHODCALLTYPE GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP* pRamp) override
    {
      (void)iSwapChain; (void)pRamp;
      Note("GetGammaRamp");
    }

    HRESULT STDMETHODCALLTYPE CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) override
    {
      (void)Width; (void)Height; (void)Levels; (void)Usage; (void)Format; (void)Pool; (void)pSharedHandle;
      if (ppTexture != nullptr) { *ppTexture = nullptr; }
      return Unexpected("CreateTexture");
    }

    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle) override
    {
      (void)Width; (void)Height; (void)Depth; (void)Levels; (void)Usage; (void)Format; (void)Pool; (void)pSharedHandle;
      if (ppVolumeTexture != nullptr) { *ppVolumeTexture = nullptr; }
      return Unexpected("CreateVolumeTexture");
    }

    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle) override
    {
      (void)EdgeLength; (void)Levels; (void)Usage; (void)Format; (void)Pool; (void)pSharedHandle;
      if (ppCubeTexture != nullptr) { *ppCubeTexture = nullptr; }
      return Unexpected("CreateCubeTexture");
    }

    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle) override
    {
      (void)Length; (void)Usage; (void)FVF; (void)Pool; (void)pSharedHandle;
      if (ppVertexBuffer != nullptr) { *ppVertexBuffer = nullptr; }
      return Unexpected("CreateVertexBuffer");
    }

    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle) override
    {
      (void)Length; (void)Usage; (void)Format; (void)Pool; (void)pSharedHandle;
      if (ppIndexBuffer != nullptr) { *ppIndexBuffer = nullptr; }
      return Unexpected("CreateIndexBuffer");
    }

    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override
    {
      (void)Width; (void)Height; (void)Format; (void)MultiSample; (void)MultisampleQuality; (void)Lockable; (void)pSharedHandle;
      if (ppSurface != nullptr) { *ppSurface = nullptr; }
      return Unexpected("CreateRenderTarget");
    }

    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override
    {
      (void)Width; (void)Height; (void)Format; (void)MultiSample; (void)MultisampleQuality; (void)Discard; (void)pSharedHandle;
      if (ppSurface != nullptr) { *ppSurface = nullptr; }
      return Unexpected("CreateDepthStencilSurface");
    }

    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestinationSurface, CONST POINT* pDestPoint) override
    {
      (void)pSourceSurface; (void)pSourceRect; (void)pDestinationSurface; (void)pDestPoint;
      return Unexpected("UpdateSurface");
    }

    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) override
    {
      (void)pSourceTexture; (void)pDestinationTexture;
      return Unexpected("UpdateTexture");
    }

    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) override
    {
      (void)pRenderTarget; (void)pDestSurface;
      return Unexpected("GetRenderTargetData");
    }

    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface) override
    {
      (void)iSwapChain; (void)pDestSurface;
      return Unexpected("GetFrontBufferData");
    }

    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestSurface, CONST RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter) override
    {
      (void)pSourceSurface; (void)pSourceRect; (void)pDestSurface; (void)pDestRect; (void)Filter;
      return Unexpected("StretchRect");
    }

    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9* pSurface, CONST RECT* pRect, D3DCOLOR color) override
    {
      (void)pSurface; (void)pRect; (void)color;
      return Unexpected("ColorFill");
    }

    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override
    {
      (void)Width; (void)Height; (void)Format; (void)Pool; (void)pSharedHandle;
      if (ppSurface != nullptr) { *ppSurface = nullptr; }
      return Unexpected("CreateOffscreenPlainSurface");
    }

    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget) override
    {
      (void)RenderTargetIndex; (void)pRenderTarget;
      return Accept("SetRenderTarget");
    }

    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) override
    {
      (void)RenderTargetIndex;
      if (ppRenderTarget != nullptr) { *ppRenderTarget = nullptr; }
      return Unexpected("GetRenderTarget");
    }

    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil) override
    {
      (void)pNewZStencil;
      return Accept("SetDepthStencilSurface");
    }

    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) override
    {
      if (ppZStencilSurface != nullptr) { *ppZStencilSurface = nullptr; }
      return Unexpected("GetDepthStencilSurface");
    }

    HRESULT STDMETHODCALLTYPE BeginScene() override
    {
      return Accept("BeginScene");
    }

    HRESULT STDMETHODCALLTYPE EndScene() override
    {
      return Accept("EndScene");
    }

    HRESULT STDMETHODCALLTYPE Clear(DWORD Count, CONST D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) override
    {
      (void)Count; (void)pRects; (void)Flags; (void)Color; (void)Z; (void)Stencil;
      return Accept("Clear");
    }

    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix) override
    {
      (void)State; (void)pMatrix;
      return Accept("SetTransform");
    }

    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) override
    {
      (void)State; (void)pMatrix;
      return Unexpected("GetTransform");
    }

    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE, CONST D3DMATRIX*) override
    {
      return Accept("MultiplyTransform");
    }

    HRESULT STDMETHODCALLTYPE SetViewport(CONST D3DVIEWPORT9* pViewport) override
    {
      (void)pViewport;
      return Accept("SetViewport");
    }

    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* pViewport) override
    {
      (void)pViewport;
      return Unexpected("GetViewport");
    }

    HRESULT STDMETHODCALLTYPE SetMaterial(CONST D3DMATERIAL9* pMaterial) override
    {
      (void)pMaterial;
      return Accept("SetMaterial");
    }

    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9* pMaterial) override
    {
      (void)pMaterial;
      return Unexpected("GetMaterial");
    }

    HRESULT STDMETHODCALLTYPE SetLight(DWORD Index, CONST D3DLIGHT9*) override
    {
      (void)Index;
      return Accept("SetLight");
    }

    HRESULT STDMETHODCALLTYPE GetLight(DWORD Index, D3DLIGHT9*) override
    {
      (void)Index;
      return Unexpected("GetLight");
    }

    HRESULT STDMETHODCALLTYPE LightEnable(DWORD Index, BOOL Enable) override
    {
      (void)Index; (void)Enable;
      return Accept("LightEnable");
    }

    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD Index, BOOL* pEnable) override
    {
      (void)Index; (void)pEnable;
      return Unexpected("GetLightEnable");
    }

    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD Index, CONST float* pPlane) override
    {
      (void)Index; (void)pPlane;
      return Accept("SetClipPlane");
    }

    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD Index, float* pPlane) override
    {
      (void)Index; (void)pPlane;
      return Unexpected("GetClipPlane");
    }

    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override
    {
      (void)State; (void)Value;
      return Accept("SetRenderState");
    }

    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue) override
    {
      (void)State; (void)pValue;
      return Unexpected("GetRenderState");
    }

    HRESULT STDMETHODCALLTYPE SetClipStatus(CONST D3DCLIPSTATUS9* pClipStatus) override
    {
      (void)pClipStatus;
      return Accept("SetClipStatus");
    }

    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9* pClipStatus) override
    {
      (void)pClipStatus;
      return Unexpected("GetClipStatus");
    }

    HRESULT STDMETHODCALLTYPE GetTexture(DWORD Stage, IDirect3DBaseTexture9** ppTexture) override
    {
      (void)Stage;
      if (ppTexture != nullptr) { *ppTexture = nullptr; }
      return Unexpected("GetTexture");
    }

    HRESULT STDMETHODCALLTYPE SetTexture(DWORD Stage, IDirect3DBaseTexture9* pTexture) override
    {
      (void)Stage; (void)pTexture;
      return Accept("SetTexture");
    }

    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) override
    {
      (void)Stage; (void)Type; (void)pValue;
      return Unexpected("GetTextureStageState");
    }

    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override
    {
      (void)Stage; (void)Type; (void)Value;
      return Accept("SetTextureStageState");
    }

    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue) override
    {
      (void)Sampler; (void)Type; (void)pValue;
      return Unexpected("GetSamplerState");
    }

    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) override
    {
      (void)Sampler; (void)Type; (void)Value;
      return Accept("SetSamplerState");
    }

    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT PaletteNumber, CONST PALETTEENTRY* pEntries) override
    {
      (void)PaletteNumber; (void)pEntries;
      return Accept("SetPaletteEntries");
    }

    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY* pEntries) override
    {
      (void)PaletteNumber; (void)pEntries;
      return Unexpected("GetPaletteEntries");
    }

    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT PaletteNumber) override
    {
      (void)PaletteNumber;
      return Accept("SetCurrentTexturePalette");
    }

    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT *PaletteNumber) override
    {
      (void)PaletteNumber;
      return Unexpected("GetCurrentTexturePalette");
    }

    HRESULT STDMETHODCALLTYPE SetScissorRect(CONST RECT* pRect) override
    {
      (void)pRect;
      return Accept("SetScissorRect");
    }

    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT* pRect) override
    {
      (void)pRect;
      return Unexpected("GetScissorRect");
    }

    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(BOOL bSoftware) override
    {
      (void)bSoftware;
      return Accept("SetSoftwareVertexProcessing");
    }

    BOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override
    {
      Note("GetSoftwareVertexProcessing"); return 0;
    }

    HRESULT STDMETHODCALLTYPE SetNPatchMode(float nSegments) override
    {
      (void)nSegments;
      return Accept("SetNPatchMode");
    }

    float STDMETHODCALLTYPE GetNPatchMode() override
    {
      Note("GetNPatchMode"); return 0;
    }

    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) override
    {
      (void)PrimitiveType; (void)StartVertex; (void)PrimitiveCount;
      return Unexpected("DrawPrimitive");
    }

    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) override
    {
      (void)BaseVertexIndex; (void)MinVertexIndex; (void)NumVertices; (void)startIndex; (void)primCount;
      return Unexpected("DrawIndexedPrimitive");
    }

    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) override
    {
      (void)PrimitiveType; (void)PrimitiveCount; (void)pVertexStreamZeroData; (void)VertexStreamZeroStride;
      return Unexpected("DrawPrimitiveUP");
    }

    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) override
    {
      (void)PrimitiveType; (void)MinVertexIndex; (void)NumVertices; (void)PrimitiveCount; (void)pIndexData; (void)IndexDataFormat; (void)pVertexStreamZeroData; (void)VertexStreamZeroStride;
      return Unexpected("DrawIndexedPrimitiveUP");
    }

    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDecl, DWORD Flags) override
    {
      (void)SrcStartIndex; (void)DestIndex; (void)VertexCount; (void)pDestBuffer; (void)pVertexDecl; (void)Flags;
      return Unexpected("ProcessVertices");
    }

    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl) override
    {
      (void)pDecl;
      return Accept("SetVertexDeclaration");
    }

    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl) override
    {
      if (ppDecl != nullptr) { *ppDecl = nullptr; }
      return Unexpected("GetVertexDeclaration");
    }

    HRESULT STDMETHODCALLTYPE SetFVF(DWORD FVF) override
    {
      (void)FVF;
      return Accept("SetFVF");
    }

    HRESULT STDMETHODCALLTYPE GetFVF(DWORD* pFVF) override
    {
      (void)pFVF;
      return Unexpected("GetFVF");
    }

    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* pShader) override
    {
      (void)pShader;
      return Accept("SetVertexShader");
    }

    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** ppShader) override
    {
      if (ppShader != nullptr) { *ppShader = nullptr; }
      return Unexpected("GetVertexShader");
    }

    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount) override
    {
      (void)StartRegister; (void)pConstantData; (void)Vector4fCount;
      return Accept("SetVertexShaderConstantF");
    }

    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) override
    {
      (void)StartRegister; (void)pConstantData; (void)Vector4fCount;
      return Unexpected("GetVertexShaderConstantF");
    }

    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount) override
    {
      (void)StartRegister; (void)pConstantData; (void)Vector4iCount;
      return Accept("SetVertexShaderConstantI");
    }

    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) override
    {
      (void)StartRegister; (void)pConstantData; (void)Vector4iCount;
      return Unexpected("GetVertexShaderConstantI");
    }

    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT BoolCount) override
    {
      (void)StartRegister; (void)pConstantData; (void)BoolCount;
      return Accept("SetVertexShaderConstantB");
    }

    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) override
    {
      (void)StartRegister; (void)pConstantData; (void)BoolCount;
      return Unexpected("GetVertexShaderConstantB");
    }

    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) override
    {
      (void)StreamNumber; (void)pStreamData; (void)OffsetInBytes; (void)Stride;
      return Accept("SetStreamSource");
    }

    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* pOffsetInBytes, UINT* pStride) override
    {
      (void)StreamNumber; (void)pOffsetInBytes; (void)pStride;
      if (ppStreamData != nullptr) { *ppStreamData = nullptr; }
      return Unexpected("GetStreamSource");
    }

    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT StreamNumber, UINT Setting) override
    {
      (void)StreamNumber; (void)Setting;
      return Accept("SetStreamSourceFreq");
    }

    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT StreamNumber, UINT* pSetting) override
    {
      (void)StreamNumber; (void)pSetting;
      return Unexpected("GetStreamSourceFreq");
    }

    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* pIndexData) override
    {
      (void)pIndexData;
      return Accept("SetIndices");
    }

    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** ppIndexData) override
    {
      if (ppIndexData != nullptr) { *ppIndexData = nullptr; }
      return Unexpected("GetIndices");
    }

    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* pShader) override
    {
      (void)pShader;
      return Accept("SetPixelShader");
    }

    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** ppShader) override
    {
      if (ppShader != nullptr) { *ppShader = nullptr; }
      return Unexpected("GetPixelShader");
    }

    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount) override
    {
      (void)StartRegister; (void)pConstantData; (void)Vector4fCount;
      return Accept("SetPixelShaderConstantF");
    }

    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) override
    {
      (void)StartRegister; (void)pConstantData; (void)Vector4fCount;
      return Unexpected("GetPixelShaderConstantF");
    }

    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount) override
    {
      (void)StartRegister; (void)pConstantData; (void)Vector4iCount;
      return Accept("SetPixelShaderConstantI");
    }

    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) override
    {
      (void)StartRegister; (void)pConstantData; (void)Vector4iCount;
      return Unexpected("GetPixelShaderConstantI");
    }

    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT BoolCount) override
    {
      (void)StartRegister; (void)pConstantData; (void)BoolCount;
      return Accept("SetPixelShaderConstantB");
    }

    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) override
    {
      (void)StartRegister; (void)pConstantData; (void)BoolCount;
      return Unexpected("GetPixelShaderConstantB");
    }

    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT Handle, CONST float* pNumSegs, CONST D3DRECTPATCH_INFO* pRectPatchInfo) override
    {
      (void)Handle; (void)pNumSegs; (void)pRectPatchInfo;
      return Unexpected("DrawRectPatch");
    }

    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT Handle, CONST float* pNumSegs, CONST D3DTRIPATCH_INFO* pTriPatchInfo) override
    {
      (void)Handle; (void)pNumSegs; (void)pTriPatchInfo;
      return Unexpected("DrawTriPatch");
    }

    HRESULT STDMETHODCALLTYPE DeletePatch(UINT Handle) override
    {
      (void)Handle;
      return Unexpected("DeletePatch");
    }

    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery) override
    {
      (void)Type;
      if (ppQuery != nullptr) { *ppQuery = nullptr; }
      return Unexpected("CreateQuery");
    }

  private:
    HRESULT Accept(const char*) { return D3D_OK; }
    HRESULT Unexpected(const char* name)
    {
      mUnexpected.insert(name);
      return D3DERR_INVALIDCALL;
    }
    void Note(const char* name) { mUnexpected.insert(name); }

    ULONG mRefs = 1;
    DeviceLimits mLimits;
    std::set<std::string> mUnexpected;
    std::uint32_t mRefused = 0;
  };

} // namespace fxd3dx
