#pragma once

// FxEffectParser: the D3D9 effect grammar (fx_2_0) at effect level.
//
// Global declarations with modifiers (static, uniform, extern, shared, const,
// volatile, row_major, column_major), semantics, register bindings,
// annotations and initializers (expressions, constructors, { } lists,
// sampler_state blocks); struct and typedef declarations; function
// declarations, whose bodies are skipped by brace matching; techniques and
// passes with annotations and state assignments. Errors are reported with
// the merged-buffer position; parsing stops at the first error.

#include <vector>

#include "gpg/gal/fx/FxAst.h"
#include "gpg/gal/fx/FxLexer.h"

namespace gpg::gal::fx {

  /// Parses preprocessed tokens (Preprocess() output). On error, a Diagnostic
  /// is added and the returned tree holds what was parsed before it.
  [[nodiscard]] EffectAst ParseEffect(const std::vector<Token>& tokens, std::vector<Diagnostic>& diagnostics);

} // namespace gpg::gal::fx
