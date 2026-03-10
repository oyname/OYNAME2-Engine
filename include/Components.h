#pragma once

// ---------------------------------------------------------------------------
// Components.h — alle Component-Typen für Step 1.
//
// Design-Regel:
//   Components sind PLAIN DATA — keine Methoden außer Konstruktoren.
//   Logik gehört in Systems.
//
// Herkunft:
//   Jeder Component ist direkt aus einer bestehenden OYNAME-Klasse
//   destilliert.  Die Originale (Entity, Mesh, Camera, Light, ...) bleiben
//   unverändert — dieser Header ist ein ADDENDUM, kein Ersatz.
//
// Schritt-Strategie:
//   Step 1: Components definieren, Registry befüllen, Systems iterieren.
//   Step 2: OYNAME-Klassen schrittweise durch Components ersetzen.
//   Step 3: Scene / ObjectManager auf Registry umstellen.
// ---------------------------------------------------------------------------

#include <string>
#include <cstdint>
#include <array>

// ---------------------------------------------------------------------------
// TagComponent — menschenlesbarer Name einer Entity.
//
// Aus: Entity::m_name (bisher nicht vorhanden, wird frisch eingeführt)
// ---------------------------------------------------------------------------
struct TagComponent
{
    std::string name;

    TagComponent() = default;
    explicit TagComponent(std::string n) : name(std::move(n)) {}
};

// ---------------------------------------------------------------------------
// TransformComponent — Position, Rotation (Euler Grad), Skalierung.
//
// Aus: Entity::transform  (Transform-Klasse mit XMVECTOR/Quaternion)
//
// Für Step 1 verwenden wir einfache float[3] / float[4] damit der Component
// keine DirectX-Headers zieht.  In Step 2 wird Transform direkt integriert.
// ---------------------------------------------------------------------------
struct TransformComponent
{
    float position[3] = { 0.0f, 0.0f, 0.0f };
    float rotation[3] = { 0.0f, 0.0f, 0.0f };  // Euler Grad (pitch, yaw, roll)
    float scale[3]    = { 1.0f, 1.0f, 1.0f };

    // Flags aus Entity
    bool  active       = true;
    bool  visible      = true;
    bool  castShadows  = true;
    uint32_t layerMask = 0x00000001u;  // LAYER_DEFAULT

    TransformComponent() = default;

    TransformComponent(float px, float py, float pz)
    {
        position[0] = px; position[1] = py; position[2] = pz;
    }
};

// ---------------------------------------------------------------------------
// MeshComponent — Referenz auf Geometrie + Material-Slots.
//
// Aus: Mesh  (Entity-Subklasse mit MeshRenderer, Surfaces, MeshAsset)
//
// In Step 1 speichern wir nur die IDs / Indizes.  Die eigentliche
// GPU-Ressource kommt im ResourceManager (Step 4).
// ---------------------------------------------------------------------------
struct MeshComponent
{
    // Index in den zukünftigen ResourceManager — Step 1: nur Platzhalter.
    uint32_t meshAssetID   = 0;   // 0 = kein Asset zugewiesen

    // Skinning
    bool     hasSkinning   = false;

    // Sichtbarkeits-Override (zusätzlich zu TransformComponent::visible)
    bool     enabled       = true;

    MeshComponent() = default;
    explicit MeshComponent(uint32_t assetID) : meshAssetID(assetID) {}
};

// ---------------------------------------------------------------------------
// MaterialComponent — PBR-Materialdaten pro Entity.
//
// Aus: Material::MaterialData  (HLSL cbuffer b2)
// ---------------------------------------------------------------------------
struct MaterialComponent
{
    float    albedo[4]        = { 1.0f, 1.0f, 1.0f, 1.0f };
    float    metallic         = 0.0f;
    float    roughness        = 0.5f;
    float    ao               = 1.0f;
    float    emissiveStrength = 0.0f;
    float    emissiveColor[3] = { 0.0f, 0.0f, 0.0f };

    // Textur-IDs (0 = kein Textur, Fallback auf Skalare)
    uint32_t albedoTexID            = 0;
    uint32_t normalTexID            = 0;
    uint32_t metallicRoughnessTexID = 0;
    uint32_t aoTexID                = 0;
    uint32_t emissiveTexID          = 0;

    MaterialComponent() = default;

    static MaterialComponent FlatColor(float r, float g, float b, float a = 1.0f)
    {
        MaterialComponent m;
        m.albedo[0] = r; m.albedo[1] = g;
        m.albedo[2] = b; m.albedo[3] = a;
        return m;
    }
};

// ---------------------------------------------------------------------------
// CameraComponent — View/Projection Kameraparameter.
//
// Aus: Camera  (Entity-Subklasse)
// ---------------------------------------------------------------------------
struct CameraComponent
{
    float fov         = 60.0f;   // Grad
    float nearPlane   = 0.1f;
    float farPlane    = 1000.0f;
    float aspectRatio = 16.0f / 9.0f;

    bool  isOrtho     = false;
    float orthoWidth  = 10.0f;
    float orthoHeight = 10.0f;

    // Kamera-Blickrichtung (world-space, werden aus TransformComponent berechnet)
    float target[3] = { 0.0f, 0.0f, 1.0f };
    float up[3]     = { 0.0f, 1.0f, 0.0f };

    uint32_t cullMask = 0xFFFFFFFFu;  // LAYER_ALL

    CameraComponent() = default;
};

// ---------------------------------------------------------------------------
// LightComponent — Lichtparameter.
//
// Aus: Light  (Entity-Subklasse, LightBufferData)
// ---------------------------------------------------------------------------
enum class LightKind : uint8_t
{
    Directional = 0,
    Point       = 1,
};

struct LightComponent
{
    LightKind kind = LightKind::Directional;

    float diffuseColor[4]  = { 1.0f, 1.0f, 1.0f, 1.0f };
    float ambientColor[4]  = { 0.1f, 0.1f, 0.1f, 1.0f };

    float radius           = 10.0f;    // Point light only
    float intensity        = 1.0f;

    // Shadow settings (Directional)
    bool  castShadows      = false;
    float shadowOrthoSize  = 50.0f;
    float shadowNear       = 0.1f;
    float shadowFar        = 1000.0f;

    LightComponent() = default;
};
