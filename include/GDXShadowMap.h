#pragma once
#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11DepthStencilView;
struct ID3D11ShaderResourceView;
struct ID3D11RasterizerState;

// ---------------------------------------------------------------------------
// GDXShadowMap — DX11 Shadow-Map-Ressourcen (adaptiert aus OYNAME Dx11ShadowMap).
//
// Was beibehalten wurde (bewährt in OYNAME):
//   - R32_TYPELESS Textur (DSV als D32_FLOAT, SRV als R32_FLOAT)
//   - PCF Comparison Sampler (LESS, ClampEdge)
//   - DepthBias + SlopeScaledDepthBias (Shadow-Acne-Vermeidung)
//   - Konfigurierbare Größe (Standard: 2048)
//
// Verbesserung:
//   - Größe konfigurierbar über GDXECSRenderer::SetShadowMapSize() vor Init
//   - Kein Coupling mit GDXDevice — nimmt ID3D11Device direkt
//   - Shadow-Sampler ist Teil von GDXSamplerCache (nicht hier)
// ---------------------------------------------------------------------------
class GDXShadowMap
{
public:
    GDXShadowMap()  = default;
    ~GDXShadowMap() = default;

    bool Create(ID3D11Device* device, uint32_t size = 2048u);
    void Release();

    // Shadow Pass: DSV binden, Depth clearen, Viewport setzen, Rasterizer setzen.
    void BeginPass(ID3D11DeviceContext* ctx);
    // Shadow Pass beenden: RTV/DSV werden im Haupt-BeginFrame() wiederhergestellt.
    void EndPass(ID3D11DeviceContext* ctx);

    // SRV für PS-Binding (t16, wie OYNAME)
    void* GetSRV()        const { return m_shadowSRV; }
    void* GetDSV()        const { return m_shadowDSV; }

    uint32_t GetSize()    const { return m_size; }
    bool     IsReady()    const { return m_shadowDSV != nullptr; }

private:
    ID3D11Texture2D*          m_shadowTex  = nullptr;
    ID3D11DepthStencilView*   m_shadowDSV  = nullptr;
    ID3D11ShaderResourceView* m_shadowSRV  = nullptr;
    ID3D11RasterizerState*    m_shadowRS   = nullptr;

    uint32_t m_size = 0u;
};
