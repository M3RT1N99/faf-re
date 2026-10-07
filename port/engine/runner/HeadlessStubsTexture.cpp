// The D3D texture resource class (moho/render/d3d/RD3DTextureResource.cpp) for the Android headless
// runner; see HeadlessStubs.h. M3c.
//
// RD3DTextureResource.cpp cannot be linked (it includes the D3D9 backend), but the Windows runner
// registers three things that belong to this class, and the Android runner must register them too:
// its reflected type (RD3DTextureResourceTypeInfo.cpp), the "d3d_textures" prefetch kind and the
// texture resource factory (CD3DTextureResourceFactory.cpp). Without them the Core-set Lua binder
// CPrefetchSet:Update raises "Unknown kind of resource in prefetch set" (CPrefetchSet.cpp:140) for a
// kind the Windows runner accepts. Under the Itanium ABI the class's vtable and type_info come with
// its key function, the destructor, so this file defines every member; link_closure.py
// (RUNNER_KEY_CLASSES) knows it does.
//
// The runner state these follow (moho/app/HeadlessReplay.cpp, Run): CreateDevice never runs, so the
// gal device singleton stays empty (gpg::gal::Device::IsReady() is false). Everything the class does
// without a gal device is plain data handling and keeps the real bodies: construction, Init (keeps the
// file's bytes), ReloadTexture (maps the file again). Creating the GPU texture is the one step that
// needs the device; without it the real LoadTexture warns and fails, and so does this one, which
// makes every query answer as on Windows (zero dimensions, no texture).
//
// What the runner does with the class: the texture factory's Preload only maps the file
// (CD3DTextureResourceFactory.cpp:261), and Load/LoadFrom wrap the bytes in an RD3DTextureResource;
// the map loader's preview chunk asks for the fallback texture when a map stores no preview
// (CWldMap.cpp, RWldMapPreviewChunk::Load). Nothing in the runner draws, so LoadTexture is reached only
// through the queries below.

#include "HeadlessStubs.h"

#include "gpg/core/streams/MemBufferStream.h"
#include "gpg/core/utils/Global.h"
#include "gpg/core/utils/Logging.h"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/Texture.hpp"
#include "moho/misc/FileWaitHandleSet.h"
#include "moho/render/d3d/RD3DTextureResource.h"

namespace moho
{
  // RD3DTextureResource.cpp:14.
  gpg::RType* RD3DTextureResource::sType = nullptr;

  namespace
  {
    // RD3DTextureResource.cpp:18-21.
    constexpr std::uint32_t kTextureSourceArchive = 1u;
    constexpr std::uint32_t kTextureUsageStatic = 1u;
    constexpr const char* kUnreachableExpr = "Reached the supposably unreachable.";
    constexpr const char* kTextureSourcePath = "c:\\work\\rts\\main\\code\\src\\core\\D3DRes.cpp";
  } // namespace

  // RD3DTextureResource.cpp:32, the same body.
  RD3DTextureResource::RD3DTextureResource(const char* const location)
    : mResources()
    , mContext(location != nullptr ? location : "", 0U, 0U, 0U)
    , mBaseTex()
  {
    FAF_RUNNER_STUB("RD3DTextureResource::RD3DTextureResource");
  }

  // RD3DTextureResource.cpp:47, the same body (CD3DDeviceResources::GetTextureSheet's form, for a
  // texture held in memory).
  RD3DTextureResource::RD3DTextureResource(const char* const location, void* const data, const std::size_t size)
    : mResources()
    , mContext()
    , mBaseTex()
  {
    FAF_RUNNER_STUB("RD3DTextureResource::RD3DTextureResource(data)");
    mContext.source_ = kTextureSourceArchive;
    mContext.location_.assign_owned(location != nullptr ? location : "");
    mContext.SetDataBuffer(gpg::CopyMemBuffer(data, size));
  }

  // RD3DTextureResource.cpp:65, the same body. The class's key function.
  RD3DTextureResource::~RD3DTextureResource()
  {
    mBaseTex.reset();
  }

  // RD3DTextureResource.cpp:78, the same body.
  bool RD3DTextureResource::Init(gpg::MemBuffer<const char> data)
  {
    FAF_RUNNER_STUB("RD3DTextureResource::Init");
    mContext.SetDataBuffer(data);
    return true;
  }

  // RD3DTextureResource.cpp:87, the same body.
  void RD3DTextureResource::ReloadTexture()
  {
    FAF_RUNNER_STUB("RD3DTextureResource::ReloadTexture");
    const gpg::MemBuffer<const char> textureBytes = DISK_MemoryMapFile(mContext.location_.c_str());
    if (textureBytes.mBegin == nullptr) {
      return;
    }

    mBaseTex.reset();
    Init(textureBytes);
    LoadTexture();
  }

