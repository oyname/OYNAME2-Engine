#pragma once

//
// GDXDX11LightSystem ist direkt in GDXDX11RenderBackend eingebettet (m_lightSystem). 
// Das IGDXLightSystem-Interface existiert, wird aber von keinem anderen Backend implementiert.
// Es ist also ein Interface ohne zweite Implementierung 
//

#include "IGDXLightSystem.h"
#include "Components.h"

// Vorwärtsdeklarationen — kein <d3d11.h> im Header nötig.
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;

// ---------------------------------------------------------------------------
// GDXDX11LightSystem — DirectX 11 Implementierung von IGDXLightSystem.
//
// Sammelt LightComponent-Entities → befüllt FrameData.
//
//   - Liest LightComponent + WorldTransformComponent aus der Registry
//   - Schreibt direkt in FrameData.lights (kein Zwischen-Array)
//   - Shadow-Licht (erstes Directional mit castShadows=true) wird
//     automatisch erkannt und shadowViewProjMatrix berechnet
//
// cbuffer-Slot: b3 (VS+PS)
// Kein Konflikt mit b0 (Entity), b1 (Frame), b2 (Material).
// ---------------------------------------------------------------------------
class GDXDX11LightSystem final : public IGDXLightSystem
{
public:
    GDXDX11LightSystem()  = default;
    ~GDXDX11LightSystem() override { Shutdown(); }

    // IGDXLightSystem — void* wird intern auf ID3D11Device* / ID3D11DeviceContext* gecastet.
    bool Init(void* device) override;
    void Shutdown() override;
    void Update(Registry& registry, FrameData& frame, void* ctx) override;
    bool IsReady() const override { return m_lightBuffer != nullptr; }

private:
    void UploadBuffer(ID3D11DeviceContext* ctx, const FrameData& frame);

    ID3D11Buffer* m_lightBuffer = nullptr;
};

// ---------------------------------------------------------------------------
// GPU-Layout des LightBuffers (b3).
// Muss exakt mit LightBuffer im PixelShader übereinstimmen.
// Liegt hier (nicht im Interface), weil es ein DX11-Impl.-Detail ist.
// ---------------------------------------------------------------------------
struct alignas(16) GDXDX11LightCBufferEntry
{
    float position[4];   // xyz=pos,  w: 0=directional, 1=point, 2=spot
    float direction[4];  // xyz=dir (world space, normalized), w=unused
    float diffuse[4];    // rgb=color*intensity, a=radius
    float innerCosAngle;
    float outerCosAngle;
    float _pad0;
    float _pad1;
    // Gesamt: 3*16 + 4*4 = 64 Byte
};
static_assert(sizeof(GDXDX11LightCBufferEntry) == 64, "LightEntry muss 64 Byte sein");

struct alignas(16) GDXDX11LightCBuffer
{
    GDXDX11LightCBufferEntry lights[MAX_LIGHTS];
    float                    sceneAmbient[3];
    uint32_t                 lightCount;
};
static_assert(sizeof(GDXDX11LightCBuffer) == MAX_LIGHTS * 64 + 16, "LightCBuffer Layout falsch");
