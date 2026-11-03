// The effects' shader compiler (gpg/gal/effects/ShaderCompiler.h) for the Android client build, in
// place of ShaderCompiler.cpp, which loads dxcompiler.dll with Win32 calls: the same compiler,
// DXC v1.9.2609, built for Android (cmake/FafAndroidDxc.cmake) and called through DxcAndroid.h.
// The arguments of Compile and CompileRaw are ShaderCompiler.cpp's, so a shader compiles to the
// SPIR-V the Windows build makes of it; ../../CMakeLists.txt stops the configure when that file
// changes, until this one follows it.
//
// Available() is in GalAvailabilityAndroid.cpp, so the renderer test can give its own answer
// (src/smoke/GalSmokeDoubles.cpp).

#include "gpg/gal/effects/ShaderCompiler.h"

#include <Windows.h>

#include "DxcAndroid.h"

namespace gpg::gal::effects
{
  /** DXC is loaded: DxcAndroid.cpp holds the library and its compilers. */
  struct ShaderCompiler::Library
  {
  };

  namespace
  {
    [[nodiscard]] std::wstring Widen(const std::string_view text)
    {
      if (text.empty()) {
        return {};
      }
      const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
      std::wstring wide(static_cast<std::size_t>(length), L'\0');
      ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
      return wide;
    }
  } // namespace

  ShaderCompiler& ShaderCompiler::Instance()
  {
    static ShaderCompiler compiler;
    return compiler;
  }

  ShaderCompiler::ShaderCompiler()
  {
    if (const std::string& error = faf::android::dxc::LoadError(); !error.empty()) {
      mLoadError = error;
      return;
    }
    mVersion = faf::android::dxc::Version();
    mLibrary = new Library();
  }

  ShaderCompiler::~ShaderCompiler() = default; // process-lifetime singleton, as on Windows

  const std::string& ShaderCompiler::LoadError() const noexcept
  {
    return mLoadError;
  }

  const std::string& ShaderCompiler::Version() const noexcept
  {
    return mVersion;
  }

  CompiledShader ShaderCompiler::Compile(
    const std::string_view hlsl, const ShaderEntry& entry, const ShaderTarget target, const std::vector<std::wstring>& extraArguments
  ) const
  {
    CompiledShader result;
    if (mLibrary == nullptr) {
      result.messages = mLoadError;
      return result;
    }

    const std::wstring entryName = Widen(entry.name);
    std::vector<std::wstring> arguments{
      L"-E", entryName,
      L"-T", entry.stage == ShaderStage::Vertex ? L"vs_6_0" : L"ps_6_0",
      // HLSL 2018: the effects predate 2021's stricter vector logic.
      L"-HV", L"2018",
      // Row-major matrices: the parameter store keeps the engine's layout.
      L"-Zpr",
      L"-O3",
      L"-D", entry.stage == ShaderStage::Vertex ? L"FAF_VERTEX_STAGE=1" : L"FAF_PIXEL_STAGE=1",
      // FXC accepted these silently.
      L"-Wno-conversion",
    };
    if (target == ShaderTarget::Spirv) {
      // The Vulkan backend's descriptor model: textures t<n> and samplers
      // s<n> in set 0 at bindings n and 64+n, $Globals alone in set 1.
      // D3D's clip-space y points the other way.
      arguments.insert(arguments.end(), {L"-spirv", L"-fvk-use-dx-layout", L"-fspv-reflect", L"-fspv-target-env=vulkan1.1",
                                         L"-fvk-t-shift", L"0", L"0", L"-fvk-s-shift", L"64", L"0", L"-fvk-b-shift", L"0", L"0",
                                         L"-fvk-bind-globals", L"0", L"1"});
      if (entry.stage == ShaderStage::Vertex) {
        arguments.push_back(L"-fvk-invert-y");
      }
    }
    arguments.insert(arguments.end(), extraArguments.begin(), extraArguments.end());
    return Run(hlsl, arguments);
  }

  CompiledShader ShaderCompiler::CompileRaw(
    const std::string_view hlsl, const std::string_view entryName, const wchar_t* const profile, const std::vector<std::wstring>& extraArguments
  ) const
  {
    if (mLibrary == nullptr) {
      CompiledShader result;
      result.messages = mLoadError;
      return result;
    }
    std::vector<std::wstring> arguments{L"-E", Widen(entryName), L"-T", profile, L"-O3"};
    arguments.insert(arguments.end(), extraArguments.begin(), extraArguments.end());
    return Run(hlsl, arguments);
  }

  CompiledShader ShaderCompiler::Run(const std::string_view hlsl, const std::vector<std::wstring>& arguments) const
  {
    faf::android::dxc::Result compiled = faf::android::dxc::Compile(hlsl, arguments, faf::android::dxc::Output::Object);
    CompiledShader result;
    result.succeeded = compiled.succeeded;
    result.code = std::move(compiled.output);
    result.messages = std::move(compiled.messages);
    return result;
  }

  std::vector<ShaderInput> ShaderCompiler::ReflectInputs(const std::vector<std::uint8_t>& dxil) const
  {
    if (mLibrary == nullptr || dxil.empty()) {
      return {};
    }
    return faf::android::dxc::ReflectInputs(dxil);
  }
} // namespace gpg::gal::effects
