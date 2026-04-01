#include "GDXWin32DX11ContextFactory.h"
#include "IGDXWin32NativeAccess.h"
#include "GDXWin32NativeHandles.h"
#include "IGDXDXGIContext.h"
#include "Core/Debug.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcommon.h>
#include <dxgidebug.h>
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")


#ifdef _DEBUG
namespace
{
    // Local copy of the well-known D3D debug object name GUID.
    // Kept local to avoid linker issues with WKPDID_D3DDebugObjectName in some setups.
    static const GUID kGDXD3DDebugObjectName =
    { 0x429b8c22, 0x9188, 0x4b0c, { 0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00 } };

    inline void SetDebugName(IUnknown* obj, const char* name)
    {
        if (!obj || !name || !*name) return;

        ID3D11DeviceChild* child = nullptr;
        if (SUCCEEDED(obj->QueryInterface(__uuidof(ID3D11DeviceChild), reinterpret_cast<void**>(&child))) && child)
        {
            child->SetPrivateData(kGDXD3DDebugObjectName,
                static_cast<UINT>(std::strlen(name)), name);
            child->Release();
        }
    }

    inline void ReportLiveObjects(ID3D11Device* device)
    {
        if (!device) return;
        ID3D11Debug* debug = nullptr;
        if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D11Debug), reinterpret_cast<void**>(&debug))) && debug)
        {
            debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
            debug->Release();
        }
    }
}
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{
    template<typename T>
    void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

    int FeatureLevelToInt(D3D_FEATURE_LEVEL fl)
    {
        switch (fl)
        {
        case D3D_FEATURE_LEVEL_12_1: return 121;
        case D3D_FEATURE_LEVEL_12_0: return 120;
        case D3D_FEATURE_LEVEL_11_1: return 111;
        case D3D_FEATURE_LEVEL_11_0: return 110;
        case D3D_FEATURE_LEVEL_10_1: return 101;
        case D3D_FEATURE_LEVEL_10_0: return 100;
        default:                     return   0;
        }
    }

    std::wstring FeatureLevelName(D3D_FEATURE_LEVEL fl)
    {
        switch (fl)
        {
        case D3D_FEATURE_LEVEL_12_1: return L"12.1";
        case D3D_FEATURE_LEVEL_12_0: return L"12.0";
        case D3D_FEATURE_LEVEL_11_1: return L"11.1";
        case D3D_FEATURE_LEVEL_11_0: return L"11.0";
        case D3D_FEATURE_LEVEL_10_1: return L"10.1";
        case D3D_FEATURE_LEVEL_10_0: return L"10.0";
        default:                     return L"unknown";
        }
    }

    // Query the display refresh rate for a given adapter / resolution.
    // Returns { numerator, denominator } — { 0, 1 } as fallback.
    std::pair<UINT,UINT> QueryRefreshRate(
        IDXGIAdapter* adapter, UINT width, UINT height)
    {
        IDXGIOutput* output = nullptr;
        if (FAILED(adapter->EnumOutputs(0, &output)) || !output)
            return { 0, 1 };

        UINT modeCount = 0;
        output->GetDisplayModeList(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_ENUM_MODES_INTERLACED, &modeCount, nullptr);

        if (modeCount == 0) { output->Release(); return { 0, 1 }; }

        std::vector<DXGI_MODE_DESC> modes(modeCount);
        output->GetDisplayModeList(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_ENUM_MODES_INTERLACED, &modeCount, modes.data());
        output->Release();

        UINT bestNum = 0, bestDen = 1;
        for (const auto& m : modes)
        {
            if (m.Width == width && m.Height == height)
            {
                const double hz = static_cast<double>(m.RefreshRate.Numerator) /
                                  static_cast<double>(m.RefreshRate.Denominator);
                const double best = static_cast<double>(bestNum) /
                                    static_cast<double>(bestDen);
                if (hz > best) { bestNum = m.RefreshRate.Numerator;
                                 bestDen = m.RefreshRate.Denominator; }
            }
        }
        return { bestNum, bestDen };
    }

    // ---------------------------------------------------------------------------
    // Win32DXGIContext — private, not exposed in header
    // ---------------------------------------------------------------------------
    class Win32DXGIContext final : public IGDXDXGIContext
    {
    public:
        ~Win32DXGIContext() override { Destroy(); }

        bool Create(HWND hwnd, IDXGIAdapter* adapter,
                    int width, int height,
                    const GDXDXGIAdapterInfo& adapterInfo,
                    bool borderless,
                    bool fullscreen);

        // IGDXDXGIContext
        bool              IsValid()          const override { return m_device != nullptr; }
        void              Present(bool vsync) override;
        void              Resize(int w, int h) override;
        GDXDXGIDeviceInfo QueryDeviceInfo()   const override;
        bool              SetFullscreen(bool fullscreen) override;
        bool              IsFullscreen()      const override { return m_fullscreen; }

        ID3D11Device*           GetDevice()        const override { return m_device; }
        ID3D11DeviceContext*    GetDeviceContext() const override { return m_context; }
        ID3D11RenderTargetView* GetRenderTarget()  const override { return m_rtv; }
        ID3D11DepthStencilView* GetDepthStencil()  const override { return m_dsv; }

    private:
        bool CreateRenderTargetView();
        bool CreateDepthBuffer(int w, int h);
        void ReleaseSwapChainResources();
        void Destroy();

        IDXGISwapChain*         m_swapChain  = nullptr;
        ID3D11Device*           m_device     = nullptr;
        ID3D11DeviceContext*    m_context    = nullptr;
        ID3D11RenderTargetView* m_rtv        = nullptr;
        ID3D11DepthStencilView* m_dsv        = nullptr;
        ID3D11Texture2D*        m_depthTex   = nullptr;
        ID3D11RasterizerState*  m_rasterizer = nullptr;
        ID3D11DepthStencilState* m_dsState   = nullptr;

        GDXDXGIAdapterInfo m_adapterInfo;
        D3D_FEATURE_LEVEL  m_featureLevel = D3D_FEATURE_LEVEL_11_0;
        int                m_width        = 0;
        int                m_height       = 0;
        HWND               m_hwnd         = nullptr;
        bool               m_borderless   = true;
        bool               m_fullscreen   = false;
    };

    bool Win32DXGIContext::Create(HWND hwnd, IDXGIAdapter* adapter,
                                   int width, int height,
                                   const GDXDXGIAdapterInfo& adapterInfo,
                                   bool borderless,
                                   bool fullscreen)
    {
        m_hwnd        = hwnd;
        m_width       = width;
        m_height      = height;
        m_adapterInfo = adapterInfo;
        m_borderless  = borderless;
        m_fullscreen  = fullscreen;

        // --- Device + context -----------------------------------------------
        const D3D_FEATURE_LEVEL levels[] =
        {
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };

        UINT flags = 0;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        HRESULT hr = D3D11CreateDevice(
            adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            flags, levels, ARRAYSIZE(levels),
            D3D11_SDK_VERSION,
            &m_device, &m_featureLevel, &m_context);

        if (FAILED(hr))
        {
            Debug::LogError("gdxwin32dx11contextfactory.cpp: D3D11CreateDevice failed 0x",
                            static_cast<unsigned long>(hr));
            return false;
        }
#ifdef _DEBUG
        SetDebugName(m_device, "GDX.D3D11.Device");
        SetDebugName(m_context, "GDX.D3D11.ImmediateContext");
#endif

        // --- Swap chain -----------------------------------------------------
        auto [rn, rd] = QueryRefreshRate(adapter,
            static_cast<UINT>(width), static_cast<UINT>(height));

        IDXGIFactory* factory = nullptr;
        adapter->GetParent(__uuidof(IDXGIFactory),
                           reinterpret_cast<void**>(&factory));

        DXGI_SWAP_CHAIN_DESC sc = {};
        sc.BufferDesc.Width                   = static_cast<UINT>(width);
        sc.BufferDesc.Height                  = static_cast<UINT>(height);
        sc.BufferDesc.RefreshRate.Numerator   = rn;
        sc.BufferDesc.RefreshRate.Denominator = rd ? rd : 1;
        sc.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        sc.BufferDesc.ScanlineOrdering        = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        sc.BufferDesc.Scaling                 = DXGI_MODE_SCALING_UNSPECIFIED;
        sc.SampleDesc.Count                   = 1;
        sc.SampleDesc.Quality                 = 0;
        sc.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc.BufferCount                        = 1;
        sc.OutputWindow                       = hwnd;
        sc.Windowed                           = fullscreen ? FALSE : TRUE;
        sc.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;
        sc.Flags                              = 0;

        hr = factory->CreateSwapChain(m_device, &sc, &m_swapChain);
        factory->Release();

        if (FAILED(hr))
        {
            Debug::LogError("gdxwin32dx11contextfactory.cpp: CreateSwapChain failed 0x",
                            static_cast<unsigned long>(hr));
            return false;
        }
#ifdef _DEBUG
        SetDebugName(m_swapChain, "GDX.D3D11.SwapChain");
#endif

        // DXGI::CreateSwapChain entfernt intern WS_CAPTION / WS_THICKFRAME.
        // Bei borderless == false den Rahmen explizit wiederherstellen.
        if (!m_borderless)
        {
            LONG style = GetWindowLong(hwnd, GWL_STYLE);
            style |= WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME;
            SetWindowLong(hwnd, GWL_STYLE, style);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }

        // --- RTV + depth + rasterizer + DS state ----------------------------
        if (!CreateRenderTargetView())  return false;
        if (!CreateDepthBuffer(width, height)) return false;

        // Rasterizer state (solid, back-face cull, CW winding)
        D3D11_RASTERIZER_DESC rd2 = {};
        rd2.FillMode              = D3D11_FILL_SOLID;
        rd2.CullMode              = D3D11_CULL_BACK;
        rd2.FrontCounterClockwise = FALSE;
        rd2.DepthClipEnable       = TRUE;
        m_device->CreateRasterizerState(&rd2, &m_rasterizer);

        // Depth-stencil state (depth test enabled, write enabled)
        D3D11_DEPTH_STENCIL_DESC dsd = {};
        dsd.DepthEnable    = TRUE;
        dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        dsd.DepthFunc      = D3D11_COMPARISON_LESS_EQUAL;
        dsd.StencilEnable  = FALSE;
        m_device->CreateDepthStencilState(&dsd, &m_dsState);
#ifdef _DEBUG
        SetDebugName(m_dsState, "GDX.D3D11.DefaultDepthStencilState");
#endif

        // Bind output-merger and rasterizer state
        m_context->OMSetRenderTargets(1, &m_rtv, m_dsv);
        m_context->OMSetDepthStencilState(m_dsState, 1);
        m_context->RSSetState(m_rasterizer);

        D3D11_VIEWPORT vp = {};
        vp.Width    = static_cast<float>(width);
        vp.Height   = static_cast<float>(height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);

        return true;
    }

    bool Win32DXGIContext::CreateRenderTargetView()
    {
        ID3D11Texture2D* backBuffer = nullptr;
        HRESULT hr = m_swapChain->GetBuffer(
            0, __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&backBuffer));
        if (FAILED(hr)) { Debug::LogError("gdxwin32dx11contextfactory.cpp: GetBuffer failed"); return false; }

        hr = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_rtv);
