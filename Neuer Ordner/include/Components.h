#pragma once

// ---------------------------------------------------------------------------
// Components.h — alle ECS-Komponenten der GIDX-Engine.
//
// Design-Regeln (strikt):
//   1. Komponenten sind Plain Data — keine Methoden außer Konstruktoren.
//   2. Geteilte Ressourcen (Mesh, Material, Shader, Texture) werden per
//      Handle referenziert, nie als Zeiger oder inline kopiert.
//   3. Jede Komponente hat EINE fachliche Verantwortung (SRP).
//   4. Visibility-Flags gehören NICHT in TransformComponent.
//   5. WorldTransformComponent wird vom TransformSystem geschrieben,
//      alle anderen Systeme lesen sie nur.
//
// ---------------------------------------------------------------------------

#include "Handle.h"
#include "ECSTypes.h"   // EntityID, NULL_ENTITY
#include "RenderPassClearDesc.h"
#include <cstdint>
#include <string>
#include <vector>
#include "GDXMath.h"

// ===========================================================================
// TagComponent — lesbarer Name einer Entity. Kein funktionaler Einfluss.
// ===========================================================================
struct TagComponent
{
    std::string name;

    TagComponent() = default;
    explicit TagComponent(std::string n) : name(std::move(n)) {}
};

// ===========================================================================
// TransformComponent — lokale Position, Rotation (Quaternion), Skalierung.
//
// Speichert NUR die lokalen Primitivdaten.
// Weltmatrix liegt in WorldTransformComponent (wird vom TransformSystem erzeugt).
//
// Dirty-Flag:
//   Wird auf true gesetzt, wenn Position/Rotation/Scale sich ändern.
//   TransformSystem liest dirty, berechnet WorldTransform, setzt dirty=false.
//
// NICHT in TransformComponent:
//   visible, active, layerMask, castShadows → VisibilityComponent
//   GPU-Daten → WorldTransformComponent
// ===========================================================================
struct TransformComponent
{
    GIDX::Float3 localPosition = { 0.0f, 0.0f, 0.0f };
    GIDX::Float4 localRotation = { 0.0f, 0.0f, 0.0f, 1.0f }; // Quaternion (X,Y,Z,W)
    GIDX::Float3 localScale = { 1.0f, 1.0f, 1.0f };

    bool dirty = true;  // true → WorldTransformComponent muss neu berechnet werden
    uint32_t localVersion = 1u;   // erhöht sich bei lokaler Änderung / Dirty-Markierung
    uint32_t worldVersion = 0u;   // erhöht sich wenn WorldTransform neu geschrieben wurde

    TransformComponent() = default;

    // Bequemer Konstruktor: nur Position.
    TransformComponent(float px, float py, float pz)
        : localPosition{ px, py, pz }, dirty(true) {
    }

    // Hilfsmethode: Euler-Winkel (Grad) in Quaternion setzen.
    // Reihenfolge: Pitch (X), Yaw (Y), Roll (Z).
    void SetEulerDeg(float pitchDeg, float yawDeg, float rollDeg)
    {
        localRotation = GIDX::QuaternionFromEulerDeg(pitchDeg, yawDeg, rollDeg);
        dirty = true;
    }
};

// ===========================================================================
// WorldTransformComponent — berechnete Weltmatrix.
//
// Wird ausschließlich vom TransformSystem geschrieben.
// Alle anderen Systeme (Renderer, Physics, Audio) lesen nur.
//
// Muss auf Entities vorhanden sein, die gerendert oder in Weltkoordinaten
// abgefragt werden sollen. TransformSystem erstellt sie ggf. automatisch.
// ===========================================================================
struct WorldTransformComponent
{
    GIDX::Float4x4 matrix = {};  // Weltmatrix
    GIDX::Float4x4 inverse = {};  // Inverse der Weltmatrix (für Beleuchtung)

    WorldTransformComponent()
    {
        matrix = GIDX::Identity4x4();
        inverse = GIDX::Identity4x4();
    }
};

// ===========================================================================
// ParentComponent — Parent-Kind-Hierarchie.
//
// Entities mit ParentComponent erhalten ihre Weltmatrix als:
//   world = local * parent.world
//
// TransformSystem verarbeitet Parent-Entities vor Kind-Entities.
// Ein Entity ohne ParentComponent ist eine Root-Entity.
// ===========================================================================
struct ParentComponent
{
    EntityID parent = NULL_ENTITY;  // NULL_ENTITY = kein Parent

