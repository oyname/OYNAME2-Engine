#include "GDXDX11Renderer.h"
#include "Debug.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>

GDXDX11Renderer::GDXDX11Renderer(std::unique_ptr<IGDXDXGIContext> context)
    : m_context(std::move(context))
{
}

bool GDXDX11Renderer::Initialize()
{
    if (!m_context || !m_context->IsValid())
    {
        Debug::LogError("gdxdx11renderer.cpp: context is null or invalid");
        return false;
    }

    const auto info = m_context->QueryDeviceInfo();
    Debug::Log("gdxdx11renderer.cpp: adapter=",      info.adapterName);
    Debug::Log("gdxdx11renderer.cpp: feature level=", info.featureLevelName);
    Debug::Log("gdxdx11renderer.cpp: initialized");
    return true;
}

void GDXDX11Renderer::BeginFrame()
{
    if (!m_context) return;

    auto* rtv = static_cast<ID3D11RenderTargetView*>(m_context->GetRenderTarget());
    auto* dsv = static_cast<ID3D11DepthStencilView*>(m_context->GetDepthStencil());
    auto* ctx = static_cast<ID3D11DeviceContext*>(m_context->GetDeviceContext());
    if (!ctx) return;

    ctx->ClearRenderTargetView(rtv, m_clearColor);
    ctx->ClearDepthStencilView(dsv,
        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void GDXDX11Renderer::EndFrame()
{
    if (m_context) m_context->Present(m_vsync);
}

void GDXDX11Renderer::Resize(int w, int h)
{
    if (m_context) m_context->Resize(w, h);
    Debug::Log("gdxdx11renderer.cpp: resize ", w, "x", h);
}

void GDXDX11Renderer::Shutdown()
{
    Debug::Log("gdxdx11renderer.cpp: shutdown");
    m_context.reset();
}

GDXDXGIDeviceInfo GDXDX11Renderer::GetDeviceInfo() const
{
    if (!m_context) return {};
    return m_context->QueryDeviceInfo();
}

void GDXDX11Renderer::SetClearColor(float r, float g, float b, float a)
{
    m_clearColor[0] = r;
    m_clearColor[1] = g;
    m_clearColor[2] = b;
    m_clearColor[3] = a;
}

void GDXDX11Renderer::SetVSync(bool enabled)
{
    m_vsync = enabled;
}
