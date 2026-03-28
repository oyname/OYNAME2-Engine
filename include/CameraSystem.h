#pragma once
#include "ECS/Registry.h"
#include "Components.h"
#include "RenderComponents.h"
#include "FrameData.h"

// ---------------------------------------------------------------------------
// CameraSystem — berechnet View/Proj-Matrix für den aktuellen Frame.
//
// Liest: WorldTransformComponent, CameraComponent, ActiveCameraTag
// Schreibt: FrameData (viewMatrix, projMatrix, viewProjMatrix, cameraPos)
//
// Genau eine Entity sollte ActiveCameraTag tragen.
// Falls keine vorhanden: Identitätsmatrizen bleiben in FrameData.
// ---------------------------------------------------------------------------
class CameraSystem
{
public:
    CameraSystem() = default;

    // Befüllt den Frame vollständig aus genau EINER aktiven Kamera-Entity.
    // Muss nach TransformSystem::Update() aufgerufen werden.
    // Rückgabewert false = keine gültige aktive Kamera gefunden.
    bool Update(Registry& registry, FrameData& frame) const;
    bool BuildFrameDataForCamera(Registry& registry, EntityID cameraEntity, FrameData& frame, float aspectOverride = 0.0f) const;

    // Sucht die aktuell aktive Kamera (ActiveCameraTag + CameraComponent + WorldTransformComponent).
    // Gibt NULL_ENTITY zurück falls keine gültige Kamera gefunden wurde.
    EntityID FindActiveCameraEntity(Registry& registry) const;

    // Erzwingt genau eine aktive Kamera. Entfernt ActiveCameraTag von allen anderen Kamera-Entities.
    bool SetActiveCamera(Registry& registry, EntityID cameraEntity) const;

    // Kern-Berechnung — direkt aus WorldTransform + CameraComponent.
    // Wird intern von Update() und GDXECSRenderer genutzt um Duplikate zu vermeiden.
    static void BuildFrameData(const WorldTransformComponent& wt, const CameraComponent& cam, FrameData& frame);

    // Hilfsmethode: Berechnet Tiefe eines Weltpunkts in NDC (0..1).
    // Wird von RenderGatherSystem für den Sort-Key-Depth-Wert verwendet.
    static float ComputeNDCDepth(const Matrix4& worldMatrix,
                                  const Matrix4& viewProjMatrix);
};
