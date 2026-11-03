// fxc_compare (Windows): d3dcompiler_47's D3DPreprocess against the Android build's preprocessor
// (src/gal/FxcPreprocessor.cpp), on the same input, byte for byte. README.md, "The effects'
// preprocessor".
//
//   fxc_compare effects <d3d10states.compat> <effect.fx> ...
//       The effects as the Vulkan backend preprocesses them (DeviceVulkan::CreateEffect): the
//       compatibility states and the effect in one buffer, named /effects/<file>, with the
//       backend's macros.
//   fxc_compare generate <seed> <count> [<folder>]
//       Generated sources: the constructs the effects use and their neighbours (macros with # and
//       ##, recursion, conditions, comments, continuations, braces, numbers, CRLF). Each one that
//       differs is written to <folder> with both outputs.
//
// The text must be the same, and so must the call's result. Messages that differ are counted
// apart: d3dcompiler_47 returns them besides a successful result, and the engine reads them only
// when the call fails.

#include <windows.h>

#include <d3dcompiler.h>
#include <fcntl.h>
#include <io.h>

#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/gal/FxcPreprocessor.h"

namespace
{
  using D3DPreprocessProc = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, ID3DBlob**, ID3DBlob**);

  struct Result
  {
    bool failed = false;
    std::string text;
    std::string messages;
  };

  D3DPreprocessProc gPreprocess = nullptr;

  [[nodiscard]] std::string BlobText(ID3DBlob* const blob)
  {
    if (blob == nullptr) {
      return {};
    }
    std::string text(static_cast<const char*>(blob->GetBufferPointer()), blob->GetBufferSize());
    if (!text.empty() && text.back() == '\0') {
      text.pop_back();
    }
    blob->Release();
    return text;
  }

  [[nodiscard]] Result Fxc(const std::string& source, const std::string& name, const std::vector<faf::android::fxc::Macro>& macros)
  {
    std::vector<D3D_SHADER_MACRO> defines;
    for (const auto& macro : macros) {
      defines.push_back(D3D_SHADER_MACRO{macro.name.c_str(), macro.definition.c_str()});
    }
    defines.push_back(D3D_SHADER_MACRO{nullptr, nullptr});
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    const HRESULT hr = gPreprocess(source.data(), source.size(), name.c_str(), defines.data(), nullptr, &code, &errors);
    Result result;
    result.failed = FAILED(hr);
    result.text = BlobText(code);
    result.messages = BlobText(errors);
    return result;
  }

  [[nodiscard]] Result Ours(const std::string& source, const std::string& name, const std::vector<faf::android::fxc::Macro>& macros)
  {
    char full[MAX_PATH]{};
    const DWORD length = ::GetFullPathNameA(name.c_str(), MAX_PATH, full, nullptr);
    const faf::android::fxc::Output output =
      faf::android::fxc::Preprocess(source, std::string_view(full, length), macros);
    return Result{output.failed, output.failed ? std::string() : output.text, output.messages};
  }

  [[nodiscard]] std::string ReadFile(const char* const path)
  {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
  }

  void WriteFile(const std::string& path, const std::string& text)
  {
    std::ofstream file(path, std::ios::binary);
    file << text;
  }

  /** Where the texts first differ: line and column. */
  [[nodiscard]] std::string FirstDifference(const std::string& a, const std::string& b)
  {
    std::size_t i = 0U;
    int line = 1;
    int column = 1;
    while (i < a.size() && i < b.size() && a[i] == b[i]) {
      if (a[i] == '\n') {
        ++line;
        column = 1;
      } else {
        ++column;
      }
      ++i;
    }
    char text[64]{};
    std::snprintf(text, sizeof(text), "line %d, column %d", line, column);
    return text;
  }

  const std::vector<faf::android::fxc::Macro> kBackendMacros = {
    {"technique", "technique10"}, {"VERSION", "DIRECT3D10"}, {"vs_1_1", "vs_4_0"},   {"vs_1_3", "vs_4_0"},
    {"vs_1_4", "vs_4_0"},         {"vs_2_0", "vs_4_0"},      {"vs_3_0", "vs_4_0"},   {"ps_1_1", "ps_4_0"},
    {"ps_1_3", "ps_4_0"},         {"ps_1_4", "ps_4_0"},      {"ps_2_0", "ps_4_0"},   {"ps_2_a", "ps_4_0"},
    {"ps_2_b", "ps_4_0"},         {"ps_3_0", "ps_4_0"},      {"MipFilter", "Filter"}, {"MinFilter", "Filter"},
    {"MagFilter", "Filter"},      {"NONE", "MIN_MAG_MIP_POINT"}, {"LINEAR", "MIN_MAG_MIP_LINEAR"},
    {"POINT", "MIN_MAG_MIP_POINT"}, {"VULKAN", "1"},
  };

  int Effects(const int argc, char** const argv)
  {
    const std::string compat = ReadFile(argv[2]);
    int differing = 0;
    for (int i = 3; i < argc; ++i) {
      const std::string effect = ReadFile(argv[i]);
      std::string file = argv[i];
      file = file.substr(file.find_last_of("\\/") + 1U);
      const std::string name = "/effects/" + file;
      const std::string source = compat + effect;
      const Result fxc = Fxc(source, name, kBackendMacros);
      const Result ours = Ours(source, name, kBackendMacros);
      const bool same = fxc.failed == ours.failed && fxc.text == ours.text;
      std::printf("%s %s: %zu bytes%s%s\n", same ? "SAME" : "DIFFERS", argv[i], fxc.text.size(),
                  same ? "" : (" (first difference at " + FirstDifference(fxc.text, ours.text) + ")").c_str(),
                  fxc.messages == ours.messages ? "" : ", messages differ");
      if (!same) {
        ++differing;
        WriteFile(std::string(argv[i]) + ".fxc.txt", fxc.text);
        WriteFile(std::string(argv[i]) + ".ours.txt", ours.text);
      }
    }
    return differing == 0 ? 0 : 1;
  }

  // --- Generated sources -------------------------------------------------------------------------
  class Generator
  {
  public:
    explicit Generator(const unsigned seed)
      : mRandom(seed)
    {}

    [[nodiscard]] std::string Source()
    {
      mText.clear();
      mObjectMacros.clear();
      mFunctionMacros.clear();
      mNewline = Chance(15) ? "\r\n" : "\n";
      const int lines = Range(3, 40);
      Block(lines, 0);
      if (Chance(20)) {
        mText.pop_back(); // no newline at the end
        if (!mText.empty() && mText.back() == '\r') {
          mText.pop_back();
        }
      }
      return mText;
    }

    [[nodiscard]] std::vector<faf::android::fxc::Macro> Macros()
    {
      std::vector<faf::android::fxc::Macro> macros;
      const int count = Range(0, 3);
      for (int i = 0; i < count; ++i) {
        macros.push_back({Pick({"A", "B", "T", "N", "LINEAR", "technique", "F"}), Pick({"1", "", "x y", "A", "B + 1", "(2)"})});
      }
      return macros;
    }

  private:
    [[nodiscard]] int Range(const int low, const int high)
    {
      return std::uniform_int_distribution<int>(low, high)(mRandom);
    }

    [[nodiscard]] bool Chance(const int percent)
    {
      return Range(0, 99) < percent;
    }

    [[nodiscard]] std::string Pick(std::initializer_list<const char*> items)
    {
      const int index = Range(0, static_cast<int>(items.size()) - 1);
      return *(items.begin() + index);
    }

    void Line(const std::string& text)
    {
      mText += text;
      mText += mNewline;
    }

    [[nodiscard]] std::string Indent()
    {
      switch (Range(0, 5)) {
      case 0:
        return "\t";
      case 1:
        return "    ";
      case 2:
        return "\t\t";
      case 3:
        return " ";
      default:
        return "";
      }
    }

    [[nodiscard]] std::string Name()
    {
      return Pick({"a", "b", "x", "y", "A", "B", "T", "N", "F", "G", "H", "P", "S", "LINEAR", "MipFilter", "technique", "Tile", "n"});
    }

    [[nodiscard]] std::string Number()
    {
      return Pick({"0", "1", "2", "10", "0x0F", "0xff", "010", "08", "1.0", "1.0f", ".5", "5.", "1e3", "1.e+2", "2.5h", "3u", "4L",
                   "1.0.0", "0x", "1e", "1abc", "09.5", "255.0", "0.0039", "1.0lf", "7ul"});
    }

    [[nodiscard]] std::string Punct()
    {
      // No lone parentheses: one after a function-like macro's name would take in the lines after it.
      return Pick({";", ",", "[", "]", "=", "+", "-", "*", "/", "<", ">", "<=", ">=", "==", "!=", "&&", "||", "++", "+=",
                   "->", ".", ":", "::", "?", "!", "~", "%", "&", "|", "^", "<<", ">>", "#", "##", "@", "\\"});
    }

    [[nodiscard]] std::string Comment(const bool multiLine)
    {
      if (multiLine) {
        return "/* c" + mNewline + Indent() + "d */";
      }
      return Pick({"/* c */", "/**/", "/* * / */"});
    }

    /** A token or a small construct. */
    [[nodiscard]] std::string Item(const int depth)
    {
      switch (Range(0, 15)) {
      case 0:
      case 1:
      case 2:
        return Name();
      case 3:
        return Number();
      case 4:
      case 5:
        return Punct();
      case 6:
        return Pick({"\"s\"", "\"a  b\"", "\"q\\\"r\"", "'c'", "'\\n'", "\"\""});
      case 7:
        return Pick({"{", "}", "{", "}"});
      case 8:
        return Comment(false);
      case 9:
      case 10:
        return Invocation(depth);
      case 11:
        return Pick({"a::b", "::a", "std::vector<int>", "a :: b"});
      default:
        switch (Range(0, 2)) {
        case 0:
          return Name() + "(" + Name() + ")";
        case 1:
          return Name() + "[" + Name() + "]";
        default:
          return Name() + "." + Name();
        }
      }
    }

    [[nodiscard]] std::string Invocation(const int depth)
    {
      if (mFunctionMacros.empty() || depth > 2) {
        return Name();
      }
      const auto& [name, parameters] = mFunctionMacros[static_cast<std::size_t>(Range(0, static_cast<int>(mFunctionMacros.size()) - 1))];
      std::string text = name + (Chance(10) ? " " : "") + "(";
      const int count = Chance(10) ? parameters + (Chance(50) ? 1 : -1) : parameters;
      for (int i = 0; i < count; ++i) {
        if (i != 0) {
          text += Chance(10) ? "," + mNewline : ",";
        }
        if (Chance(5)) {
          continue; // an empty argument
        }
        const int items = Range(1, 3);
        for (int j = 0; j < items; ++j) {
          text += (j != 0 ? " " : "") + (Chance(30) ? Invocation(depth + 1) : Pick({"a", "b", "1", "2.0", "x y", "(1,2)", "T", "A", "LINEAR"}));
        }
      }
      return text + ")";
    }

    void TextLine(const int depth)
    {
      std::string text = Indent();
      const int items = Range(1, 8);
      for (int i = 0; i < items; ++i) {
        text += Item(depth);
        text += Pick({" ", " ", "", "\t", "  "});
      }
      if (Chance(15)) {
        text += "// comment" + std::string(Chance(10) ? " \\" : "");
      }
      if (Chance(5)) {
        text += Comment(true);
      }
      if (Chance(3)) {
        text += " \\";
      }
      Line(text);
    }

    [[nodiscard]] std::string Body(const std::vector<std::string>& parameters)
    {
      std::string body;
      const int items = Range(0, 6);
      for (int i = 0; i < items; ++i) {
        if (!parameters.empty() && Chance(40)) {
          const std::string& parameter = parameters[static_cast<std::size_t>(Range(0, static_cast<int>(parameters.size()) - 1))];
          switch (Range(0, 4)) {
          case 0:
            body += "#" + parameter;
            break;
          case 1:
            body += parameter + "##" + Pick({"Tile", "1", "x", "_y"});
            break;
          case 2:
            body += Pick({"t", "q", "1"}) + std::string(" ## ") + parameter;
            break;
          default:
            body += parameter;
            break;
          }
        } else {
          // Balanced, as in the effects' macros: no bracket alone, a group like DECLARE_STRATUM's.
          if (Chance(8)) {
            static constexpr const char* kGroups[][2] = {{"{ ", " }"}, {"( ", " )"}, {"[ ", " ]"}};
            const auto& group = kGroups[Range(0, 2)];
            body += group[0] + Name() + " " + Pick({";", ",", "="}) + " " + Number() + group[1] + " ";
          }
          std::string punct = Punct();
          while (punct == "[" || punct == "]") {
            punct = Punct();
          }
          body += Chance(30) ? Name() : (Chance(50) ? punct : Number());
        }
        body += Chance(10) ? " \\" + mNewline + Indent() : " ";
        if (Chance(5)) {
          body += "/* b */ ";
        }
      }
      if (Chance(5)) {
        body += "// tail";
      }
      return body;
    }

    void DefineLine()
    {
      const std::string name = Name();
      if (Chance(55)) {
        std::vector<std::string> parameters;
        const int count = Range(1, 2);
        parameters.push_back(Pick({"p", "q", "n", "x"}));
        if (count == 2) {
          parameters.push_back(parameters[0] == "q" ? "p" : "q");
        }
        std::string text = Indent() + "#" + (Chance(10) ? " " : "") + "define " + name + "(";
        for (std::size_t i = 0; i < parameters.size(); ++i) {
          text += (i != 0 ? "," : "") + parameters[i];
        }
        text += ") " + Body(parameters);
        Line(text);
        mFunctionMacros.emplace_back(name, static_cast<int>(parameters.size()));
      } else {
        Line(Indent() + "#define " + name + (Chance(10) ? "" : " " + Body({})));
        mObjectMacros.push_back(name);
      }
    }

    [[nodiscard]] std::string Expression(const int depth)
    {
      if (depth > 2 || Chance(30)) {
        return Pick({"0", "1", "2", "0x10", "010", "'a'", "A", "T", "N", "defined(A)", "defined T", "defined ( F )", "-1", "4294967295"});
      }
      switch (Range(0, 7)) {
      case 0:
        return "(" + Expression(depth + 1) + ")";
      case 1:
        return "!" + Expression(depth + 1);
      case 2:
        return Expression(depth + 1) + " ? " + Expression(depth + 1) + " : " + Expression(depth + 1);
      default:
        return Expression(depth + 1) + " " +
          Pick({"&&", "||", "==", "!=", "<", ">", "<=", ">=", "+", "-", "*", "/", "|", "&", "^", "<<", "%"}) + " " + Expression(depth + 1);
      }
    }

    void Conditional(const int lines, const int depth)
    {
      switch (Range(0, 3)) {
      case 0:
        Line(Indent() + "#ifdef " + Name());
        break;
      case 1:
        Line(Indent() + "#ifndef " + Name());
        break;
      case 2:
        Line("#if " + Pick({"0", "1"}));
        break;
      default:
        Line("#if " + Expression(0));
        break;
      }
      Block(Range(0, lines), depth + 1);
      while (Chance(30)) {
        Line("#elif " + Expression(0));
        Block(Range(0, lines), depth + 1);
      }
      if (Chance(50)) {
        Line("#else" + std::string(Chance(10) ? " junk" : ""));
        Block(Range(0, lines), depth + 1);
      }
      Line("#endif");
    }

    void Block(const int lines, const int depth)
    {
      for (int i = 0; i < lines; ++i) {
        const int kind = Range(0, 99);
        if (kind < 12) {
          Line(Chance(20) ? Indent() : "");
        } else if (kind < 22) {
          DefineLine();
        } else if (kind < 26 && depth < 3) {
          Conditional(4, depth);
        } else if (kind < 28) {
          Line("#undef " + Name());
        } else if (kind < 30) {
          Line(Indent() + Comment(false));
        } else {
          TextLine(0);
        }
      }
    }

    std::mt19937 mRandom;
    std::string mText;
    std::string mNewline;
    std::vector<std::string> mObjectMacros;
    std::vector<std::pair<std::string, int>> mFunctionMacros;
  };

  int Generate(const unsigned seed, const int count, const char* const folder)
  {
    Generator generator(seed);
    int valid = 0;
    int differing = 0;
    int erroneousDiffering = 0;
    int messages = 0;
    for (int i = 0; i < count; ++i) {
      const std::string source = generator.Source();
      const std::vector<faf::android::fxc::Macro> macros = generator.Macros();
      const Result fxc = Fxc(source, "/g.fx", macros);
      const Result ours = Ours(source, "/g.fx", macros);
      // A source d3dcompiler_47 reports errors for is one the effects would not be: its recovery
      // from them is counted apart.
      const bool erroneous = fxc.failed || fxc.messages.find(": error") != std::string::npos;
      valid += erroneous ? 0 : 1;
      if (fxc.messages != ours.messages) {
        ++messages;
      }
      if (fxc.failed == ours.failed && fxc.text == ours.text) {
        continue;
      }
      if (erroneous) {
        ++erroneousDiffering;
        continue;
      }
      ++differing;
      if (folder != nullptr && differing <= 50) {
        const std::string stem = std::string(folder) + "\\case" + std::to_string(i);
        WriteFile(stem + ".fx", source);
        std::string defines;
        for (const auto& macro : macros) {
          defines += macro.name + "=" + macro.definition + "\n";
        }
        WriteFile(stem + ".macros.txt", defines);
        WriteFile(stem + ".fxc.txt", fxc.text + "\n--- messages\n" + fxc.messages);
        WriteFile(stem + ".ours.txt", ours.text + "\n--- messages\n" + ours.messages);
      }
    }
    std::printf(
      "generated %d sources (seed %u): %d without errors, %d of them differ; %d of the %d with errors differ; messages differ in %d\n",
      count, seed, valid, differing, erroneousDiffering, count - valid, messages
    );
    return differing == 0 ? 0 : 1;
  }
} // namespace

