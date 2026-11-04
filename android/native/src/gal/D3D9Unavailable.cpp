// The Direct3D 9 backend's names that shared code calls, for the Android client build, where the
// backend does not exist (android/native/include/shadow/README.md). No Direct3D 9 device is ever
// created here, so a Direct3D 9 object never reaches these.

#include <cstdlib>

#include "gpg/core/utils/Logging.h"
#include "gpg/gal/backends/d3d9/EffectTechniqueD3D9.hpp"
#include "gpg/gal/backends/d3d9/Float16HardwareVertexFormatterD3D9.hpp"
#include "gpg/gal/backends/d3d9/HardwareVertexFormatterD3D9.hpp"
#include "gpg/gal/backends/d3d9/RenderTargetD3D9.hpp"

namespace gpg::gal
{
  namespace
  {
    // The Direct3D 9 mesh formatters: gpg/gal/MeshVertex.cpp keeps one of each, and only a
    // Direct3D 9 device chooses them (GetHardwareVertexFormatter switches on the device's API).
    [[noreturn]] void FormatterWithoutDirect3D9()
    {
      gpg::Warnf("gal: Direct3D 9 mesh formatter on a device without Direct3D 9");
      std::abort();
    }
  } // namespace

  HardwareVertexFormatterD3D9::HardwareVertexFormatterD3D9() = default;
  HardwareVertexFormatterD3D9::~HardwareVertexFormatterD3D9() = default;

  bool HardwareVertexFormatterD3D9::AllowMeshInstancing()
  {
    FormatterWithoutDirect3D9();
  }

  boost::shared_ptr<VertexFormat> HardwareVertexFormatterD3D9::CreateVertexFormat(std::int32_t)
  {
    FormatterWithoutDirect3D9();
  }

  std::uint32_t HardwareVertexFormatterD3D9::GetVertexStride(std::int32_t, std::int32_t)
  {
    FormatterWithoutDirect3D9();
  }

  void HardwareVertexFormatterD3D9::WriteFormattedVertex(std::int32_t, void*, const MeshVertex&, std::int32_t)
  {
    FormatterWithoutDirect3D9();
  }

  Float16HardwareVertexFormatterD3D9::Float16HardwareVertexFormatterD3D9() = default;
  Float16HardwareVertexFormatterD3D9::~Float16HardwareVertexFormatterD3D9() = default;

  bool Float16HardwareVertexFormatterD3D9::AllowMeshInstancing()
  {
    FormatterWithoutDirect3D9();
  }

  boost::shared_ptr<VertexFormat> Float16HardwareVertexFormatterD3D9::CreateVertexFormat(std::int32_t)
  {
    FormatterWithoutDirect3D9();
  }

  std::uint32_t Float16HardwareVertexFormatterD3D9::GetVertexStride(std::int32_t, std::int32_t)
  {
    FormatterWithoutDirect3D9();
  }

  void Float16HardwareVertexFormatterD3D9::WriteFormattedVertex(std::int32_t, void*, const MeshVertex&, std::int32_t)
  {
    FormatterWithoutDirect3D9();
  }

  // Declared by moho/render/Shadow.cpp, defined in backends/d3d9/D3D9DebugDump.cpp on Windows:
  // the shadow-map dump (FAF_TOGGLE_DIR) saves a Direct3D 9 surface with D3DX.
  long DebugSaveSurfaceToFileA(const char* filePath, unsigned int fileFormat, void* sourceSurface);

  long DebugSaveSurfaceToFileA(const char* const filePath, unsigned int, void*)
  {
    gpg::Warnf("gal: %s not saved: the surface dump needs Direct3D 9", filePath != nullptr ? filePath : "(null)");
    return static_cast<long>(E_NOTIMPL);
  }

  IDirect3DSurface9* RenderTargetD3D9::GetSurface()
  {
    return nullptr;
  }

  void EffectTechniqueD3D9::CommitChanges()
  {
    // HardwareMeshBatch's open-pass draw: only a technique begun on a Direct3D 9 device gets here.
    gpg::Warnf("gal: Direct3D 9 effect technique on a device without Direct3D 9");
    std::abort();
  }
} // namespace gpg::gal
