// fxtechlist: the D3D9 side of gate 1's technique-list check (M6 step 1, docs/port/renderer.md).
//
// Compiles one effect source buffer exactly as DeviceD3D9::CreateEffectFromSourceBuffer does
// (src/sdk/gpg/gal/backends/d3d9/D3D9Interfaces.cpp:1529-1626: D3DXCreateEffectCompiler with
// D3DXSHADER_DEBUG | D3DXSHADER_USE_LEGACY_D3DX9_31_DLL, CompileEffect with D3DXSHADER_DEBUG,
// D3DXCreateEffect), creates it on a D3D9 device and prints the techniques that
// EffectD3D9::GetTechniques would return (FindNextValidTechnique order, D3D9Interfaces.cpp:4226-4250)
// as one JSON object.
//
// The source buffers are the ones the engine itself handed to gal CreateEffect: the Diligent
// backend writes them with `/galdumpfx <dir>` (nn_<name>.src plus nn_<name>.json with the macros),
// so both sides see the same bytes. They are game data and stay in scratch.
//
//   fxtechlist <hal|oracle> <file.src> [NAME=VALUE ...]
//
// `hal` is the D3D9 backend's device: D3DDEVTYPE_HAL, D3DCREATE_MULTITHREADED |
// D3DCREATE_HARDWARE_VERTEXPROCESSING (behaviour 0x44, D3D9Interfaces.cpp:1772), and the list is
// FindNextValidTechnique's. `oracle` repeats the Diligent backend's rule outside the engine: the
// effect on a NULLREF device (port/graphics/diligent/D3D9Oracle.cpp), where FindNextValidTechnique
// crashes, and a technique kept when its passes' shader versions are within the HAL caps
// (EffectsDiligent.cpp, IsTechniqueValid). Both devices sit on a hidden popup window that is never
// shown, with D3DCREATE_NOWINDOWCHANGES, so neither can take focus.
//
// Build (x86, the same DXSDK D3DX import library main.exe links):
//   cl /nologo /EHsc /std:c++17 /I dependencies\DXSDK_D3DX\include fxtechlist.cpp
//      /link /LIBPATH:dependencies\DXSDK_D3DX\lib\x86 d3d9.lib d3dx9.lib user32.lib
// port/graphics/diligent/tools/gate1.py builds and runs it.

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    std::string JsonEscape(const std::string& text)
    {
        std::string out;
        for (const char c : text) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += c;
            } else if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buffer;
            } else {
                out += c;
            }
        }
        return out;
    }

    int Fail(const std::string& message)
    {
        std::printf("{\"error\": \"%s\"}\n", JsonEscape(message).c_str());
        return 1;
    }

    LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        return ::DefWindowProcW(window, message, wParam, lParam);
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: fxtechlist <hal|oracle> <file.src> [NAME=VALUE ...]\n");
        return 2;
    }
    const std::string deviceKind = argv[1];
    const D3DDEVTYPE deviceType = deviceKind == "hal" ? D3DDEVTYPE_HAL : D3DDEVTYPE_NULLREF;

    std::ifstream file(argv[2], std::ios::binary);
    if (!file.is_open()) {
        return Fail(std::string("cannot open ") + argv[2]);
    }
    const std::vector<char> source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

    std::vector<std::string> names;
    std::vector<std::string> values;
    for (int index = 3; index < argc; ++index) {
        const std::string pair = argv[index];
        const std::size_t equals = pair.find('=');
        names.push_back(pair.substr(0, equals));
        values.push_back(equals == std::string::npos ? std::string() : pair.substr(equals + 1));
    }
    std::vector<D3DXMACRO> defines;
    for (std::size_t index = 0; index < names.size(); ++index) {
        defines.push_back(D3DXMACRO{names[index].c_str(), values[index].c_str()});
    }
    if (!defines.empty()) {
        defines.push_back(D3DXMACRO{nullptr, nullptr});
    }
    const D3DXMACRO* const defineArray = defines.empty() ? nullptr : defines.data();
    bool boneTexture = false;
    for (const std::string& name : names) {
        boneTexture = boneTexture || name == "FAF_BONE_TEXTURE";
    }
    const DWORD flowControl = boneTexture ? D3DXSHADER_AVOID_FLOW_CONTROL : 0U;

    IDirect3D9* const d3d = ::Direct3DCreate9(D3D_SDK_VERSION);
    if (d3d == nullptr) {
        return Fail("Direct3DCreate9 failed");
    }
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = ::GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"FafFxTechList";
    ::RegisterClassExW(&windowClass);
    const HWND window = ::CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, windowClass.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
        windowClass.hInstance, nullptr
    );

    D3DPRESENT_PARAMETERS parameters{};
    parameters.BackBufferWidth = 1;
    parameters.BackBufferHeight = 1;
    parameters.BackBufferFormat = D3DFMT_UNKNOWN;
    parameters.BackBufferCount = 1;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = window;
    parameters.Windowed = TRUE;
    IDirect3DDevice9* device = nullptr;
    HRESULT result = d3d->CreateDevice(
        D3DADAPTER_DEFAULT, deviceType, window,
        D3DCREATE_MULTITHREADED | D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES, &parameters, &device
    );
    if (FAILED(result)) {
        char text[96];
        std::snprintf(text, sizeof(text), "CreateDevice(%s) failed: 0x%08lX", deviceKind.c_str(), static_cast<unsigned long>(result));
        return Fail(text);
    }

    ID3DXEffectCompiler* compiler = nullptr;
    ID3DXBuffer* compiled = nullptr;
    ID3DXBuffer* errors = nullptr;
    result = D3DXCreateEffectCompiler(
        source.data(), static_cast<UINT>(source.size()), defineArray, nullptr,
        D3DXSHADER_DEBUG | D3DXSHADER_USE_LEGACY_D3DX9_31_DLL | flowControl, &compiler, &errors
    );
    if (SUCCEEDED(result)) {
        result = compiler->CompileEffect(D3DXSHADER_DEBUG | flowControl, &compiled, &errors);
    }
    if (FAILED(result)) {
        return Fail(std::string("compile failed: ") + (errors != nullptr ? static_cast<const char*>(errors->GetBufferPointer()) : ""));
    }
    ID3DXEffect* effect = nullptr;
    result = D3DXCreateEffect(
        device, compiled->GetBufferPointer(), compiled->GetBufferSize(), defineArray, nullptr, D3DXSHADER_DEBUG, nullptr,
        &effect, &errors
    );
    if (FAILED(result)) {
        return Fail(std::string("create failed: ") + (errors != nullptr ? static_cast<const char*>(errors->GetBufferPointer()) : ""));
    }

    D3DXEFFECT_DESC effectDesc{};
    effect->GetDesc(&effectDesc);
    std::string json = "{\"device\": \"" + deviceKind + "\", \"techniques\": " + std::to_string(effectDesc.Techniques) +
                       ", \"parameters\": " + std::to_string(effectDesc.Parameters) + ", \"validTechniques\": [";
    bool first = true;
    if (deviceType == D3DDEVTYPE_HAL) {
        D3DXHANDLE technique = nullptr;
        result = effect->FindNextValidTechnique(nullptr, &technique);
        while (SUCCEEDED(result) && technique != nullptr) {
            D3DXTECHNIQUE_DESC desc{};
            effect->GetTechniqueDesc(technique, &desc);
            json += std::string(first ? "\"" : ", \"") + JsonEscape(desc.Name) + "\"";
            first = false;
            result = effect->FindNextValidTechnique(technique, &technique);
        }
    } else {
        D3DCAPS9 caps{};
        d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps);
        for (UINT index = 0; index < effectDesc.Techniques; ++index) {
            const D3DXHANDLE technique = effect->GetTechnique(index);
            D3DXTECHNIQUE_DESC desc{};
            effect->GetTechniqueDesc(technique, &desc);
            bool valid = true;
            for (UINT pass = 0; pass < desc.Passes; ++pass) {
                D3DXPASS_DESC passDesc{};
                effect->GetPassDesc(effect->GetPass(technique, pass), &passDesc);
                valid = valid && !(passDesc.pVertexShaderFunction != nullptr && *passDesc.pVertexShaderFunction > caps.VertexShaderVersion);
                valid = valid && !(passDesc.pPixelShaderFunction != nullptr && *passDesc.pPixelShaderFunction > caps.PixelShaderVersion);
            }
            if (valid) {
                json += std::string(first ? "\"" : ", \"") + JsonEscape(desc.Name) + "\"";
                first = false;
            }
        }
    }
    // Every technique in declaration order, valid or not, for the report.
    json += "], \"allTechniques\": [";
    for (UINT index = 0; index < effectDesc.Techniques; ++index) {
        const D3DXHANDLE handle = effect->GetTechnique(index);
        D3DXTECHNIQUE_DESC desc{};
        effect->GetTechniqueDesc(handle, &desc);
        json += std::string(index == 0 ? "\"" : ", \"") + JsonEscape(desc.Name) + "\"";
    }
    json += "]}";
    std::printf("%s\n", json.c_str());

    effect->Release();
    compiled->Release();
    compiler->Release();
    device->Release();
    d3d->Release();
    ::DestroyWindow(window);
    return 0;
}
