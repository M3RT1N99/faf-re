// D3DPreprocess for the Android client build (android/native/include/directx/d3dcompiler.h):
// d3dcompiler_47's preprocessor, which the Vulkan backend runs on every effect before its
// translator (DeviceVulkan::CreateEffect), as FxcPreprocessor.cpp writes it. The blobs are
// d3dcompiler's: the text with its terminating NUL, and the messages only when there are any.
// Without an include handler, and the backend passes none, an #include fails as in d3dcompiler_47.

#include <d3dcompiler.h>

#include <unistd.h>

#include <atomic>
#include <string>
#include <string_view>
#include <vector>

#include "FxcPreprocessor.h"

namespace
{
  class Blob final : public ID3DBlob
  {
  public:
    explicit Blob(const std::string& text)
      : mBytes(text.begin(), text.end())
    {
      mBytes.push_back('\0');
    }

    HRESULT QueryInterface(REFIID, void** const object) override
    {
      if (object != nullptr) {
        *object = nullptr;
      }
      return E_NOINTERFACE;
    }

    ULONG AddRef() override
    {
      return ++mReferences;
    }

    ULONG Release() override
    {
      const ULONG left = --mReferences;
      if (left == 0U) {
        delete this;
      }
      return left;
    }

    LPVOID GetBufferPointer() override
    {
      return mBytes.data();
    }

    SIZE_T GetBufferSize() override
    {
      return mBytes.size();
    }

  private:
    ~Blob() = default;

    std::vector<char> mBytes;
    std::atomic<ULONG> mReferences{1U};
  };

  /**
   * The source's name made absolute, as d3dcompiler_47 makes it with GetFullPathNameA for its
   * #line directives, __FILE__ and messages: a path from the root stays as it is, any other is
   * taken from the working directory.
   */
  [[nodiscard]] std::string FullPath(const char* const name)
  {
    if (name == nullptr) {
      return {};
    }
    if (name[0] == '\0' || name[0] == '/') {
      return name;
    }
    char directory[4096]{};
    if (::getcwd(directory, sizeof(directory)) == nullptr) {
      return name;
    }
    std::string path(directory);
    if (path.empty() || path.back() != '/') {
      path += '/';
    }
    return path + name;
  }
} // namespace

HRESULT D3DPreprocess(
  const LPCVOID source,
  const SIZE_T sourceBytes,
  const LPCSTR sourceName,
  const D3D_SHADER_MACRO* const defines,
  ID3DInclude*,
  ID3DBlob** const codeText,
  ID3DBlob** const errorMessages
)
{
  if (codeText != nullptr) {
    *codeText = nullptr;
  }
  if (errorMessages != nullptr) {
    *errorMessages = nullptr;
  }
  // A macro without a definition is an empty one, as in d3dcompiler_47.
  std::vector<faf::android::fxc::Macro> macros;
  for (const D3D_SHADER_MACRO* macro = defines; macro != nullptr && macro->Name != nullptr; ++macro) {
    macros.push_back({macro->Name, (macro->Definition != nullptr) ? macro->Definition : ""});
  }
  const std::string_view text(static_cast<const char*>(source), (source != nullptr) ? sourceBytes : 0U);
  const faf::android::fxc::Output output = faf::android::fxc::Preprocess(text, FullPath(sourceName), macros);
  if (errorMessages != nullptr && !output.messages.empty()) {
    *errorMessages = new Blob(output.messages);
  }
  if (output.failed) {
    return E_FAIL;
  }
  if (codeText != nullptr) {
    *codeText = new Blob(output.text);
  }
  return S_OK;
}