#ifdef _DEBUG
        SetDebugName(backBuffer, "GDX.D3D11.BackBuffer");
        SetDebugName(m_rtv, "GDX.D3D11.BackBufferRTV");
#endif
        backBuffer->Release();
        if (FAILED(hr)) { Debug::LogError("gdxwin32dx11contextfactory.cpp: CreateRenderTargetView failed"); return false; }
        return true;
    }

    bool Win32DXGIContext::CreateDepthBuffer(int w, int h)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width              = static_cast<UINT>(w);
        td.Height             = static_cast<UINT>(h);
        td.MipLevels          = 1;
        td.ArraySize          = 1;
        td.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
        td.SampleDesc.Count   = 1;
        td.SampleDesc.Quality = 0;
        td.Usage              = D3D11_USAGE_DEFAULT;
        td.BindFlags          = D3D11_BIND_DEPTH_STENCIL;

        HRESULT hr = m_device->CreateTexture2D(&td, nullptr, &m_depthTex);
        if (FAILED(hr)) { Debug::LogError("gdxwin32dx11contextfactory.cpp: depth texture failed"); return false; }

        hr = m_device->CreateDepthStencilView(m_depthTex, nullptr, &m_dsv);
#ifdef _DEBUG
        SetDebugName(m_depthTex, "GDX.D3D11.DepthTexture");
        SetDebugName(m_dsv, "GDX.D3D11.DepthDSV");
