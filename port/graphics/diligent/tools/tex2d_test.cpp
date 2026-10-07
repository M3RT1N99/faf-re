// GetTexture2D unit test (M6b gate "GetTexture2D", component R).
//
// For each input file, GetTexture2D's DXT5 blocks are computed three ways and compared byte for byte:
//   reference  DeviceD3D9::GetTexture2D (D3D9Interfaces.cpp:3134-3244, binary 0x008ECD20) exactly:
//              D3DXCreateTextureFromFileInMemoryEx(D3DX_DEFAULT_NONPOW2 x2, 1 level, usage 0,
//              D3DFMT_UNKNOWN, D3DPOOL_SYSTEMMEM, D3DX_FILTER_NONE x2), then for a non-DXT5 image
//              D3DXLoadSurfaceFromSurface into a DXT5 system-memory texture of the size rounded up to
//              whole blocks (D3DX_FILTER_NONE), then the blocks row by row - on a D3D9 HAL device, the
//              device the D3D9 backend renders with;
//   oracle     the same calls on a NULLREF device, which is what the Diligent backend's Windows oracle
//              mode does (D3D9Oracle::GetTexture2D, port/graphics/diligent/D3D9Oracle.cpp);
//   portable   DecodeTexture2DPortable (port/graphics/diligent/Texture2DPortable.cpp), the D3DX-free
//              path for Android, compiled into this tool from the same source main.exe uses.
// It can also check outputs main.exe wrote with /galdumptex (tex2d_<n>_<hash>.bin inputs with their
// .out blocks), so the in-engine oracle path is held to the same reference.
//
// usage: tex2d_test <list.txt> <result.json> [--no-hal]
//   list.txt: one input path per line; a line "path|label" names the file (its VFS path) in the report.
//   A path ending in .bin with a sibling .out file is also checked against that engine output.
// The device windows are hidden 1x1 popups with WS_EX_NOACTIVATE that are never shown, and the HAL
// device uses D3DCREATE_NOWINDOWCHANGES, as tools/fxtechlist.cpp does.
//
// x87 precision. D3DX's DXT encoder is x87 code, so where it encodes (every source that is not DXT5
// in whole blocks) its output depends on the calling thread's precision control. The engine's main
// thread runs under _PC_24 (CScApp::Init), which is also what IDirect3D9::CreateDevice leaves on the
// creating thread without D3DCREATE_FPU_PRESERVE (the D3D9 backend's flags 0x44 do not preserve,
// D3D9Interfaces.cpp:1772). The reference and the oracle run under _PC_24; the report also counts the
// files whose D3DX blocks differ under _PC_53 (the default of any other thread, such as the
// ResourceManager prefetch thread).

#ifndef NOMINMAX
#define NOMINMAX // std::min below; main.vcxproj defines it for the engine too
#endif
#include <d3d9.h>
#include <d3dx9.h>

#include <float.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "port/graphics/diligent/Texture2DPortable.h"

namespace
{
    struct Result
    {
        bool ok = false;
        std::string error;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t sourceFormat = 0; // D3DFORMAT of the loaded image
        std::vector<std::uint8_t> blocks;
    };

    unsigned int AlignToDword(const unsigned int value)
    {
        return (value + 3U) & ~3U;
    }

