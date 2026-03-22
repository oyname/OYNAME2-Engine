#pragma once
#include "Core/GDXMath.h"
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

// Maximale Anzahl Shadow-Kaskaden (CSM).
static constexpr uint32_t MAX_SHADOW_CASCADES = 4u;

// ---------------------------------------------------------------------------
// LightEntry — ein Licht für den cbuffer.
// Muss mit LightBuffer in PixelShader übereinstimmen (16-Byte-Alignment).
// ---------------------------------------------------------------------------
struct LightEntry
{
    GIDX::Float4 position  = { 0.0f, 0.0f, 0.0f, 0.0f }; // xyz=pos, w: 0=dir, 1=point, 2=spot
    GIDX::Float4 direction = { 0.0f,-1.0f, 0.0f, 0.0f }; // xyz=Richtung, w=castShadows(0/1)
    GIDX::Float4 diffuse   = { 1.0f, 1.0f, 1.0f, 1.0f }; // rgb*intensity, a=radius
    float             radius         = 0.0f;
    float             intensity      = 1.0f;
    float             innerCosAngle  = 0.9659f;  // cos(15°) — Spot inner cone
    float             outerCosAngle  = 0.8660f;  // cos(30°) — Spot outer cone
    // Gesamt: 3*16 + 4*4 = 64 Byte
};

// ---------------------------------------------------------------------------
// FrameData — wird pro Frame neu befüllt, niemals gecacht.
// ---------------------------------------------------------------------------
struct FrameData
{
    // --- Camera ---
    GIDX::Float4x4 viewMatrix     = {};
    GIDX::Float4x4 projMatrix     = {};
    GIDX::Float4x4 viewProjMatrix = {};  // view * proj
    GIDX::Float3   cameraPos      = {};
    float               _padCameraPos  = 0.0f;
    GIDX::Float3   cameraForward  = { 0.0f, 0.0f, 1.0f };
    uint32_t            cullMask       = 0xFFFFFFFFu;

    // --- Lights ---
    std::array<LightEntry, MAX_LIGHTS> lights     = {};
    uint32_t                           lightCount = 0u;

    // --- Scene Ambient — globale Grundhelligkeit, unabhängig von Lichtern ---
    // Wird einmal pro Frame gesetzt (z.B. von GDXECSRenderer oder der App).
    // Im Shader: ambient * albedo * ao als Basis-Term.
    GIDX::Float3 sceneAmbient = { 0.08f, 0.08f, 0.12f }; // kühles Standard-Ambient
    float             _padAmbient  = 0.0f;

    // --- Shadow (Cascaded Shadow Maps) ---
    // shadowViewProjMatrix = äußerste Kaskade — wird für Frustum-Culling des Shadow-Pass genutzt.
    GIDX::Float4x4 shadowViewProjMatrix = {};
    // Pro-Kaskaden-Daten (befüllt von GDXDX11LightSystem::Update).
    GIDX::Float4x4 shadowCascadeViewProj[MAX_SHADOW_CASCADES] = {};
    float          shadowCascadeSplits[MAX_SHADOW_CASCADES]   = {};  // View-Space-Tiefe
    uint32_t       shadowCascadeCount   = 0u;
    bool           hasShadowPass        = false;
    uint32_t       shadowCasterMask     = 0xFFFFFFFFu;
    uint32_t       lightAffectMask      = 0xFFFFFFFFu;

    // Viewport
    float viewportWidth  = 1280.0f;
    float viewportHeight = 720.0f;

    FrameData()
    {
        viewMatrix = GIDX::Identity4x4();
        projMatrix = GIDX::Identity4x4();
        viewProjMatrix = GIDX::Identity4x4();
        shadowViewProjMatrix = GIDX::Identity4x4();
    }
};
