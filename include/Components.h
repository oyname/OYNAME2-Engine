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
// Wichtige Korrektur gegenüber Step-1-Entwurf:
//   - MaterialComponent (mit inline albedo, metallic etc.) → ENTFERNT.
//     Ersetzt durch MaterialRefComponent (Handle).
//     Inline-Materialdaten pro Entity erzwingen Kopien und verhindern Batching.
//   - MeshComponent (mit uint32_t meshAssetID) → ENTFERNT.
//     Ersetzt durch MeshRefComponent (MeshHandle).
//     uint32_t ohne typisierten Store ist keine Lifetime-Garantie.
// ---------------------------------------------------------------------------

#include "Handle.h"
#include "ECSTypes.h"   // EntityID, NULL_ENTITY

#include <cstdint>
#include <string>
#include <DirectXMath.h>

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
    DirectX::XMFLOAT3 localPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 localRotation = { 0.0f, 0.0f, 0.0f, 1.0f }; // Quaternion (X,Y,Z,W)
    DirectX::XMFLOAT3 localScale = { 1.0f, 1.0f, 1.0f };

    bool dirty = true;  // true → WorldTransformComponent muss neu berechnet werden

    TransformComponent() = default;

    // Bequemer Konstruktor: nur Position.
    TransformComponent(float px, float py, float pz)
        : localPosition{ px, py, pz }, dirty(true) {
    }

    // Hilfsmethode: Euler-Winkel (Grad) in Quaternion setzen.
    // Reihenfolge: Pitch (X), Yaw (Y), Roll (Z).
    void SetEulerDeg(float pitchDeg, float yawDeg, float rollDeg)
    {
        const float toRad = DirectX::XM_PI / 180.0f;
        DirectX::XMStoreFloat4(
            &localRotation,
            DirectX::XMQuaternionRotationRollPitchYaw(
                pitchDeg * toRad,
                yawDeg * toRad,
                rollDeg * toRad
            )
        );
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
    DirectX::XMFLOAT4X4 matrix = {};  // Weltmatrix
    DirectX::XMFLOAT4X4 inverse = {};  // Inverse der Weltmatrix (für Beleuchtung)

    WorldTransformComponent()
    {
        DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixIdentity());
        DirectX::XMStoreFloat4x4(&inverse, DirectX::XMMatrixIdentity());
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
// MeshRefComponent — Referenz auf eine Mesh-Ressource per Handle.
//
// Ersetzt das alte MeshComponent { uint32_t meshAssetID }.
// Ein MeshHandle referenziert einen Slot im ResourceStore<MeshAssetResource>.
//
// submeshIndex: Welcher Sub-Mesh-Slot soll gerendert werden?
//   0 = erster Slot (Standard).
//   Für Multi-Mesh-Entities: mehrere MeshRefComponents (eine pro Slot).
//   Oder: eine Entity pro Slot, alle als Kind der Eltern-Entity.
// ===========================================================================
struct MeshRefComponent
{
    MeshHandle mesh;
    uint32_t   submeshIndex = 0u;
    bool       enabled = true;

    MeshRefComponent() = default;
    explicit MeshRefComponent(MeshHandle h, uint32_t slot = 0u)
        : mesh(h), submeshIndex(slot) {
    }
};

// ===========================================================================
// MaterialRefComponent — Referenz auf eine Material-Ressource per Handle.
//
// Ersetzt das alte MaterialComponent mit inline PBR-Daten.
//
// WARUM Handle statt inline Daten?
//   - Geteilte Materialien: 100 Entities mit demselben Material → 1 GPU-Upload.
//   - Batching: Sort nach materialHandle.value minimiert Shader/State-Wechsel.
//   - Lazy GPU-Upload: Material wird nur einmal auf die GPU gebracht.
//
// Für per-Entity-Overrides:
//   Entweder ein neues MaterialResource im Store anlegen (billiges Shallow-Copy),
//   oder MaterialOverrideComponent hinzufügen (kommt in Phase 5).
// ===========================================================================
struct MaterialRefComponent
{
    MaterialHandle material;

    MaterialRefComponent() = default;
    explicit MaterialRefComponent(MaterialHandle h) : material(h) {}
};

// ===========================================================================
// VisibilityComponent — Render-Sichtbarkeits- und Layer-Flags.
//
// BEWUSST GETRENNT von TransformComponent.
// Ein Transform ist ein geometrisches Konzept.
// Visibility ist ein Render-Metadatum.
//
// Entities ohne VisibilityComponent werden vom Renderer übersprungen,
// wenn das System explizit nach Visibility filtert.
// ===========================================================================
struct VisibilityComponent
{
    bool     visible   = true;
    bool     active    = true;
    uint32_t layerMask = 0x00000001u;  // LAYER_DEFAULT

    // Schatten werfen: RenderGatherSystem liest dieses Flag um ShadowCasterTag zu ersetzen.
    // Schatten empfangen: liegt in MaterialData.receiveShadows (float, direkt im Shader).
    // → kein receiveShadows hier, verhindert die doppelte inkonsistente Logik.
    bool castShadows = true;

    VisibilityComponent() = default;
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
// LightKind — Lichttyp.
// ===========================================================================
enum class LightKind : uint8_t
{
    Directional = 0,
    Point       = 1,
    Spot        = 2,   // Kegel-Licht: Position + Richtung + inner/outer cone
};

// ===========================================================================
// LightComponent — Lichtparameter.
// ===========================================================================
struct LightComponent
{
    LightKind kind = LightKind::Directional;

    DirectX::XMFLOAT4 diffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    float radius         = 10.0f;   // Point/Spot: Reichweite in Welteinheiten
    float intensity      = 1.0f;

    // Spot-Light Kegel (in Grad)
    float innerConeAngle = 15.0f;
    float outerConeAngle = 30.0f;

    // Shadow (nur Directional)
    bool  castShadows     = false;
    float shadowOrthoSize = 50.0f;
    float shadowNear      = 0.1f;
    float shadowFar       = 1000.0f;

    LightComponent() = default;
};

// ===========================================================================
// ShadowCasterTag — Marker: diese Entity wirft Schatten.
//
// ShadowGatherSystem filtert: View<WorldTransformComponent, MeshRefComponent, ShadowCasterTag>
// ===========================================================================
struct ShadowCasterTag {};
