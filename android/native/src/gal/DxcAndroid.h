#pragma once

// DXC for the Android client build: libdxcompiler.so (cmake/FafAndroidDxc.cmake), for the effects'
// shader compiler (ShaderCompilerAndroid.cpp) and for their preprocessor in place of
// d3dcompiler_47's (D3DCompilerAndroid.cpp).
//
// DXC's headers declare the Windows types themselves on Linux and Android (dxc/WinAdapter.h), as
// the client's Win32 stand-ins do, so the two cannot meet in one file: DxcAndroid.cpp, the one
// file that calls DXC, is compiled with DXC's headers and without the stand-ins (faf_android_dxc,
// ../../CMakeLists.txt). This header has no Windows types.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gpg/gal/effects/ShaderCompiler.h"

namespace faf::android::dxc
{
  /** What a compile hands back. */
  enum class Output : std::uint8_t
  {
    Object,       ///< DXC_OUT_OBJECT: the shader
    Preprocessed, ///< DXC_OUT_HLSL: the source after the preprocessor (-P), as text
  };

  struct Result
  {
    bool succeeded = false;
    std::vector<std::uint8_t> output;
    std::string messages; ///< errors, or warnings on success
  };

  /**
   * Why libdxcompiler.so could not be loaded; empty when it was. DXC is loaded on first use, as
   * gpg/gal/effects/ShaderCompiler.cpp loads dxcompiler.dll: from the folder of the program that
   * holds this code (the app's libraries, where the APK's libdxcompiler.so is), else from the
   * folder in FAF_DXC_DIR.
   */
  [[nodiscard]] const std::string& LoadError();

  /** "dxc <major>.<minor> unsigned" (the shader cache's key, as on Windows without dxil.dll). */
  [[nodiscard]] const std::string& Version();

  /** Runs IDxcCompiler3::Compile on UTF-8 `source` with `arguments`, on a compiler of this thread's. */
  [[nodiscard]] Result Compile(std::string_view source, const std::vector<std::wstring>& arguments, Output output);

  /** The input signature of a DXIL shader, as ShaderCompiler::ReflectInputs gives it. */
  [[nodiscard]] std::vector<gpg::gal::effects::ShaderInput> ReflectInputs(const std::vector<std::uint8_t>& dxil);
} // namespace faf::android::dxc
