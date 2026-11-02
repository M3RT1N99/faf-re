// The effects' shader compiler (gpg/gal/effects/ShaderCompiler.h) for the Android client build,
// in place of ShaderCompiler.cpp, which loads dxcompiler.dll. There is no compiler here yet, as on
// a Windows host without DXC: LoadError() says why, and every compile fails with that message.
// DXC built for Android replaces this (CLIENT_PLAN.md, C2).
//
// Available() is in GalAvailabilityAndroid.cpp, so the renderer test can give its own answer
// (src/smoke/GalSmokeDoubles.cpp).

#include "gpg/gal/effects/ShaderCompiler.h"

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
    : mLoadError("no shader compiler on Android yet (android/CLIENT_PLAN.md, C2)")
  {}

  ShaderCompiler::~ShaderCompiler() = default;

  const std::string& ShaderCompiler::LoadError() const noexcept
  {
    return mLoadError;
  }

  const std::string& ShaderCompiler::Version() const noexcept
  {
    return mVersion;
  }

  CompiledShader ShaderCompiler::Compile(std::string_view, const ShaderEntry&, ShaderTarget, const std::vector<std::wstring>&) const
  {
    return Run({}, {});
  }

  CompiledShader ShaderCompiler::CompileRaw(std::string_view, std::string_view, const wchar_t*, const std::vector<std::wstring>&) const
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
