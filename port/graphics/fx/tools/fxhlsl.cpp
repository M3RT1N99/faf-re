// fxhlsl: the SM5 HLSL the portable front end generates for every pass stage of an effect
// (gpg/gal/fx/FxHlslEmitter.h), optionally compiled with FXC.
//
//   fxhlsl [--compat d3d9states.compat] [-D NAME[=VALUE]]... [--out DIR] [--fxc] [--all-techniques]
//          EFFECT.fx...
//
// For each technique (only those valid on the "all" device profile unless --all-techniques) and
// pass, the pixel shader is generated first and the vertex shader then writes exactly the varyings
// it reads, as the Diligent backend does. A vertex shader is generated for the vertex layout that
// feeds every input it declares (COLOR0 as D3DCOLOR, as in the engine's vertex formats); a
// `VertexShader = null` pass gets the fixed-function shader for POSITIONT + TEXCOORD0..1 (vertex
// formats 7 and 8, VertexFormatTableD3D9.inl). Unique stages (same entry, arguments and interface)
// are written once to DIR as <effect>.<stage><n>.hlsl with an index.json; --fxc compiles each
// with D3DCompile (d3dcompiler_47, vs_5_0/ps_5_0, Windows only).
//
// Exit code 0 when every stage generates (and compiles with --fxc), 1 otherwise, 2 on bad usage.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "gpg/gal/fx/FxHlslEmitter.h"
#include "gpg/gal/fx/FxMetadata.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

  using namespace gpg::gal::fx;

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
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    const std::size_t dot = name.rfind('.');
    return (dot == std::string::npos) ? name : name.substr(0, dot);
  }

  std::string JsonString(const std::string& text)
  {
    std::string out = "\"";
    for (const char c : text) {
      if (c == '"' || c == '\\') {
        out += '\\';
        out += c;
      } else if (c == '\n') {
        out += "\\n";
      } else if (static_cast<unsigned char>(c) < 0x20) {
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
        out += buffer;
      } else {
        out += c;
      }
    }
    return out + "\"";
  }

#if defined(_WIN32)
  // ID3DBlob as far as D3DCompile's outputs need it.
  struct Blob : IUnknown
  {
    virtual LPVOID STDMETHODCALLTYPE GetBufferPointer() = 0;
    virtual SIZE_T STDMETHODCALLTYPE GetBufferSize() = 0;
  };
  using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR, UINT, UINT, Blob**, Blob**);

  D3DCompileFn LoadCompiler()
  {
    static D3DCompileFn compile = nullptr;
    if (compile == nullptr) {
      if (HMODULE module = LoadLibraryA("d3dcompiler_47.dll")) {
        compile = reinterpret_cast<D3DCompileFn>(reinterpret_cast<void*>(GetProcAddress(module, "D3DCompile")));
      }
    }
    return compile;
  }

  // Returns "" on success, else the compiler's messages; warnings go to `warnings`.
  std::string CompileFxc(const std::string& source, const char* profile, std::string& warnings)
  {
    const D3DCompileFn compile = LoadCompiler();
    if (compile == nullptr) {
      return "d3dcompiler_47.dll not available";
    }
    Blob* code = nullptr;
    Blob* errors = nullptr;
    const HRESULT result = compile(source.data(), source.size(), "fxhlsl", nullptr, nullptr, "main", profile,
                                   0x800U /* D3DCOMPILE_OPTIMIZATION_LEVEL3 */, 0, &code, &errors);
    std::string messages;
    if (errors != nullptr) {
      messages.assign(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
      errors->Release();
    }
    if (code != nullptr) {
      code->Release();
    }
    if (FAILED(result)) {
      return messages.empty() ? "D3DCompile failed" : messages;
    }
    warnings = messages;
    return {};
  }
#endif

  int Usage()
  {
    std::cerr << "usage: fxhlsl [--compat FILE] [-D NAME[=VALUE]]... [--out DIR] [--fxc] [--all-techniques] EFFECT.fx...\n";
    return 2;
  }

  struct Stage
  {
    std::string file;
    std::string kind; // "vs" / "ps"
    std::string entry;
    EmittedStage emitted;
    std::string fxc; // "ok", or the error
    std::string fxcWarnings;
    std::vector<std::string> passes; // technique/pass using it
  };

} // namespace

