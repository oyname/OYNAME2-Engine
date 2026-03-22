#pragma once
#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11DepthStencilView;
struct ID3D11ShaderResourceView;
struct ID3D11RasterizerState;

// ---------------------------------------------------------------------------
// GDXShadowMap — DX11 CSM Shadow-Map (Texture2DArray, bis zu 4 Kaskaden).
//
// Jede Kaskade hat einen eigenen DSV (für BeginPass).
// Der SRV zeigt auf das gesamte Array (für den Main-Pass Pixel-Shader).
//
// Slot t16 / Sampler s7 — unverändert gegenüber dem Single-Kaskaden-System.
// ---------------------------------------------------------------------------
class GDXShadowMap
{
public:
    static constexpr uint32_t kMaxCascades = 4u;

    GDXShadowMap()  = default;
    ~GDXShadowMap() = default;

    bool Create(ID3D11Device* device, uint32_t size = 2048u, uint32_t cascadeCount = 4u);
    void Release();

    // Shadow Pass pro Kaskade: DSV binden, Depth clearen, Viewport setzen, RS setzen.
    void BeginPass(ID3D11DeviceContext* ctx, uint32_t cascade);
    // Shadow Pass beenden: SRV lösen (wird im nächsten BeginFrame wiederhergestellt).
    void EndPass(ID3D11DeviceContext* ctx);

    // SRV des Texture2DArray für PS t16 — alle Kaskaden.
    void* GetSRV()        const { return m_shadowSRV; }
    void* GetDSV()        const { return m_shadowDSV[0]; }  // Kaskade 0 als "hat Resources" Check

    uint32_t GetSize()         const { return m_size; }
    uint32_t GetCascadeCount() const { return m_cascadeCount; }
    bool     IsReady()         const { return m_shadowDSV[0] != nullptr; }

private:
    ID3D11Texture2D*          m_shadowTex             = nullptr;
    ID3D11DepthStencilView*   m_shadowDSV[kMaxCascades] = {};
    ID3D11ShaderResourceView* m_shadowSRV             = nullptr;
    ID3D11RasterizerState*    m_shadowRS               = nullptr;

    uint32_t m_size         = 0u;
    uint32_t m_cascadeCount = 0u;
};