int main(const int argc, char** const argv)
{
  HMODULE compiler = ::LoadLibraryA("d3dcompiler_47.dll");
  gPreprocess = (compiler != nullptr) ? reinterpret_cast<D3DPreprocessProc>(::GetProcAddress(compiler, "D3DPreprocess")) : nullptr;
  if (gPreprocess == nullptr) {
    std::fprintf(stderr, "d3dcompiler_47.dll with D3DPreprocess not found\n");
    return 2;
  }
  if (argc >= 4 && std::string(argv[1]) == "effects") {
    return Effects(argc, argv);
  }
  if (argc == 3 && std::string(argv[1]) == "one") {
    // The Android preprocessor's text for one file named /g.fx, on stdout (tests/fxc tooling).
    const Result ours = Ours(ReadFile(argv[2]), "/g.fx", {});
    ::_setmode(::_fileno(stdout), _O_BINARY);
    std::fwrite(ours.text.data(), 1U, ours.text.size(), stdout);
    return 0;
  }
  if (argc >= 4 && std::string(argv[1]) == "generate") {
    return Generate(static_cast<unsigned>(std::stoul(argv[2])), std::stoi(argv[3]), argc >= 5 ? argv[4] : nullptr);
  }
  std::fprintf(stderr, "usage: fxc_compare effects <compat> <effect.fx>...\n       fxc_compare generate <seed> <count> [<folder>]\n");
  return 2;
}
