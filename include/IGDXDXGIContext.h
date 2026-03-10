#pragma once

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Forward declarations for D3D11 COM interfaces.
//
// Consumers of this header (e.g. GDXDX11Renderer) still include <d3d11.h>
// themselves to actually call methods on these types.  What we avoid here is
// forcing every translation unit that merely holds an IGDXDXGIContext pointer
// to transitively pull in the entire D3D11 / DXGI header chain.
//
// These forward declarations match the structs defined in d3d11.h exactly —
// they are COM interfaces and therefore plain structs at the ABI level.
// ---------------------------------------------------------------------------
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;

// Adapter info populated by GDXWin32DX11ContextFactory::EnumerateAdapters().
// Callers use this to select a GPU before construction.
struct GDXDXGIAdapterInfo
{
    unsigned int index;           // passed to GDXWin32DX11ContextFactory::Create()
    std::wstring name;
    size_t       dedicatedVRAM;   // bytes
    int          featureLevel;    // 110 = FL 11.0, 111 = FL 11.1, 120 = FL 12.0
    std::wstring featureLevelName;
};

// Device info populated after context creation.
struct GDXDXGIDeviceInfo
{
    std::wstring adapterName;
    int          featureLevel;    // same encoding as GDXDXGIAdapterInfo
    std::wstring featureLevelName;
};

// IGDXDXGIContext owns the DX11 device, immediate context, and swap chain
// for one window surface.  GDXDX11Renderer receives a fully constructed
// context — it never touches HWND, DXGI, or COM directly.
class IGDXDXGIContext
{
public:
    virtual ~IGDXDXGIContext() = default;

    virtual bool             IsValid()         const = 0;
    virtual void             Present(bool vsync)     = 0;
    virtual void             Resize(int w, int h)    = 0;
    virtual GDXDXGIDeviceInfo QueryDeviceInfo() const = 0;

    // Expose raw COM pointers for renderer draw calls.
    // Returned pointers are non-owning — lifetime is managed by the context.
    // Callers that need to invoke methods must include <d3d11.h> themselves.
    virtual ID3D11Device*           GetDevice()        const = 0;
    virtual ID3D11DeviceContext*    GetDeviceContext()  const = 0;
    virtual ID3D11RenderTargetView* GetRenderTarget()   const = 0;
    virtual ID3D11DepthStencilView* GetDepthStencil()   const = 0;
};
