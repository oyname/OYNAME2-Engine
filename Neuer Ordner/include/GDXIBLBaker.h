#pragma once
// GDXIBLBaker.h — Image-Based Lighting, reines CPU-Baking.
//
// Kein DX11, kein OpenGL — nur Mathe auf float-Puffern.
// Das Backend (DX11, OpenGL, ...) ist selbst verantwortlich die
// Ergebnispuffer als Texturen hochzuladen.
//
// Ablauf:
//   GDXIBLData data = GDXIBLBaker::Bake(path, ...);
//   // data.irradiance[], data.prefiltered[], data.brdfLut[] sind jetzt gültig
//   // → Backend lädt sie hoch

#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// Ergebnisdaten — reine CPU-Puffer, kein GPU-Handle
// ---------------------------------------------------------------------------
struct GDXIBLData
{
    // Irradiance-Cubemap: 6 Faces × irrSize × irrSize × RGBA32F
    // Layout: face0_pixels, face1_pixels, ..., face5_pixels
    std::vector<float> irradiance;
    uint32_t           irrSize = 0u;

    // Prefiltered-Env-Cubemap: alle Mips linear hintereinander
    // Layout: Mip0(6*envSize^2*4), Mip1(6*(envSize/2)^2*4), ...
    std::vector<float> prefiltered;
    uint32_t           envSize = 0u;
    uint32_t           envMips = 0u;

    // BRDF Split-Sum LUT: lutSize x lutSize x RG32F
    std::vector<float> brdfLut;
    uint32_t           lutSize = 0u;

    bool valid = false;
};

// ---------------------------------------------------------------------------
// Baker — statische API, keine Instanz nötig
// ---------------------------------------------------------------------------
class GDXIBLBaker
{
public:
    // HDR-Datei laden und alle drei Puffer backen.
    // irrSize      = Irradiance-Cubemap-Seitenlaenge pro Face (empfohlen: 32)
    // envSize      = Prefiltered-Seitenlaenge pro Face Mip0   (empfohlen: 128)
    // lutSize      = BRDF-LUT-Seitenlaenge                    (empfohlen: 256)
    // envMipLevels = Anzahl Roughness-Mip-Stufen               (empfohlen: 5)
    static GDXIBLData Bake(
        const wchar_t* hdrPath,
        uint32_t       irrSize      = 32u,
        uint32_t       envSize      = 128u,
        uint32_t       lutSize      = 256u,
        uint32_t       envMipLevels = 5u);

    // Neutraler Fallback ohne HDR-Datei.
    static GDXIBLData MakeFallback(
        uint32_t irrSize = 32u,
        uint32_t envSize = 64u,
        uint32_t lutSize = 128u,
        uint32_t envMips = 5u);
};