#endif
        if (FAILED(hr)) { Debug::LogError("gdxwin32dx11contextfactory.cpp: CreateDepthStencilView failed"); return false; }
        return true;
    }

    void Win32DXGIContext::ReleaseSwapChainResources()
    {
        if (m_context) m_context->OMSetRenderTargets(0, nullptr, nullptr);
        SafeRelease(m_dsv);
        SafeRelease(m_depthTex);
        SafeRelease(m_rtv);
    }

    void Win32DXGIContext::Resize(int w, int h)
    {
        if (w == 0 || h == 0 || (w == m_width && h == m_height)) return;
        m_width  = w;
        m_height = h;

        ReleaseSwapChainResources();
        m_swapChain->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h),
                                    DXGI_FORMAT_UNKNOWN, 0);
        CreateRenderTargetView();
        CreateDepthBuffer(w, h);

        m_context->OMSetRenderTargets(1, &m_rtv, m_dsv);
        m_context->OMSetDepthStencilState(m_dsState, 1);
        m_context->RSSetState(m_rasterizer);

        D3D11_VIEWPORT vp = {};
        vp.Width    = static_cast<float>(w);
        vp.Height   = static_cast<float>(h);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);

        Debug::Log("gdxwin32dx11contextfactory.cpp: resize ", w, "x", h);
    }

    void Win32DXGIContext::Present(bool vsync)
    {
        m_swapChain->Present(vsync ? 1 : 0, 0);
    }

    bool Win32DXGIContext::SetFullscreen(bool fullscreen)
    {
        if (!m_swapChain) return false;
        if (m_fullscreen == fullscreen) return true;

        // Vor dem Wechsel SwapChain-Ressourcen freigeben
        ReleaseSwapChainResources();

        const HRESULT hr = m_swapChain->SetFullscreenState(fullscreen ? TRUE : FALSE, nullptr);
        if (FAILED(hr))
        {
            Debug::LogError("SetFullscreen: SetFullscreenState fehlgeschlagen hr=", (int)hr);
            // SwapChain-Ressourcen wiederherstellen
            CreateRenderTargetView();
            CreateDepthBuffer(m_width, m_height);
            return false;
        }

        m_fullscreen = fullscreen;

        // Resize nach Wechsel um BackBuffer neu zu erstellen
        DXGI_SWAP_CHAIN_DESC desc{};
        m_swapChain->GetDesc(&desc);
        m_swapChain->ResizeBuffers(0,
            desc.BufferDesc.Width,
            desc.BufferDesc.Height,
            DXGI_FORMAT_UNKNOWN, 0);

        m_width  = static_cast<int>(desc.BufferDesc.Width);
        m_height = static_cast<int>(desc.BufferDesc.Height);

        CreateRenderTargetView();
        CreateDepthBuffer(m_width, m_height);

        Debug::Log("SetFullscreen: ", fullscreen ? "ON" : "OFF",
                   " ", m_width, "x", m_height);
        return true;
    }

    GDXDXGIDeviceInfo Win32DXGIContext::QueryDeviceInfo() const
    {
        GDXDXGIDeviceInfo info;
        info.adapterName     = m_adapterInfo.name;
        info.featureLevel    = FeatureLevelToInt(m_featureLevel);
        info.featureLevelName = FeatureLevelName(m_featureLevel);
        return info;
    }

    void Win32DXGIContext::Destroy()
    {
        if (m_context)
        {
            ID3D11ShaderResourceView* nullSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
            ID3D11Buffer* nullCBs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
            ID3D11SamplerState* nullSamplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
            ID3D11UnorderedAccessView* nullUAVs[D3D11_PS_CS_UAV_REGISTER_COUNT] = {};
            UINT initialCounts[D3D11_PS_CS_UAV_REGISTER_COUNT] = {};

            m_context->VSSetShader(nullptr, nullptr, 0);
            m_context->PSSetShader(nullptr, nullptr, 0);
            m_context->GSSetShader(nullptr, nullptr, 0);
            m_context->HSSetShader(nullptr, nullptr, 0);
            m_context->DSSetShader(nullptr, nullptr, 0);
            m_context->CSSetShader(nullptr, nullptr, 0);

            m_context->VSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, nullCBs);
            m_context->PSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, nullCBs);
            m_context->GSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, nullCBs);
            m_context->HSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, nullCBs);
            m_context->DSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, nullCBs);
            m_context->CSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, nullCBs);

            m_context->VSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRVs);
            m_context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRVs);
            m_context->GSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRVs);
            m_context->HSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRVs);
            m_context->DSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRVs);
            m_context->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRVs);

            m_context->VSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, nullSamplers);
            m_context->PSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, nullSamplers);
            m_context->GSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, nullSamplers);
            m_context->HSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, nullSamplers);
            m_context->DSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, nullSamplers);
            m_context->CSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, nullSamplers);

            m_context->CSSetUnorderedAccessViews(0, D3D11_PS_CS_UAV_REGISTER_COUNT, nullUAVs, initialCounts);
            m_context->OMSetRenderTargets(0, nullptr, nullptr);
            m_context->ClearState();
            m_context->Flush();
        }

        if (m_swapChain) m_swapChain->SetFullscreenState(FALSE, nullptr);

        ReleaseSwapChainResources();

