// Engine addition (android/CLIENT_PLAN.md, C1): ShaderCompiler.h where no dxcompiler.dll can be
// loaded, in place of ShaderCompiler.cpp. The compiler is unavailable, as on a Windows host
// without DXC: LoadError() says why, and every compile fails with that message. C2 replaces this
// with DXC built for Android (libdxcompiler.so).
//
// FAF_GAL_SMOKE_TEST (the renderer test library, src/platform/android/GalSmoke.cpp) reports it
// available, so a Vulkan device comes up for a test that clears and presents without effects;
// a compile still fails.

#include "ShaderCompiler.h"

namespace gpg::gal::effects
{
  struct ShaderCompiler::Library
  {
  };

  ShaderCompiler& ShaderCompiler::Instance()
  {
    static ShaderCompiler sCompiler;
    return sCompiler;
  }

  ShaderCompiler::ShaderCompiler()
    : mLoadError("no shader compiler on this platform yet (DXC for Android, android/CLIENT_PLAN.md C2)")
  {}

  ShaderCompiler::~ShaderCompiler() = default;

  bool ShaderCompiler::Available() const noexcept
  {
#if defined(FAF_GAL_SMOKE_TEST)
    return true;
#else
    return false;
#endif
  }

  const std::string& ShaderCompiler::LoadError() const noexcept
  {
    return mLoadError;
  }

  const std::string& ShaderCompiler::Version() const noexcept
  {
    return mVersion;
  }

  CompiledShader ShaderCompiler::Compile(
    std::string_view, const ShaderEntry&, ShaderTarget, const std::vector<std::wstring>&
  ) const
  {
    return Run({}, {});
  }

  CompiledShader ShaderCompiler::CompileRaw(
    std::string_view, std::string_view, const wchar_t*, const std::vector<std::wstring>&
  ) const
  {
    return Run({}, {});
  }

  CompiledShader ShaderCompiler::Run(std::string_view, const std::vector<std::wstring>&) const
  {
    CompiledShader result;
    result.messages = mLoadError;
    return result;
  }

  std::vector<ShaderInput> ShaderCompiler::ReflectInputs(const std::vector<std::uint8_t>&) const
  {
    return {};
  }
} // namespace gpg::gal::effects
