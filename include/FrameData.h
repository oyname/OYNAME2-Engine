#pragma once
#include <DirectXMath.h>
#include <cstdint>
#include <array>

// ---------------------------------------------------------------------------
// FrameData — alle pro-Frame berechneten Render-Daten.
//
// Wird von Systemen befüllt und an den Renderer übergeben.
// Kein System speichert FrameData dauerhaft — es ist ein Frame-lokales Struct.
//
// Befüllungsreihenfolge:
//   1. CameraSystem  → cameraPos, viewMatrix, projMatrix, viewProjMatrix
//   2. LightSystem   → lights, lightCount, shadowViewProj
//   3. RenderGatherSystem liest FrameData und baut RenderQueue
// ---------------------------------------------------------------------------

// Maximale Anzahl Lichter — muss mit dem HLSL LightBuffer übereinstimmen.
static constexpr uint32_t MAX_LIGHTS = 32u;

// ---------------------------------------------------------------------------
// LightEntry — ein Licht für den cbuffer.
// Muss mit LightBuffer in PixelShader übereinstimmen (16-Byte-Alignment).
// ---------------------------------------------------------------------------
struct LightEntry
{
    DirectX::XMFLOAT4 position  = { 0.0f, 0.0f, 0.0f, 0.0f }; // W=0: directional, W=1: point
    DirectX::XMFLOAT4 direction = { 0.0f,-1.0f, 0.0f, 0.0f }; // nur Directional
    DirectX::XMFLOAT4 diffuse   = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4 ambient   = { 0.1f, 0.1f, 0.1f, 1.0f };
    float             radius    = 0.0f;
    float             intensity = 1.0f;
    float             _pad0     = 0.0f;
    float             _pad1     = 0.0f;
    // Gesamtgröße: 4*16 + 4*4 = 80 Byte
};

// ---------------------------------------------------------------------------
// FrameData — wird pro Frame neu befüllt, niemals gecacht.
// ---------------------------------------------------------------------------
struct FrameData
{
    // --- Camera ---
    DirectX::XMFLOAT4X4 viewMatrix     = {};
    DirectX::XMFLOAT4X4 projMatrix     = {};
    DirectX::XMFLOAT4X4 viewProjMatrix = {};  // view * proj
    DirectX::XMFLOAT3   cameraPos      = {};
    float               _padCam        = 0.0f;

    // --- Lights ---
    std::array<LightEntry, MAX_LIGHTS> lights      = {};
    uint32_t                           lightCount  = 0u;

    // --- Shadow (Directional Light) ---
    DirectX::XMFLOAT4X4 shadowViewProjMatrix = {};
    bool                 hasShadowPass        = false;

    // Viewport
    float viewportWidth  = 1280.0f;
    float viewportHeight = 720.0f;

    FrameData()
    {
        DirectX::XMStoreFloat4x4(&viewMatrix,     DirectX::XMMatrixIdentity());
        DirectX::XMStoreFloat4x4(&projMatrix,     DirectX::XMMatrixIdentity());
        DirectX::XMStoreFloat4x4(&viewProjMatrix, DirectX::XMMatrixIdentity());
        DirectX::XMStoreFloat4x4(&shadowViewProjMatrix, DirectX::XMMatrixIdentity());
    }
};
