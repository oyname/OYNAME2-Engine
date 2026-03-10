#pragma once

#include <memory>
#include <vector>
#include "IGDXDXGIContext.h"

class IGDXWin32NativeAccess;

// GDXWin32DX11ContextFactory builds a DX11 device + swap chain for a Win32
// window.  It follows the same pattern as GDXWin32OpenGLContextFactory:
//
//   - Takes IGDXWin32NativeAccess& directly — not IGDXWindow&.
//   - No dynamic_cast anywhere.
//   - EnumerateAdapters() is side-effect-free: a temporary IDXGIFactory is
//     created and destroyed; no D3D device survives the call.
//   - FindBestAdapter() picks the highest feature level automatically.
//     Replace with a UI selection if desired.
class GDXWin32DX11ContextFactory
{
public:
    // Query available hardware adapters without creating a device.
    static std::vector<GDXDXGIAdapterInfo> EnumerateAdapters();

    // Returns the index of the adapter with the highest feature level.
    static unsigned int FindBestAdapter(
        const std::vector<GDXDXGIAdapterInfo>& adapters);

    // Create a DX11 context for the given window and adapter.
    // adapterIndex comes from EnumerateAdapters() / FindBestAdapter().
    // Returns nullptr on failure (logs via Debug::LogError).
    std::unique_ptr<IGDXDXGIContext> Create(
        IGDXWin32NativeAccess& nativeAccess,
        unsigned int           adapterIndex) const;
};
