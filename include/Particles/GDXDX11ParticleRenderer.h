#pragma once
// ============================================================
//  GDXDX11ParticleRenderer.h  --  DX11 GPU-instanced particle renderer
//  One static unit-quad + dynamic per-particle instance buffer.
//  No DX11 types leak outside this header.
// ============================================================
#include "Particles/IGDXParticleRenderer.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;
struct ID3D11BlendState;
struct ID3D11DepthStencilState;
struct ID3D11RasterizerState;
struct ID3D11InputLayout;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;

class GDXDX11ParticleRenderer : public IGDXParticleRenderer
{
public:
    GDXDX11ParticleRenderer() = default;
    ~GDXDX11ParticleRenderer() override;

    // particleTexture = SRV of the particle atlas (not owned)
    // maxParticles    = upper bound for instance buffer allocation
    bool Init(ID3D11Device*             device,
              ID3D11DeviceContext*       context,
              ID3D11ShaderResourceView*  particleTexture,
              int                        maxParticles = 16384);

    void Render  (const GDXParticleSystem& system, const ParticleRenderContext& ctx) override;
    void Shutdown()                                                                   override;

private:
    bool CreateBuffers(ID3D11Device* dev, int maxParticles);
    bool CreateShaders(ID3D11Device* dev);
    bool CreateStates (ID3D11Device* dev);

    bool UploadAndDrawInstanced(int blendMode,
                                const std::vector<ParticleInstance>& instances);

    ID3D11DeviceContext*     m_ctx         = nullptr;  // not owned

    // Static unit-quad (shared by all instances, never changes)
    ID3D11Buffer*            m_quadVB      = nullptr;
    ID3D11Buffer*            m_quadIB      = nullptr;

    // Dynamic per-particle instance buffers (one per blend mode)
    ID3D11Buffer*            m_instanceBuf[2] = {};

    // Cbuffer: viewProj + camRight + camUp
    ID3D11Buffer*            m_cbuffer     = nullptr;

    ID3D11BlendState*        m_blendAlpha  = nullptr;
    ID3D11BlendState*        m_blendAdd    = nullptr;
    ID3D11DepthStencilState* m_depthState  = nullptr;
    ID3D11RasterizerState*   m_rsCullNone  = nullptr;
    ID3D11InputLayout*       m_layout      = nullptr;
    ID3D11VertexShader*      m_vs          = nullptr;
    ID3D11PixelShader*       m_ps          = nullptr;
    ID3D11SamplerState*      m_sampler     = nullptr;

    ID3D11ShaderResourceView* m_texture    = nullptr;  // not owned

    int m_maxInstances = 0;
};
