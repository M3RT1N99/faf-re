#pragma once

// DXC for the Android client build: libdxcompiler.so (cmake/FafAndroidDxc.cmake), for the effects'
// shader compiler (ShaderCompilerAndroid.cpp).
//
// DXC's headers declare the Windows types themselves on Linux and Android (dxc/WinAdapter.h), as
// the client's Win32 stand-ins do, so the two cannot meet in one file: DxcAndroid.cpp, the one
// file that calls DXC, is compiled with DXC's headers and without the stand-ins (faf_android_dxc,
// ../../CMakeLists.txt). This header has no Windows types.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace faf::android::dxc
{
  struct Result
  {
    bool succeeded = false;
    std::vector<std::uint8_t> output; ///< the shader
    std::string messages;             ///< errors, or warnings on success
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
  [[nodiscard]] Result Compile(std::string_view source, const std::vector<std::wstring>& arguments);
} // namespace faf::android::dxc
