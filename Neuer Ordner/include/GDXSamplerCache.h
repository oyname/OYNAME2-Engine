#pragma once
#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11SamplerState;

// ---------------------------------------------------------------------------
// GDXSamplerCache — geteilte Sampler-Zustände.
//
// Verbesserung gegenüber OYNAME:
//   In OYNAME hatte jede Textur ihren eigenen SamplerState → hunderte
//   identische DX11-Objekte. Hier werden 4 Sampler einmalig erstellt
//   und von allen Texturen geteilt.
//
//   s0: LinearWrap      — Standard für Albedo, Normal, ORM, Emissive
//   s1: LinearClamp     — für UI, Decals, Render-Texturen
//   s2: Anisotropic     — für qualitativ hochwertige Oberflächen
//   s7: PCFComparison   — Shadow Map (OYNAME-kompatibles Register s7)
// ---------------------------------------------------------------------------
class GDXSamplerCache
{
public:
    GDXSamplerCache()  = default;
    ~GDXSamplerCache() = default;

    bool Init(ID3D11Device* device);
    void Shutdown();

    // Bindet alle Sampler an VS+PS in einem Aufruf.
    // Sollte einmal pro Frame am Anfang von BeginFrame() aufgerufen werden.
    void BindAll(ID3D11DeviceContext* ctx) const;

    ID3D11SamplerState* GetLinearWrap()    const { return m_linearWrap; }
    ID3D11SamplerState* GetLinearClamp()   const { return m_linearClamp; }
    ID3D11SamplerState* GetAnisotropic()   const { return m_anisotropic; }
    ID3D11SamplerState* GetPCFComparison() const { return m_pcfComparison; }

    bool IsReady() const { return m_linearWrap != nullptr; }

private:
    ID3D11SamplerState* m_linearWrap    = nullptr;  // s0
    ID3D11SamplerState* m_linearClamp   = nullptr;  // s1
    ID3D11SamplerState* m_anisotropic   = nullptr;  // s2
    ID3D11SamplerState* m_pcfComparison = nullptr;  // s7 (Shadow PCF)
};