#ifdef _DEBUG
        ReportLiveObjects(m_device);
#endif

        SafeRelease(m_dsState);
        SafeRelease(m_rasterizer);
        SafeRelease(m_swapChain);
        SafeRelease(m_context);
        SafeRelease(m_device);
    }
}

// ---------------------------------------------------------------------------
// GDXWin32DX11ContextFactory
// ---------------------------------------------------------------------------

std::vector<GDXDXGIAdapterInfo>
GDXWin32DX11ContextFactory::EnumerateAdapters()
{
    std::vector<GDXDXGIAdapterInfo> result;

    IDXGIFactory* factory = nullptr;
    if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory),
                                  reinterpret_cast<void**>(&factory))))
    {
        Debug::LogError("gdxwin32dx11contextfactory.cpp: CreateDXGIFactory failed");
        return result;
    }

    const D3D_FEATURE_LEVEL levels[] =
    {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    IDXGIAdapter* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC desc = {};
        adapter->GetDesc(&desc);

        // Skip software / WARP adapters.
        if (desc.DedicatedVideoMemory == 0 ||
            std::wstring(desc.Description).find(L"Microsoft Basic") != std::wstring::npos)
        {
            adapter->Release();
            continue;
        }

        // Probe feature level — no surviving device.
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_10_0;
        HRESULT hr = D3D11CreateDevice(
            adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            0, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
            nullptr, &fl, nullptr);

        if (SUCCEEDED(hr))
        {
            GDXDXGIAdapterInfo info;
            info.index            = i;
            info.name             = desc.Description;
            info.dedicatedVRAM    = desc.DedicatedVideoMemory;
            info.featureLevel     = FeatureLevelToInt(fl);
            info.featureLevelName = FeatureLevelName(fl);
            result.push_back(info);
            Debug::Log("gdxwin32dx11contextfactory.cpp: adapter ", i,
                       " [", info.name, "]  FL ", info.featureLevelName);
        }
        adapter->Release();
    }
    factory->Release();
    return result;
}

