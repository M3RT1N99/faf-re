#pragma once

// d3dcompiler_47's preprocessor (D3DPreprocess) for the Android client build. The Vulkan backend
// preprocesses an effect as it does on Windows (DeviceVulkan::CreateEffect), and the effect
// translator after it (d3d11::TranslateEffectSource) reads the text d3dcompiler_47 writes, which
// is not what a C preprocessor writes: every token is followed by one space, lines are indented
// by brace depth, at most one blank line follows another, and #line directives bring the output
// back in step with the source. This implementation writes that text byte for byte, with the
// rules d3dcompiler_47 10.0.26100 follows where they depart from C (FxcPreprocessor.cpp), as the
// test that runs both on the effects and on generated sources checks (tests/fxc).
//
// It has no Windows types, so the test builds it on Windows next to d3dcompiler_47.

#include <string>
#include <string_view>
#include <vector>

namespace faf::android::fxc
{
  /** A macro defined before the source (D3D_SHADER_MACRO); a null definition is an empty one. */
  struct Macro
  {
    std::string name;
    std::string definition;
  };

  struct Output
  {
    bool failed = false;  ///< #error or #include: D3DPreprocess fails with E_FAIL and writes no text
    std::string text;     ///< the preprocessed source, without the terminating NUL of the blob
    std::string messages; ///< errors and warnings, each on a line of its own, as d3dcompiler_47 words them
  };

  /**
   * Preprocesses `source`. `path` is the source's name as d3dcompiler_47 prints it, made absolute
   * (GetFullPathNameA), in the first #line directive, in __FILE__ and in the messages.
   */
  [[nodiscard]] Output Preprocess(std::string_view source, std::string_view path, const std::vector<Macro>& macros);
} // namespace faf::android::fxc
