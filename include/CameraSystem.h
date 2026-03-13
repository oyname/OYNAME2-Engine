#pragma once
#include "Registry.h"
#include "Components.h"
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

    // Befüllt FrameData::viewMatrix, projMatrix, viewProjMatrix, cameraPos.
    // Muss nach TransformSystem::Update() aufgerufen werden.
    void Update(Registry& registry, FrameData& frame) const;
    bool BuildFrameDataForCamera(Registry& registry, EntityID cameraEntity, FrameData& frame, float aspectOverride = 0.0f) const;

    // Hilfsmethode: Berechnet Tiefe eines Weltpunkts in NDC (0..1).
    // Wird von RenderGatherSystem für den Sort-Key-Depth-Wert verwendet.
    static float ComputeNDCDepth(const DirectX::XMFLOAT4X4& worldMatrix,
                                  const DirectX::XMFLOAT4X4& viewProjMatrix);
};
