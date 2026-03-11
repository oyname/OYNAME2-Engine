#pragma once
#include "Registry.h"
#include "Components.h"
#include "FrameData.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;

// ---------------------------------------------------------------------------
// GDXLightSystem — sammelt LightComponent-Entities → befüllt FrameData.
//
// Verbesserung gegenüber OYNAME:
//   In OYNAME war der LightManager eng mit GDXEngine gekoppelt und
//   verwendete raw Light*-Zeiger. Hier:
//   - LightSystem ist ein eigenständiges ECS-System
//   - Liest LightComponent + WorldTransformComponent aus der Registry
//   - Schreibt direkt in FrameData.lights (kein Zwischen-Array)
//   - Shadow-Licht (erstes directional mit castShadows=true) wird
//     automatisch erkannt und shadowViewProjMatrix berechnet
//
// cbuffer-Slot: b3 (VS+PS)
// Kein Konflikt mit b0 (Entity), b1 (Frame), b2 (Material).
// ---------------------------------------------------------------------------
class GDXLightSystem
{
public:
    GDXLightSystem()  = default;
    ~GDXLightSystem() = default;

    bool Init(ID3D11Device* device);
    void Shutdown();

    // Scannt die Registry, befüllt FrameData.lights + shadowViewProjMatrix,
    // lädt den LightBuffer auf die GPU (b3) und gibt ihn an VS+PS.
    void Update(Registry& registry, FrameData& frame, ID3D11DeviceContext* ctx);

    bool IsReady() const { return m_lightBuffer != nullptr; }

private:
    void UploadBuffer(ID3D11DeviceContext* ctx, const FrameData& frame);

    ID3D11Buffer* m_lightBuffer = nullptr;
};

// ---------------------------------------------------------------------------
// GDXLightCBuffer — GPU-Seite des LightBuffers (b3).
// Muss exakt mit LightBuffer im PixelShader übereinstimmen.
// ---------------------------------------------------------------------------
struct alignas(16) GDXLightCBufferEntry
{
    float position[4];       // xyz=pos,  w: 0=directional, 1=point, 2=spot
    float direction[4];      // xyz=dir (world space, normalized), w=unused
    float diffuse[4];        // rgb=color*intensity, a=radius
    float innerCosAngle;     // cos(innerConeAngle)
    float outerCosAngle;     // cos(outerConeAngle)
    float _pad0;
    float _pad1;
    // Gesamt: 3*16 + 4*4 = 64 Byte
};
static_assert(sizeof(GDXLightCBufferEntry) == 64, "LightEntry muss 64 Byte sein");

struct alignas(16) GDXLightCBuffer
{
    GDXLightCBufferEntry lights[MAX_LIGHTS];
    float                sceneAmbient[3]; // globale Szenen-Grundhelligkeit
    uint32_t             lightCount;
};
static_assert(sizeof(GDXLightCBuffer) == MAX_LIGHTS * 64 + 16, "LightCBuffer Layout falsch");
