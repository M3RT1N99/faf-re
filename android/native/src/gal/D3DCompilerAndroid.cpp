// D3DPreprocess for the Android client build (android/native/include/directx/d3dcompiler.h).
// d3dcompiler_47 is Windows-only. Until the effects get a preprocessor here (CLIENT_PLAN.md, C2),
// it fails the way d3dcompiler fails on a bad source: E_FAIL and the reason as the error blob,
// which DeviceVulkan::CreateEffect reports with the effect's name.

#include <d3dcompiler.h>

#include <atomic>
#include <string>
#include <vector>

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
} // namespace

HRESULT D3DPreprocess(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, ID3DBlob** const codeText, ID3DBlob** const errorMessages)
{
  if (codeText != nullptr) {
    *codeText = nullptr;
  }
  if (errorMessages != nullptr) {
    *errorMessages = new Blob("no effect preprocessor on Android yet (android/CLIENT_PLAN.md, C2)");
  }
  return E_FAIL;
}
