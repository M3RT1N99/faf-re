#pragma once

// FxPreprocessor: the C preprocessor as D3DX applies it to an effect.
//
// - Object-like and function-like macros with hide sets (Prosser's
//   algorithm), # and ## (terrain.fx DECLARE_STRATUM pastes n##Texture four
//   times), argument pre-expansion and rescanning.
// - #if/#ifdef/#ifndef/#elif/#else/#endif with `defined`, integer arithmetic
//   and C operator precedence. Identifiers left after expansion are 0.
// - #define/#undef, #error (an error), #pragma and #line (ignored with a
//   warning).
// - #include is an error: the engine passes no include handler to
//   D3DXCreateEffectCompiler (D3D9Interfaces.cpp:1557-1559), and D3DX fails
//   an #include from a memory buffer without one.
//
// Macros given by the caller correspond to EffectContext::mMacros (the
// D3DXMACRO list; FAF_BONE_TEXTURE=1 is the only one the engine adds,
// CD3DEffectTechnique.cpp:491-513).

#include <string>
#include <vector>

#include "gpg/gal/fx/FxLexer.h"

namespace gpg::gal::fx {

  struct MacroDefinition
  {
    std::string name;
    std::string value; // D3DXMACRO::Definition
  };

  /// Runs the preprocessor over lexed text. Returns the expanded tokens
  /// (no Newline tokens) followed by one End token. Errors go to
  /// `diagnostics`; after an error the output is still complete but should not
  /// be trusted.
  [[nodiscard]] std::vector<Token> Preprocess(
    const std::vector<Token>& lexed, const std::vector<MacroDefinition>& macros, std::vector<Diagnostic>& diagnostics
  );

  /// The expanded tokens as text, one output line per source line that
  /// produced tokens (close to `fxc /P` output, for inspection and tests).
  [[nodiscard]] std::string FormatPreprocessed(const std::vector<Token>& tokens);

} // namespace gpg::gal::fx
