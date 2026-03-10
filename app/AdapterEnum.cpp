// AdapterEnum.cpp
//
// Standalone example: enumerate all hardware DX11 adapters and print their
// properties to the console.  No window, no renderer, no engine loop.
//
// Build (Visual Studio, x64):
//   Add to a Win32 console project together with:
//     src/platform/windows/GDXWin32DX11ContextFactory.cpp
//     src/util/Debug.h   (header-only)
//   Linker: d3d11.lib, dxgi.lib
//
// Expected output (example):
//
//   ========================================
//    GDX Adapter Enumeration
//   ========================================
//
//   Adapter 0
//     Name         : NVIDIA GeForce RTX 3080
//     Dedicated GPU: 10240 MB
//     Shared System: 8192 MB
//     Feature Level: 12.1
//     Best Adapter : YES
//
//   Adapter 1
//     Name         : Intel(R) UHD Graphics 770
//     Dedicated GPU: 128 MB
//     Shared System: 8192 MB
//     Feature Level: 12.1
//     Best Adapter : NO
//
//   ========================================
//   Best adapter: 0  [NVIDIA GeForce RTX 3080]
//   Feature Level: 12.1
//   ========================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi.h>

#include "GDXWin32DX11ContextFactory.h"
#include "Debug.h"

#include <iostream>
#include <iomanip>
#include <string>

// Utility: wstring -> narrow for std::cout
static std::string Narrow(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(
        CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

static std::string FormatMB(size_t bytes)
{
    std::ostringstream oss;
    oss << (bytes / (1024 * 1024)) << " MB";
    return oss.str();
}

// Also query the output (monitor) list for a given adapter.
// Uses DXGI directly since GDXWin32DX11ContextFactory doesn't expose outputs.
static void PrintOutputs(unsigned int adapterIndex)
{
    IDXGIFactory* factory = nullptr;
    if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory),
                                  reinterpret_cast<void**>(&factory))))
        return;

    IDXGIAdapter* adapter = nullptr;
    if (FAILED(factory->EnumAdapters(adapterIndex, &adapter)))
    {
        factory->Release();
        return;
    }

    IDXGIOutput* output = nullptr;
    for (UINT i = 0; adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_OUTPUT_DESC odesc = {};
        output->GetDesc(&odesc);

        const RECT& r = odesc.DesktopCoordinates;
        const int   w = r.right  - r.left;
        const int   h = r.bottom - r.top;

        std::cout
            << "    Output " << i << "  ["
            << Narrow(odesc.DeviceName) << "]"
            << "  " << w << "x" << h
            << (odesc.AttachedToDesktop ? "  (attached)" : "  (detached)")
            << "\n";

        output->Release();
    }

    adapter->Release();
    factory->Release();
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi.h>

int main()
{
    std::cout
        << "\n"
        << "========================================\n"
        << " GDX Adapter Enumeration\n"
        << "========================================\n\n";

    const auto adapters = GDXWin32DX11ContextFactory::EnumerateAdapters();

    if (adapters.empty())
    {
        std::cout << "  No suitable hardware adapters found.\n"
                  << "  (Software / WARP adapters are excluded by design.)\n\n";
        Debug::LogError("adapterenumeration.cpp: no hardware adapters found");
        return 1;
    }

    const unsigned int bestIdx = GDXWin32DX11ContextFactory::FindBestAdapter(adapters);

    for (const auto& a : adapters)
    {
        const bool isBest = (a.index == bestIdx);

        std::cout
            << "Adapter " << a.index << "\n"
            << "  Name         : " << Narrow(a.name) << "\n"
            << "  Dedicated GPU: " << FormatMB(a.dedicatedVRAM) << "\n"
            << "  Feature Level: " << Narrow(a.featureLevelName) << "\n"
            << "  Best Adapter : " << (isBest ? "YES  <---" : "NO") << "\n"
            << "  Outputs      :\n";

        PrintOutputs(a.index);
        std::cout << "\n";
    }

    const auto& best = adapters[bestIdx];
    std::cout
        << "========================================\n"
        << "Best adapter : " << best.index
        << "  [" << Narrow(best.name) << "]\n"
        << "Feature Level: " << Narrow(best.featureLevelName) << "\n"
        << "========================================\n\n";

    Debug::Log("adapterenumeration.cpp: enumeration complete  best=",
               best.index, "  [", best.name, "]  FL ", best.featureLevelName);

    return 0;
}