unsigned int GDXWin32DX11ContextFactory::FindBestAdapter(
    const std::vector<GDXDXGIAdapterInfo>& adapters)
{
    unsigned int best = 0;
    int          bestFL = -1;
    for (const auto& a : adapters)
    {
        if (a.featureLevel > bestFL)
        {
            bestFL = a.featureLevel;
            best   = a.index;
        }
    }
    return best;
}

std::unique_ptr<IGDXDXGIContext>
GDXWin32DX11ContextFactory::Create(
    IGDXWin32NativeAccess& nativeAccess,
    unsigned int adapterIndex) const
{
    Debug::Log("gdxwin32dx11contextfactory.cpp: Create START  adapter=", adapterIndex);

    GDXWin32NativeHandles handles{};
    if (!nativeAccess.QueryNativeHandles(handles))
    {
        Debug::LogError("gdxwin32dx11contextfactory.cpp: QueryNativeHandles failed — window not yet created?");
        return nullptr;
    }

    const bool borderless  = nativeAccess.IsBorderless();
    const bool fullscreen  = nativeAccess.IsFullscreen();

    HWND hwnd = reinterpret_cast<HWND>(handles.hwnd);

    IDXGIFactory* factory = nullptr;
    if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory))))
    {
        Debug::LogError("gdxwin32dx11contextfactory.cpp: CreateDXGIFactory failed");
        return nullptr;
    }

    IDXGIAdapter* adapter = nullptr;
    HRESULT hr = factory->EnumAdapters(adapterIndex, &adapter);
    factory->Release();

    if (FAILED(hr) || !adapter)
    {
        Debug::LogError("gdxwin32dx11contextfactory.cpp: adapter ", adapterIndex, " not found");
        return nullptr;
    }

    DXGI_ADAPTER_DESC adesc = {};
    adapter->GetDesc(&adesc);

    GDXDXGIAdapterInfo adapterInfo;
    adapterInfo.index = adapterIndex;
    adapterInfo.name = adesc.Description;
    adapterInfo.dedicatedVRAM = adesc.DedicatedVideoMemory;

    RECT rc = {};
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;

    auto ctx = std::make_unique<Win32DXGIContext>();
    if (!ctx->Create(hwnd, adapter, w, h, adapterInfo, borderless, fullscreen))
    {
        adapter->Release();
        return nullptr;
    }

    adapter->Release();

    const auto info = ctx->QueryDeviceInfo();
    Debug::Log("gdxwin32dx11contextfactory.cpp: Create END  ", info.adapterName, "  FL ", info.featureLevelName);
    return ctx;
}