  // RD3DTextureResource.cpp:105 without a gal device. The real body reaches the device through
  // D3D_GetDevice(), the CD3DDevice singleton: it copies the device resources' skip-mip level into the
  // context (0: CD3DDeviceResources.cpp:222, only a console command changes it), then asks
  // CD3DDevice::GetGalDevice() (CD3DDevice.cpp:707), which is null while gpg::gal::Device::IsReady()
  // is false - always, in the runner - and so warns and fails here. A device would mean a renderer this
  // runner does not have: trap.
  bool RD3DTextureResource::LoadTexture()
  {
    FAF_RUNNER_STUB("RD3DTextureResource::LoadTexture");
    if (mBaseTex.get() != nullptr) {
      return true;
    }

    if (mContext.dataBegin_ == 0) {
      return false;
    }

    mContext.format_ = 0;
    mContext.usage_ = kTextureUsageStatic;
    mContext.source_ = kTextureSourceArchive;
    mContext.reserved0x44_ = 0u;

    if (gpg::gal::Device::IsReady()) {
      FAF_RUNNER_TRAP("RD3DTextureResource::LoadTexture with a gal device");
    }
    gpg::Warnf("Unable to load texture: %s", mContext.location_.c_str());
    return false;
  }

  // RD3DTextureResource.cpp:155, :184, :204, :222, the same bodies (each LoadTexture() fails, see above).
  Wm3::Vector3f* RD3DTextureResource::GetDimensions(Wm3::Vector3f* const outDimensions)
  {
    FAF_RUNNER_STUB("RD3DTextureResource::GetDimensions");
    if (outDimensions == nullptr) {
      return nullptr;
    }

    if (!LoadTexture() || mBaseTex.get() == nullptr) {
      outDimensions->x = 0.0f;
      outDimensions->y = 0.0f;
      outDimensions->z = 0.0f;
      return outDimensions;
    }

    const gpg::gal::TextureContext* const context = mBaseTex->GetContext();
    outDimensions->x = static_cast<float>(context->width_);
    outDimensions->y = static_cast<float>(context->height_);
    outDimensions->z = 0.0f;
    return outDimensions;
  }

  Wm3::Vector2i* RD3DTextureResource::GetOriginalDimensions(Wm3::Vector2i* const outDimensions)
  {
    FAF_RUNNER_STUB("RD3DTextureResource::GetOriginalDimensions");
    if (outDimensions == nullptr) {
      return nullptr;
    }

    if (!LoadTexture() || mBaseTex.get() == nullptr) {
      outDimensions->x = 0;
      outDimensions->y = 0;
      return outDimensions;
    }

    const gpg::gal::TextureContext* const context = mBaseTex->GetContext();
    outDimensions->x = static_cast<int>(context->width_);
    outDimensions->y = static_cast<int>(context->height_);
    return outDimensions;
  }

  int RD3DTextureResource::GetTextureSizeInBytes()
  {
    FAF_RUNNER_STUB("RD3DTextureResource::GetTextureSizeInBytes");
    if (!LoadTexture() || mBaseTex.get() == nullptr) {
      return 0;
    }

    return static_cast<int>(mBaseTex->GetContext()->reserved0x50_);
  }

  RD3DTextureResource::TextureHandle& RD3DTextureResource::GetTexture(TextureHandle& outTexture)
  {
    FAF_RUNNER_STUB("RD3DTextureResource::GetTexture");
    LoadTexture();
    outTexture = mBaseTex;
    return outTexture;
  }

  // RD3DTextureResource.cpp:236, :249, :261, :273, :285, the same bodies: lanes the binary marks
  // unreachable.
  bool RD3DTextureResource::Lock(std::uint32_t* const, void** const)
  {
    FAF_RUNNER_STUB("RD3DTextureResource::Lock");
    LoadTexture();
    gpg::HandleAssertFailure(kUnreachableExpr, 189, kTextureSourcePath);
    return false;
  }

  bool RD3DTextureResource::LockRect(const RECT* const, std::uint32_t* const, void** const)
  {
    FAF_RUNNER_STUB("RD3DTextureResource::LockRect");
    LoadTexture();
    gpg::HandleAssertFailure(kUnreachableExpr, 195, kTextureSourcePath);
    return false;
  }

  bool RD3DTextureResource::Unlock()
  {
    FAF_RUNNER_STUB("RD3DTextureResource::Unlock");
    LoadTexture();
    gpg::HandleAssertFailure(kUnreachableExpr, 202, kTextureSourcePath);
    return false;
  }

  bool RD3DTextureResource::ReadFromArchive(gpg::BinaryReader* const)
  {
    FAF_RUNNER_STUB("RD3DTextureResource::ReadFromArchive");
    LoadTexture();
    gpg::HandleAssertFailure(kUnreachableExpr, 208, kTextureSourcePath);
    return false;
  }

  bool RD3DTextureResource::SaveToArchive(gpg::Stream* const, const bool)
  {
    FAF_RUNNER_STUB("RD3DTextureResource::SaveToArchive");
    LoadTexture();
    gpg::HandleAssertFailure(kUnreachableExpr, 214, kTextureSourcePath);
    return false;
  }
} // namespace moho
