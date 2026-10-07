// fxmeta: effect metadata from the portable front end, as JSON
// (schema in gpg/gal/fx/FxJson.h).
//
//   fxmeta [--compat d3d9states.compat] [-D NAME[=VALUE]]... [--out FILE] effect.fx
//   fxmeta --preprocess [--compat ...] effect.fx
//
// --compat is prepended byte for byte, as CD3DEffect::InitEffectFromFile
// does (CD3DEffectTechnique.cpp:459-476); -D adds an EffectContext macro
// (D3DXMACRO). Exit code 0 on success, 1 when the effect has errors (printed
// to stderr), 2 on bad usage or unreadable files.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "gpg/gal/fx/FxJson.h"
#include "gpg/gal/fx/FxMetadata.h"

namespace {

  bool ReadFile(const std::string& path, std::string& text)
  {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      return false;
    }
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
  }

  std::string BaseName(const std::string& path)
  {
    const std::size_t slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
  }

  int Usage()
  {
    std::cerr << "usage: fxmeta [--compat FILE] [-D NAME[=VALUE]]... [--out FILE] [--preprocess] EFFECT.fx\n";
    return 2;
  }

} // namespace

int main(int argc, char** argv)
{
  using namespace gpg::gal::fx;

  std::string compatPath;
  std::string effectPath;
  std::string outPath;
  bool preprocessOnly = false;
  EffectInput input;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--compat" && i + 1 < argc) {
      compatPath = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      outPath = argv[++i];
    } else if (arg == "--preprocess") {
      preprocessOnly = true;
    } else if (arg.rfind("-D", 0) == 0) {
      std::string definition = arg.substr(2);
      if (definition.empty() && i + 1 < argc) {
        definition = argv[++i];
      }
      const std::size_t equals = definition.find('=');
      MacroDefinition macro;
      macro.name = definition.substr(0, equals);
      macro.value = (equals == std::string::npos) ? "1" : definition.substr(equals + 1);
      input.macros.push_back(macro);
    } else if (!arg.empty() && arg[0] == '-') {
      return Usage();
    } else if (effectPath.empty()) {
      effectPath = arg;
    } else {
      return Usage();
    }
  }
  if (effectPath.empty()) {
    return Usage();
  }

  if (!compatPath.empty()) {
    std::string compat;
    if (!ReadFile(compatPath, compat)) {
      std::cerr << "fxmeta: cannot read " << compatPath << "\n";
      return 2;
    }
    input.parts.emplace_back(BaseName(compatPath), compat);
  }
  std::string effect;
  if (!ReadFile(effectPath, effect)) {
    std::cerr << "fxmeta: cannot read " << effectPath << "\n";
    return 2;
  }
  input.parts.emplace_back(BaseName(effectPath), effect);

  std::string output;
  int status = 0;
  if (preprocessOnly) {
    std::vector<Diagnostic> diagnostics;
    SourceBuffer source;
    output = PreprocessEffect(input, diagnostics, source);
    std::cerr << FormatDiagnostics(source, diagnostics);
    for (const Diagnostic& diagnostic : diagnostics) {
      if (diagnostic.severity == Diagnostic::Severity::Error) {
        status = 1;
      }
    }
  } else {
    const FrontEndResult result = BuildEffectMetadata(input);
    std::cerr << FormatDiagnostics(result.source, result.diagnostics);
    if (!result.ok) {
      return 1;
    }
    output = EffectMetadataToJson(result.metadata);
  }

  if (outPath.empty()) {
    std::cout << output;
  } else {
    std::ofstream file(outPath, std::ios::binary);
    file << output;
    if (!file) {
      std::cerr << "fxmeta: cannot write " << outPath << "\n";
      return 2;
    }
  }
  return status;
}
