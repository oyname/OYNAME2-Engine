#pragma once

// ---------------------------------------------------------------------------
// Components.h — ECS-Kern-Komponenten (renderer-frei).
//
// Enthält: TagComponent, TransformComponent, WorldTransformComponent,
//          ParentComponent, ChildrenComponent.
//
// Renderer-Komponenten (RenderableComponent, CameraComponent, LightComponent
// etc.) sind nach RenderComponents.h ausgelagert — analog zu
// CollisionBodyComponent.h für das Collision-Modul.
//
// RenderComponents.h wird hier am Ende includiert damit bestehender App-Code
// der nur Components.h includiert weiter kompiliert ohne Änderung.
// ---------------------------------------------------------------------------

#include "ECS/ECSTypes.h"
#include "Core/GDXMath.h"

#include <cstdint>
#include <string>
#include <vector>
#include <cmath>

// ===========================================================================
// TagComponent
// ===========================================================================
struct TagComponent
{
    std::string name;

    TagComponent() = default;
    explicit TagComponent(std::string n) : name(std::move(n)) {}
};

// ===========================================================================
// TransformComponent
// ===========================================================================
struct TransformComponent
{
    GIDX::Float3 localPosition = { 0.0f, 0.0f, 0.0f };
    GIDX::Float4 localRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    GIDX::Float3 localScale    = { 1.0f, 1.0f, 1.0f };

    bool     dirty        = true;
    uint32_t localVersion = 1u;
    uint32_t worldVersion = 0u;

    TransformComponent() = default;

    TransformComponent(float px, float py, float pz)
        : localPosition{ px, py, pz }, dirty(true) {}

    void SetEulerDeg(float pitchDeg, float yawDeg, float rollDeg)
    {
        localRotation = GIDX::QuaternionFromEulerDeg(pitchDeg, yawDeg, rollDeg);
        dirty = true;
    }
};

// ===========================================================================
// WorldTransformComponent — wird ausschließlich vom TransformSystem geschrieben.
// ===========================================================================
struct WorldTransformComponent
{
    GIDX::Float4x4 matrix  = {};
    GIDX::Float4x4 inverse = {};

    WorldTransformComponent()
    {
        matrix  = GIDX::Identity4x4();
        inverse = GIDX::Identity4x4();
    }
};

// ===========================================================================
// ParentComponent
// ===========================================================================
struct ParentComponent
{
    EntityID parent = NULL_ENTITY;

    ParentComponent() = default;
    explicit ParentComponent(EntityID p) : parent(p) {}
};

// ===========================================================================
// ChildrenComponent — wird ausschließlich von HierarchySystem gepflegt.
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
