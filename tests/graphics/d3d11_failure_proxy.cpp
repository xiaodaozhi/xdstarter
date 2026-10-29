// Test-only app-local proxy. Never deploy this DLL with XDStarter.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>

#include <cstdio>
#include <cstring>
#include <cwchar>

using CreateDevice = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
                                      const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**,
                                      D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

extern "C" HRESULT WINAPI ProbeD3D11CreateDevice(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driverType,
                                                 HMODULE software, UINT flags,
                                                 const D3D_FEATURE_LEVEL* levels, UINT levelCount,
                                                 UINT sdk, ID3D11Device** device,
                                                 D3D_FEATURE_LEVEL* featureLevel,
                                                 ID3D11DeviceContext** context) {
    char mode[32] = {};
    GetEnvironmentVariableA("XDSTARTER_D3D11_FAULT", mode, sizeof(mode));
    IDXGIAdapter1* adapter1 = nullptr;
    DXGI_ADAPTER_DESC1 desc = {};
    const bool identified = adapter &&
                            SUCCEEDED(adapter->QueryInterface(
                                __uuidof(IDXGIAdapter1), reinterpret_cast<void**>(&adapter1))) &&
                            SUCCEEDED(adapter1->GetDesc1(&desc));
    if (adapter1)
        adapter1->Release();
    if (!identified) {
        std::fprintf(stderr, "[d3d11-test-proxy] adapter-unidentified\n");
        return E_INVALIDARG;
    }

    const bool softwareAdapter = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    const bool delayed = std::strcmp(mode, "delayed-all") == 0;
    const bool fail = delayed || std::strcmp(mode, "all") == 0 ||
                      (std::strcmp(mode, "hardware") == 0 && !softwareAdapter);
    std::fprintf(stderr,
                 "[d3d11-test-proxy] mode=%s vendor=0x%X device=0x%X flags=0x%X action=%s\n", mode,
                 desc.VendorId, desc.DeviceId, desc.Flags, fail ? "injected-failure" : "forward");
    std::fflush(stderr);
    if (fail) {
        if (delayed)
            Sleep(600);
        if (device)
            *device = nullptr;
        if (context)
            *context = nullptr;
        return DXGI_ERROR_UNSUPPORTED;
    }

    // Load the original library by absolute System32 path, not by the proxy's
    // basename. Only the isolated test process ever sees this replacement.
    wchar_t path[MAX_PATH] = {};
    const UINT size = GetSystemDirectoryW(path, MAX_PATH);
    if (!size || size >= MAX_PATH || wcscat_s(path, L"\\d3d11.dll"))
        return E_FAIL;
    HMODULE real = LoadLibraryExW(path, nullptr,
                                  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ProbeD3D11CreateDevice), &self);
    if (!real || real == self)
        return E_FAIL;
    wchar_t loadedPath[MAX_PATH] = {};
    if (!GetModuleFileNameW(real, loadedPath, MAX_PATH))
        return E_FAIL;
    std::fprintf(stderr, "[d3d11-test-proxy] real-module=%ls\n", loadedPath);
    const auto create = reinterpret_cast<CreateDevice>(GetProcAddress(real, "D3D11CreateDevice"));
    if (!create)
        return E_FAIL;
    const HRESULT result = create(adapter, driverType, software, flags, levels, levelCount, sdk,
                                  device, featureLevel, context);
    std::fprintf(stderr, "[d3d11-test-proxy] system-result=0x%08lX\n",
                 static_cast<unsigned long>(result));
    std::fflush(stderr);
    return result;
}