int main(int argc, char** argv)
{
  std::string compatPath;
  std::string outDir;
  bool fxc = false;
  bool allTechniques = false;
  std::vector<std::string> effectPaths;
  std::vector<MacroDefinition> macros;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--compat" && i + 1 < argc) {
      compatPath = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      outDir = argv[++i];
    } else if (arg == "--fxc") {
      fxc = true;
    } else if (arg == "--all-techniques") {
      allTechniques = true;
    } else if (arg.rfind("-D", 0) == 0) {
      std::string definition = arg.substr(2);
      if (definition.empty() && i + 1 < argc) {
        definition = argv[++i];
      }
      const std::size_t equals = definition.find('=');
      macros.push_back({definition.substr(0, equals), (equals == std::string::npos) ? "1" : definition.substr(equals + 1)});
    } else if (!arg.empty() && arg[0] == '-') {
      return Usage();
    } else {
      effectPaths.push_back(arg);
    }
  }
  if (effectPaths.empty()) {
    return Usage();
  }
  std::string compat;
  if (!compatPath.empty() && !ReadFile(compatPath, compat)) {
    std::cerr << "fxhlsl: cannot read " << compatPath << "\n";
    return 2;
  }

  int failures = 0;
  std::size_t stageCount = 0;
  std::ostringstream index;
  index << "{\n  \"effects\": [\n";
  for (std::size_t e = 0; e < effectPaths.size(); ++e) {
    const std::string& effectPath = effectPaths[e];
    EffectInput input;
    if (!compatPath.empty()) {
      input.parts.emplace_back("d3d9states.compat", compat);
    }
    std::string text;
    if (!ReadFile(effectPath, text)) {
      std::cerr << "fxhlsl: cannot read " << effectPath << "\n";
      return 2;
    }
    const std::string effectName = BaseName(effectPath);
    input.parts.emplace_back(effectName + ".fx", text);
    input.macros = macros;
    const FrontEndResult result = BuildEffectMetadata(input);
    if (!result.ok) {
      std::cerr << FormatDiagnostics(result.source, result.diagnostics);
      ++failures;
      continue;
    }
    std::vector<Diagnostic> diagnostics;
    const std::unique_ptr<HlslEmitter> emitter = HlslEmitter::Create(input, result.metadata, diagnostics);
    if (!emitter) {
      std::cerr << FormatDiagnostics(result.source, diagnostics);
      ++failures;
      continue;
    }
    const DeviceProfile& profile = StandardDeviceProfiles().front();
    std::map<std::string, Stage> stages; // by key
    std::vector<std::string> order;
    std::size_t passCount = 0;
    for (const TechniqueInfo& technique : result.metadata.techniques) {
      if (!allTechniques && !IsTechniqueValid(technique, profile)) {
        continue;
      }
      for (std::size_t p = 0; p < technique.passes.size(); ++p) {
        const PassInfo& pass = technique.passes[p];
        const std::string where = technique.name + "/P" + std::to_string(p);
        ++passCount;
        EmittedStage ps;
        std::string psEntry;
        diagnostics.clear();
        bool ok = true;
        if (pass.pixelShader.kind == ShaderEntry::Kind::Compile) {
          ok = emitter->EmitPixelShader(pass.pixelShader, ps, diagnostics);
          psEntry = pass.pixelShader.entry;
          for (const std::string& argument : pass.pixelShader.arguments) {
            psEntry += "|" + argument;
          }
        } else {
          ok = emitter->EmitFixedFunctionPixelShader(ps, diagnostics);
          psEntry = "<fixed-function>";
        }
        if (!ok) {
          std::cerr << effectName << " " << where << " ps: " << FormatDiagnostics(result.source, diagnostics);
          ++failures;
          continue;
        }
        EmittedStage vs;
        std::string vsEntry;
        VertexInputLayout layout;
        if (pass.vertexShader.kind == ShaderEntry::Kind::Compile) {
          VertexInputLayout full;
          for (InputKind& kind : full.kinds) {
            kind = InputKind::Float;
          }
          full.kinds[kSlotColor0] = InputKind::UNormBgra;
          ok = emitter->EmitVertexShader(pass.vertexShader, full, ps.varyings, vs, diagnostics);
          for (std::uint32_t slot = 0; slot < kVertexSlotCount; ++slot) {
            layout.kinds[slot] = (vs.vertexSlots & (1U << slot)) != 0 ? full.kinds[slot] : InputKind::Absent;
          }
          vsEntry = pass.vertexShader.entry;
          for (const std::string& argument : pass.vertexShader.arguments) {
            vsEntry += "|" + argument;
          }
        } else {
          layout.kinds[kSlotPosition] = InputKind::PositionT;
          layout.kinds[kSlotTexcoord0] = InputKind::Float;
          layout.kinds[kSlotTexcoord0 + 1] = InputKind::Float;
          ok = emitter->EmitFixedFunctionVertexShader(layout, ps.varyings, vs, diagnostics);
          vsEntry = "<fixed-function>";
        }
        if (!ok) {
          std::cerr << effectName << " " << where << " vs: " << FormatDiagnostics(result.source, diagnostics);
          ++failures;
          continue;
        }
        const std::pair<std::string, EmittedStage*> pair[2] = {{"vs:" + vsEntry + ":" + std::to_string(ps.varyings), &vs},
                                                                {"ps:" + psEntry, &ps}};
        for (const auto& [key, emitted] : pair) {
          auto it = stages.find(key);
          if (it == stages.end()) {
            Stage stage;
            stage.kind = key.substr(0, 2);
            stage.entry = key.substr(3);
            stage.emitted = *emitted;
            stage.file = effectName + "." + stage.kind + std::to_string(order.size()) + ".hlsl";
            it = stages.emplace(key, stage).first;
            order.push_back(key);
          }
          it->second.passes.push_back(where);
        }
      }
    }
    // Write and compile.
    index << (e != 0 ? ",\n" : "") << "    {\"effect\": " << JsonString(effectName) << ", \"passes\": " << passCount
          << ", \"stages\": [\n";
    for (std::size_t s = 0; s < order.size(); ++s) {
      Stage& stage = stages[order[s]];
      ++stageCount;
      if (!outDir.empty()) {
        std::ofstream file(outDir + "/" + stage.file, std::ios::binary);
        file << stage.emitted.source;
      }
      stage.fxc = "skipped";
#if defined(_WIN32)
      if (fxc) {
        const std::string error = CompileFxc(stage.emitted.source, stage.kind == "vs" ? "vs_5_0" : "ps_5_0", stage.fxcWarnings);
        stage.fxc = error.empty() ? "ok" : error;
        if (!error.empty()) {
          ++failures;
          std::cerr << effectName << " " << stage.file << " (" << stage.entry << "): FXC error:\n" << error << "\n";
        }
      }
#endif
      index << "      {\"file\": " << JsonString(stage.file) << ", \"stage\": " << JsonString(stage.kind)
            << ", \"entry\": " << JsonString(stage.entry) << ", \"varyings\": " << stage.emitted.varyings
            << ", \"vertexSlots\": " << stage.emitted.vertexSlots << ", \"textures\": [";
      for (std::size_t t = 0; t < stage.emitted.textures.size(); ++t) {
        index << (t != 0 ? ", " : "") << JsonString(stage.emitted.textures[t].name);
      }
      index << "], \"samplers\": [";
      for (std::size_t t = 0; t < stage.emitted.samplers.size(); ++t) {
        index << (t != 0 ? ", " : "") << JsonString(stage.emitted.samplers[t].name);
      }
      index << "], \"params\": " << (stage.emitted.usesParams ? "true" : "false") << ", \"fxc\": " << JsonString(stage.fxc)
            << ", \"fxcWarnings\": " << JsonString(stage.fxcWarnings) << ", \"passes\": [";
      for (std::size_t p = 0; p < stage.passes.size(); ++p) {
        index << (p != 0 ? ", " : "") << JsonString(stage.passes[p]);
      }
      index << "]}" << (s + 1 < order.size() ? "," : "") << "\n";
    }
    index << "    ]}";
    std::cout << effectName << ": " << passCount << " passes, " << order.size() << " unique stages";
    if (fxc) {
      std::size_t ok = 0;
      for (const std::string& key : order) {
        ok += stages[key].fxc == "ok" ? 1U : 0U;
      }
      std::cout << ", FXC " << ok << "/" << order.size();
    }
    std::cout << "\n";
  }
  index << "\n  ]\n}\n";
  if (!outDir.empty()) {
    std::ofstream file(outDir + "/index.json", std::ios::binary);
    file << index.str();
  }
  std::cout << "stages: " << stageCount << ", failures: " << failures << "\n";
  return failures == 0 ? 0 : 1;
}
