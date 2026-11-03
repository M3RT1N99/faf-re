// DXC for the Android client build (DxcAndroid.h), with DXC's own headers: on Linux and Android
// dxcapi.h declares the Windows types itself (dxc/WinAdapter.h, with its CComPtr), and the D3D12
// reflection comes from the DirectX-Headers as DXC's build includes them (dxc/Support/D3DReflection.h).
// The loading, the compiler per thread and the reflection follow gpg/gal/effects/ShaderCompiler.cpp;
// dxil.dll, the DXIL signer, is not needed for SPIR-V and not built.

#include "DxcAndroid.h"

#include <dlfcn.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "dxc/Support/D3DReflection.h"
#include "dxc/dxcapi.h"

namespace faf::android::dxc
{
  namespace
  {
    struct Library
    {
      DxcCreateInstanceProc create = nullptr;
      std::string loadError;
      std::string version;
    };

    /**
     * A DXC compiler and utils object per thread, as in ShaderCompiler.cpp: one object must not be
     * used by two threads at once, but separate objects compile in parallel.
     */
    struct ThreadDxc
    {
      CComPtr<IDxcUtils> utils;
      CComPtr<IDxcCompiler3> compiler;
    };

    [[nodiscard]] ThreadDxc* ThisThreadDxc(const DxcCreateInstanceProc create)
    {
      thread_local ThreadDxc instance;
      thread_local bool tried = false;
      if (!tried) {
        tried = true;
        if (create == nullptr || FAILED(create(CLSID_DxcUtils, IID_PPV_ARGS(&instance.utils))) ||
            FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&instance.compiler)))) {
          instance.utils.Release();
          instance.compiler.Release();
        }
      }
      return instance.compiler != nullptr ? &instance : nullptr;
    }

    /** The folder of the program this code is part of: the app's libraries, or a test's own folder. */
    [[nodiscard]] std::string ProgramDirectory()
    {
      Dl_info info{};
      if (::dladdr(reinterpret_cast<const void*>(&ProgramDirectory), &info) == 0 || info.dli_fname == nullptr) {
        return {};
      }
      const std::string path(info.dli_fname);
      const std::size_t slash = path.find_last_of('/');
      return (slash == std::string::npos) ? std::string() : path.substr(0U, slash + 1U);
    }

    [[nodiscard]] Library Load()
    {
      Library library;
      std::vector<std::string> directories{ProgramDirectory()};
      if (const char* const configured = std::getenv("FAF_DXC_DIR"); configured != nullptr && configured[0] != '\0') {
        std::string directory = configured;
        if (directory.back() != '/') {
          directory += '/';
        }
        directories.push_back(directory);
      }

      // Never closed: the compilers of every thread live until the process ends.
      void* handle = nullptr;
      std::string reason;
      for (const std::string& directory : directories) {
        handle = ::dlopen((directory + "libdxcompiler.so").c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle != nullptr) {
          break;
        }
        const char* const error = ::dlerror();
        reason = (error != nullptr) ? error : "";
      }
      if (handle == nullptr) {
        library.loadError = "libdxcompiler.so not found next to the program or in FAF_DXC_DIR (" + reason + ")";
        return library;
      }

      const auto create = reinterpret_cast<DxcCreateInstanceProc>(::dlsym(handle, "DxcCreateInstance"));
      CComPtr<IDxcUtils> utils;
      CComPtr<IDxcCompiler3> compiler;
      if (create == nullptr || FAILED(create(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) ||
          FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) {
        library.loadError = "libdxcompiler.so does not provide IDxcCompiler3";
        return library;
      }

      CComPtr<IDxcVersionInfo> versionInfo;
      UINT32 major = 0U;
      UINT32 minor = 0U;
      if (SUCCEEDED(compiler.QueryInterface(&versionInfo)) && SUCCEEDED(versionInfo->GetVersion(&major, &minor))) {
        char version[64]{};
        std::snprintf(version, sizeof(version), "dxc %u.%u unsigned", major, minor);
        library.version = version;
      } else {
        library.version = "dxc";
      }
      library.create = create;
      return library;
    }

    [[nodiscard]] const Library& TheLibrary()
    {
      static const Library library = Load();
      return library;
    }
  } // namespace

  const std::string& LoadError()
  {
    return TheLibrary().loadError;
  }

  const std::string& Version()
  {
    return TheLibrary().version;
  }

  Result Compile(const std::string_view source, const std::vector<std::wstring>& arguments, const Output output)
  {
    Result result;
    const Library& library = TheLibrary();
    if (library.create == nullptr) {
      result.messages = library.loadError;
      return result;
    }

    std::vector<LPCWSTR> argumentPointers;
    argumentPointers.reserve(arguments.size());
    for (const std::wstring& argument : arguments) {
      argumentPointers.push_back(argument.c_str());
    }

    DxcBuffer buffer{};
    buffer.Ptr = source.data();
    buffer.Size = source.size();
    buffer.Encoding = DXC_CP_UTF8;

    ThreadDxc* const dxc = ThisThreadDxc(library.create);
    if (dxc == nullptr) {
      result.messages = "DxcCreateInstance failed on this thread";
      return result;
    }
    CComPtr<IDxcResult> compiled;
    const HRESULT hr = dxc->compiler->Compile(
      &buffer, argumentPointers.data(), static_cast<UINT32>(argumentPointers.size()), nullptr, IID_PPV_ARGS(&compiled)
    );
    if (FAILED(hr) || compiled == nullptr) {
      char message[64]{};
      std::snprintf(message, sizeof(message), "IDxcCompiler3::Compile failed (0x%08X)", static_cast<unsigned>(hr));
      result.messages = message;
      return result;
    }

    CComPtr<IDxcBlobUtf8> errors;
    if (SUCCEEDED(compiled->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr)) && errors != nullptr &&
        errors->GetStringLength() != 0U) {
      result.messages.assign(errors->GetStringPointer(), errors->GetStringLength());
    }
    HRESULT status = E_FAIL;
    compiled->GetStatus(&status);
    if (FAILED(status)) {
      return result;
    }

    if (output == Output::Preprocessed) {
      CComPtr<IDxcBlobUtf8> text;
      if (FAILED(compiled->GetOutput(DXC_OUT_HLSL, IID_PPV_ARGS(&text), nullptr)) || text == nullptr) {
        result.messages += "\nno preprocessed source produced";
        return result;
      }
      const auto* const bytes = reinterpret_cast<const std::uint8_t*>(text->GetStringPointer());
      result.output.assign(bytes, bytes + text->GetStringLength());
    } else {
      CComPtr<IDxcBlob> object;
      if (FAILED(compiled->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr)) || object == nullptr) {
        result.messages += "\nno object produced";
        return result;
      }
      const auto* const bytes = static_cast<const std::uint8_t*>(object->GetBufferPointer());
      result.output.assign(bytes, bytes + object->GetBufferSize());
    }
    result.succeeded = true;
    return result;
  }

  std::vector<gpg::gal::effects::ShaderInput> ReflectInputs(const std::vector<std::uint8_t>& dxil)
  {
    using gpg::gal::effects::ShaderInput;
    std::vector<ShaderInput> inputs;
    const Library& library = TheLibrary();
    if (library.create == nullptr || dxil.empty()) {
      return inputs;
    }
    DxcBuffer buffer{};
    buffer.Ptr = dxil.data();
    buffer.Size = dxil.size();
    ThreadDxc* const dxc = ThisThreadDxc(library.create);
    if (dxc == nullptr) {
      return inputs;
    }
    CComPtr<ID3D12ShaderReflection> reflection;
    if (FAILED(dxc->utils->CreateReflection(&buffer, IID_PPV_ARGS(&reflection)))) {
      return inputs;
    }
    D3D12_SHADER_DESC desc{};
    if (FAILED(reflection->GetDesc(&desc))) {
      return inputs;
    }
    for (UINT index = 0U; index < desc.InputParameters; ++index) {
      D3D12_SIGNATURE_PARAMETER_DESC parameter{};
      if (FAILED(reflection->GetInputParameterDesc(index, &parameter))) {
        continue;
      }
      ShaderInput input;
      input.semantic = (parameter.SemanticName != nullptr) ? parameter.SemanticName : "";
      std::transform(input.semantic.begin(), input.semantic.end(), input.semantic.begin(), [](const unsigned char c) {
        return static_cast<char>(std::toupper(c));
      });
      input.index = parameter.SemanticIndex;
      input.component = (parameter.ComponentType == D3D_REGISTER_COMPONENT_UINT32) ? ShaderInput::Component::Uint
        : (parameter.ComponentType == D3D_REGISTER_COMPONENT_SINT32)                ? ShaderInput::Component::Sint
                                                                                    : ShaderInput::Component::Float;
      input.mask = parameter.Mask;
      input.systemValue = parameter.SystemValueType != D3D_NAME_UNDEFINED;
      inputs.push_back(std::move(input));
    }
    return inputs;
  }
} // namespace faf::android::dxc