    ParentComponent() = default;
    explicit ParentComponent(EntityID p) : parent(p) {}
};
// ===========================================================================
// ChildrenComponent — Rückreferenz vom Parent zu seinen direkten Kindern.
//
// Wird ausschließlich von HierarchySystem::SetParent / Detach gepflegt.
// Ermöglicht O(1)-Kindlookup und O(depth) dirty-Propagation.
//
// NICHT manuell befüllen.
// Entities ohne Kinder haben keine ChildrenComponent.
// ===========================================================================
struct ChildrenComponent
{
    std::vector<EntityID> children;

    uint32_t Count() const noexcept
    {
        return static_cast<uint32_t>(children.size());
    }

    void Add(EntityID child)
    {
        for (EntityID e : children)
            if (e == child) return;
        children.push_back(child);
    }

    void Remove(EntityID child)
    {
        for (auto it = children.begin(); it != children.end(); ++it)
        {
            if (*it == child)
            {
                *it = children.back();
                children.pop_back();
                return;
            }
        }
    }
};



// ===========================================================================
// RenderableComponent — renderrelevante Kern-Daten in EINER Komponente.
//
// Primärpfad für Mesh/Material/Submesh/Enabled.
// Sichtbarkeits- und Layer-Flags gehören NICHT hier hinein, sondern exklusiv
// in VisibilityComponent. Damit gibt es nur noch eine Wahrheitsquelle für
// visible/active/layerMask/castShadows/receiveShadows.
// ===========================================================================
struct RenderableComponent
{
    MeshHandle     mesh;
    MaterialHandle material;
    uint32_t       submeshIndex = 0u;
    bool           enabled = true;

    // Dirty-/Versionsdaten für Mesh/Material/Submesh/Enabled.
    bool           dirty = true;
    uint32_t       stateVersion = 1u;

    RenderableComponent() = default;
    RenderableComponent(MeshHandle meshHandle, MaterialHandle materialHandle, uint32_t slot = 0u)
        : mesh(meshHandle), material(materialHandle), submeshIndex(slot)
    {
    }
};

// ===========================================================================
// SkinComponent — Laufzeit-Bone-Palette für skinnte Meshes.
// Erwartet finale Bone-Matrizen im Mesh-Lokalraum.
// ===========================================================================
struct SkinComponent
{
    static constexpr uint32_t MaxBones = 64u;

    std::vector<GIDX::Float4x4> finalBoneMatrices;
    bool enabled = true;

    SkinComponent() = default;
};

// ===========================================================================
// VisibilityComponent — Render-Sichtbarkeits- und Layer-Flags.
//
// BEWUSST GETRENNT von TransformComponent.
// Ein Transform ist ein geometrisches Konzept.
// Visibility ist ein Render-Metadatum.
//
// VisibilityComponent ist Pflicht für renderbare Entities.
// Entities ohne VisibilityComponent werden vom Renderer übersprungen.
// ===========================================================================
struct VisibilityComponent
{
    bool     visible = true;
    bool     active = true;
    uint32_t layerMask = 0x00000001u;  // LAYER_DEFAULT

    // Schatten werfen: kommt in den Shadow-Pass.
    bool castShadows = true;

    // Schatten empfangen: pro Entity. Standard = true.
    // Wird im Main-Pass mit MaterialData.receiveShadows kombiniert.
    bool receiveShadows = true;

    // Dirty-/Versionsdaten für Gather-/Render-Cache.
    bool     dirty = true;
    uint32_t stateVersion = 1u;

    VisibilityComponent() = default;
};


// ===========================================================================
// RenderBoundsComponent — renderrelevante lokale Bounds für Visibility/Culling.
//
// Bewusst getrennt von Mesh-/Scene-Logik:
// - nur Daten für vorbereitende Sichtbarkeitsprüfung
// - keine Objektmethoden
// - keine Backend-Abfragen im Draw
//
// Erste Ausbaustufe:
// - Bounding Sphere als Hauptpfad
// - lokale AABB optional für spätere präzisere Tests vorhanden
// ===========================================================================
struct RenderBoundsComponent
{
    enum class Shape : uint8_t
    {
        Sphere = 0,
        AABB = 1,
    };

    Shape shape = Shape::Sphere;
    GIDX::Float3 localCenter = { 0.0f, 0.0f, 0.0f };
    float localSphereRadius = 0.5f;