    // DeviceD3D9::GetTexture2D's body with its D3D9 objects on `device`.
    Result GetTexture2D(IDirect3DDevice9* const device, const std::vector<std::uint8_t>& source)
    {
        Result result;
        D3DXIMAGE_INFO info{};
        IDirect3DTexture9* sourceTexture = nullptr;
        HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(device, source.data(), static_cast<UINT>(source.size()), D3DX_DEFAULT_NONPOW2,
                                                         D3DX_DEFAULT_NONPOW2, 1U, 0U, D3DFMT_UNKNOWN, D3DPOOL_SYSTEMMEM, D3DX_FILTER_NONE,
                                                         D3DX_FILTER_NONE, 0U, &info, nullptr, &sourceTexture);
        if (FAILED(hr)) {
            char text[64];
            std::snprintf(text, sizeof(text), "D3DXCreateTextureFromFileInMemoryEx 0x%08lX", static_cast<unsigned long>(hr));
            result.error = text;
            return result;
        }
        IDirect3DSurface9* surface = nullptr;
        sourceTexture->GetSurfaceLevel(0U, &surface);
        D3DSURFACE_DESC desc{};
        surface->GetDesc(&desc);
        result.width = desc.Width;
        result.height = desc.Height;
        result.sourceFormat = static_cast<std::uint32_t>(desc.Format);
        IDirect3DTexture9* decodeTexture = nullptr;
        if (desc.Format != D3DFMT_DXT5) {
            hr = device->CreateTexture(AlignToDword(desc.Width), AlignToDword(desc.Height), 1U, 0U, D3DFMT_DXT5, D3DPOOL_SYSTEMMEM,
                                       &decodeTexture, nullptr);
            IDirect3DSurface9* decodeSurface = nullptr;
            if (SUCCEEDED(hr)) {
                decodeTexture->GetSurfaceLevel(0U, &decodeSurface);
                hr = D3DXLoadSurfaceFromSurface(decodeSurface, nullptr, nullptr, surface, nullptr, nullptr, D3DX_FILTER_NONE, 0U);
            }
            surface->Release();
            surface = decodeSurface;
            if (FAILED(hr) || surface == nullptr) {
                result.error = "DXT5 conversion failed";
                if (surface != nullptr) {
                    surface->Release();
                }
                if (decodeTexture != nullptr) {
                    decodeTexture->Release();
                }
                sourceTexture->Release();
                return result;
            }
        }
        D3DLOCKED_RECT locked{};
        surface->LockRect(&locked, nullptr, D3DLOCK_READONLY);
        const std::size_t bytesPerRow = static_cast<std::size_t>(AlignToDword(result.width) >> 2U) * 16U;
        const std::size_t rows = static_cast<std::size_t>(AlignToDword(result.height) >> 2U);
        result.blocks.resize(bytesPerRow * rows);
        for (std::size_t row = 0; row < rows; ++row) {
            std::memcpy(result.blocks.data() + row * bytesPerRow, static_cast<const std::uint8_t*>(locked.pBits) + row * static_cast<std::size_t>(locked.Pitch),
                        bytesPerRow);
        }
        surface->UnlockRect();
        surface->Release();
        sourceTexture->Release();
        if (decodeTexture != nullptr) {
            decodeTexture->Release();
        }
        result.ok = true;
        return result;
    }

    LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        return ::DefWindowProcW(window, message, wParam, lParam);
    }

    IDirect3DDevice9* CreateDevice(IDirect3D9* const d3d, const D3DDEVTYPE type, HWND const window)
    {
        D3DPRESENT_PARAMETERS parameters{};
        parameters.BackBufferWidth = 1;
        parameters.BackBufferHeight = 1;
        parameters.BackBufferFormat = D3DFMT_UNKNOWN;
        parameters.BackBufferCount = 1;
        parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        parameters.hDeviceWindow = window;
        parameters.Windowed = TRUE;
        IDirect3DDevice9* device = nullptr;
        // The D3D9 backend's behaviour flags include MULTITHREADED (0x44, D3D9Interfaces.cpp:1772).
        const DWORD flags[] = {
            D3DCREATE_MULTITHREADED | D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES,
            D3DCREATE_MULTITHREADED | D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES,
        };
        for (const DWORD flag : flags) {
            D3DPRESENT_PARAMETERS attempt = parameters;
            if (SUCCEEDED(d3d->CreateDevice(D3DADAPTER_DEFAULT, type, window, flag, &attempt, &device))) {
                return device;
            }
        }
        return nullptr;
    }

    std::string Hex64(const std::vector<std::uint8_t>& data)
    {
        std::uint64_t hash = 0xCBF29CE484222325ULL;
        for (const std::uint8_t byte : data) {
            hash ^= byte;
            hash *= 0x100000001B3ULL;
        }
        char text[24];
        std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
        return text;
    }

    std::string Escape(const std::string& text)
    {
        std::string out;
        for (const char c : text) {
            if (c == '"' || c == '\\') {
                out += '\\';
            }
            out += c;
        }
        return out;
    }

    /** "equal", or where the first byte differs and how many bytes differ. */
    std::string Compare(const Result& reference, const std::vector<std::uint8_t>& blocks, const std::uint32_t width, const std::uint32_t height)
    {
        if (width != reference.width || height != reference.height) {
            return "size " + std::to_string(width) + "x" + std::to_string(height) + " vs " + std::to_string(reference.width) + "x" +
                   std::to_string(reference.height);
        }
        if (blocks.size() != reference.blocks.size()) {
            return "byte count " + std::to_string(blocks.size()) + " vs " + std::to_string(reference.blocks.size());
        }
        std::size_t first = blocks.size();
        std::size_t differing = 0;
        for (std::size_t index = 0; index < blocks.size(); ++index) {
            if (blocks[index] != reference.blocks[index]) {
                differing += 1;
                first = std::min(first, index);
            }
        }
        if (differing == 0) {
            return "equal";
        }
        return "differ: " + std::to_string(differing) + " bytes, first at " + std::to_string(first) + " (block " + std::to_string(first / 16) + ")";
    }

    bool ReadFile(const std::string& path, std::vector<std::uint8_t>* const out)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return false;
        }
        out->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return true;
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: tex2d_test <list.txt> <result.json> [--no-hal]\n");
        return 2;
    }
    const bool useHal = !(argc > 3 && std::strcmp(argv[3], "--no-hal") == 0);
    std::ifstream list(argv[1]);
    std::vector<std::pair<std::string, std::string>> inputs;
    for (std::string line; std::getline(list, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const std::size_t bar = line.find('|');
        inputs.emplace_back(line.substr(0, bar), bar == std::string::npos ? line : line.substr(bar + 1));
    }

    IDirect3D9* const d3d = ::Direct3DCreate9(D3D_SDK_VERSION);
    if (d3d == nullptr) {
        std::fprintf(stderr, "Direct3DCreate9 failed\n");
        return 1;
    }
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = ::GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"FafTex2DTest";
    ::RegisterClassExW(&windowClass);
    HWND const window = ::CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, windowClass.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr,
                                          nullptr, windowClass.hInstance, nullptr);
    IDirect3DDevice9* const hal = useHal ? CreateDevice(d3d, D3DDEVTYPE_HAL, window) : nullptr;
    IDirect3DDevice9* const nullref = CreateDevice(d3d, D3DDEVTYPE_NULLREF, window);
    if ((useHal && hal == nullptr) || nullref == nullptr) {
        std::fprintf(stderr, "cannot create the %s device\n", hal == nullptr && useHal ? "HAL" : "NULLREF");
        return 1;
    }

    std::ostringstream json;
    json << "{\"reference\": \"" << (useHal ? "HAL" : "NULLREF") << "\", \"files\": [\n";
    std::size_t total = 0, oracleEqual = 0, portableEqual = 0, engineChecked = 0, engineEqual = 0, passThrough = 0, failed = 0, x87SensitiveCount = 0;
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const std::string& path = inputs[index].first;
        const std::string& label = inputs[index].second;
        std::vector<std::uint8_t> source;
        if (!ReadFile(path, &source)) {
            std::fprintf(stderr, "cannot read %s\n", path.c_str());
            ++failed;
            continue;
        }
        ++total;
        unsigned int unused = 0;
        ::_controlfp_s(&unused, _PC_24, _MCW_PC);
        const Result reference = GetTexture2D(useHal ? hal : nullref, source);
        const Result oracle = GetTexture2D(nullref, source);
        ::_controlfp_s(&unused, _PC_53, _MCW_PC);
        const Result oracle53 = GetTexture2D(nullref, source);
        ::_controlfp_s(&unused, _PC_24, _MCW_PC);
        const bool x87Sensitive = oracle.ok && oracle53.ok && oracle.blocks != oracle53.blocks;
        x87SensitiveCount += x87Sensitive ? 1 : 0;
        gpg::gal::diligent::Texture2DBlocks portable;
        std::string portableError;
        const bool portableOk = gpg::gal::diligent::DecodeTexture2DPortable(source.data(), static_cast<std::uint32_t>(source.size()), &portable, &portableError);

        std::string oracleVerdict = !reference.ok ? "reference failed: " + reference.error
                                    : !oracle.ok  ? "failed: " + oracle.error
                                                  : Compare(reference, oracle.blocks, oracle.width, oracle.height);
        std::string portableVerdict = !reference.ok ? "reference failed"
                                      : !portableOk ? "failed: " + portableError
                                                    : Compare(reference, portable.data, portable.width, portable.height);
        oracleEqual += oracleVerdict == "equal" ? 1 : 0;
        portableEqual += portableVerdict == "equal" ? 1 : 0;
        passThrough += (portableOk && portable.passThrough) ? 1 : 0;

        // An engine dump (main.exe /galdumptex): <input>.bin with <input>.out = u32 width, u32 height, blocks.
        std::string engineVerdict;
        if (path.size() > 4 && path.compare(path.size() - 4, 4, ".bin") == 0) {
            std::vector<std::uint8_t> engine;
            if (ReadFile(path.substr(0, path.size() - 4) + ".out", &engine) && engine.size() >= 8) {
                std::uint32_t width = 0;
                std::uint32_t height = 0;
                std::memcpy(&width, engine.data(), 4);
                std::memcpy(&height, engine.data() + 4, 4);
                const std::vector<std::uint8_t> blocks(engine.begin() + 8, engine.end());
                engineVerdict = reference.ok ? Compare(reference, blocks, width, height) : "reference failed";
                ++engineChecked;
                engineEqual += engineVerdict == "equal" ? 1 : 0;
            }
        }

        json << (index == 0 ? "" : ",\n") << "  {\"file\": \"" << Escape(label) << "\", \"bytes\": " << source.size()
             << ", \"d3dFormat\": " << reference.sourceFormat << ", \"size\": [" << reference.width << ", " << reference.height
             << "], \"reference\": \"" << (reference.ok ? Hex64(reference.blocks) : Escape(reference.error)) << "\", \"oracle\": \""
             << Escape(oracleVerdict) << "\", \"portable\": \"" << Escape(portableVerdict) << "\", \"portableSource\": \""
             << Escape(portable.sourceFormat) << "\", \"portablePassThrough\": " << (portableOk && portable.passThrough ? "true" : "false")
             << ", \"x87Sensitive\": " << (x87Sensitive ? "true" : "false");
        if (!engineVerdict.empty()) {
            json << ", \"engine\": \"" << Escape(engineVerdict) << "\"";
        }
        json << "}";
    }
    json << "\n], \"total\": " << total << ", \"unreadable\": " << failed << ", \"oracleEqual\": " << oracleEqual << ", \"portableEqual\": " << portableEqual
         << ", \"portablePassThrough\": " << passThrough << ", \"x87Sensitive\": " << x87SensitiveCount << ", \"engineChecked\": " << engineChecked << ", \"engineEqual\": " << engineEqual << "}\n";
    std::ofstream out(argv[2], std::ios::binary);
    out << json.str();
    std::printf("files %zu, oracle equal %zu, portable equal %zu (pass-through %zu), engine outputs equal %zu/%zu, unreadable %zu, "
                "D3DX blocks x87-dependent (_PC_24 vs _PC_53) %zu\n",
                total, oracleEqual, portableEqual, passThrough, engineEqual, engineChecked, failed, x87SensitiveCount);

    if (hal != nullptr) {
        hal->Release();
    }
    nullref->Release();
    d3d->Release();
    ::DestroyWindow(window);
    const bool pass = failed == 0 && oracleEqual == total && portableEqual == total && engineEqual == engineChecked;
    return pass ? 0 : 3;
}