    GIDX::Float3 localAabbMin = { -0.5f, -0.5f, -0.5f };
    float _pad0 = 0.0f;
    GIDX::Float3 localAabbMax = { 0.5f, 0.5f, 0.5f };
    float _pad1 = 0.0f;

    bool valid = false;
    bool enableDistanceCull = false;
    float maxViewDistance = 0.0f;
    float _pad2 = 0.0f;

    RenderBoundsComponent() = default;

    static RenderBoundsComponent MakeSphere(const GIDX::Float3& center, float radius)
    {
        RenderBoundsComponent b{};
        b.shape = Shape::Sphere;
        b.localCenter = center;
        b.localSphereRadius = radius;
        b.valid = radius > 0.0f;
        return b;
    }
};

// ===========================================================================
// CameraComponent — Projektionsparameter.
//
// View- und Projektionsmatrix werden vom CameraSystem pro Frame berechnet
// und in FrameData abgelegt — NICHT in dieser Komponente gespeichert.
// ===========================================================================
struct CameraComponent
{
    float fovDeg = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    float aspectRatio = 16.0f / 9.0f;

    bool  isOrtho = false;
    float orthoWidth = 10.0f;
    float orthoHeight = 10.0f;

    uint32_t cullMask = 0xFFFFFFFFu;  // LAYER_ALL

    CameraComponent() = default;
};

// ===========================================================================
// ActiveCameraTag — Marker: diese Entity ist die aktive Kamera.
//
// Genau eine Entity soll diesen Tag tragen.
// CameraSystem filtert: View<WorldTransformComponent, CameraComponent, ActiveCameraTag>
// ===========================================================================
struct ActiveCameraTag {};

// ===========================================================================
// RenderTargetCameraComponent — rendert diese Kamera zuerst in ein Offscreen-
// Target. Die erzeugte Farbausgabe kann über exposedTexture im Materialsystem
// wieder gelesen werden.
// ===========================================================================
struct RenderTargetCameraComponent
{
    RenderTargetHandle target = RenderTargetHandle::Invalid();
    bool enabled = true;
    bool autoAspectFromTarget = true;

    // Pass-Filter für diese Kamera. Standard = altes Verhalten.
    bool renderShadows = true;
    bool renderOpaque = true;
    bool renderTransparent = true;

    // Sicherheitsnetz für RTT: Draws überspringen, die dieselbe RTT-Textur
    // im selben Pass wieder als Shader-Resource lesen würden.
    bool skipSelfReferentialDraws = true;

    RenderPassClearDesc clear;

    RenderTargetCameraComponent() = default;
    explicit RenderTargetCameraComponent(RenderTargetHandle h) : target(h) {}
};

// ===========================================================================
// LightKind — Lichttyp.
// ===========================================================================
enum class LightKind : uint8_t
{
    Directional = 0,
    Point = 1,
    Spot = 2,   // Kegel-Licht: Position + Richtung + inner/outer cone
};

// ===========================================================================
// LightComponent — Lichtparameter.
// ===========================================================================
struct LightComponent
{
    LightKind kind = LightKind::Directional;

    GIDX::Float4 diffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    float radius = 10.0f;   // Point/Spot: Reichweite in Welteinheiten
    float intensity = 1.0f;

    // Spot-Light Kegel (in Grad)
    float innerConeAngle = 15.0f;
    float outerConeAngle = 30.0f;

    // Shadow (nur Directional)
    bool  castShadows = false;
    float shadowOrthoSize = 50.0f;  // Fallback wenn shadowCascadeCount == 0
    float shadowNear = 0.1f;
    float shadowFar = 1000.0f;

    // Cascaded Shadow Maps — 0 = Legacy (1 Kaskade via shadowOrthoSize)
    uint32_t shadowCascadeCount  = 4u;     // 1..MAX_SHADOW_CASCADES
    float    shadowCascadeLambda = 0.75f;  // 0=linear, 1=logarithmisch
    uint32_t shadowMapSize       = 2048u;  // Muss mit GDXECSRenderer::SetShadowMapSize übereinstimmen

    // Layer-/Affect-Masken.
    // affectLayerMask: Welche sichtbaren Layer dieses Licht grundsätzlich beleuchtet.
    // shadowLayerMask: Welche Layer in den Shadow-Pass dieses Lichts aufgenommen werden.
    uint32_t affectLayerMask = 0xFFFFFFFFu;
    uint32_t shadowLayerMask = 0xFFFFFFFFu;

    LightComponent() = default;
};

