#pragma once
// ============================================================
//  IGDXParticleRenderer.h  --  backend-neutral particle renderer interface
// ============================================================
#include "Particles/GDXParticleTypes.h"
#include "Core/GDXMath.h"

#include <vector>

struct ParticleRenderContext
{
    Matrix4 viewMatrix = Matrix4::Identity();
    Matrix4 projMatrix = Matrix4::Identity();
    Matrix4 viewProj = Matrix4::Identity();
    Float3  cameraPosition = { 0.0f, 0.0f, 0.0f };
    Float3  cameraForward = { 0.0f, 0.0f, 1.0f };
    Float3  camRight = { 1.0f, 0.0f, 0.0f };
    Float3  camUp = { 0.0f, 1.0f, 0.0f };
    float   cameraNearPlane = 0.1f;
    float   viewportWidth = 1280.0f;
    float   viewportHeight = 720.0f;
};

struct ParticleRenderSubmission
{
    ParticleRenderContext context{};
    std::vector<ParticleInstance> alphaInstances{};
    std::vector<ParticleInstance> additiveInstances{};

    void Clear()
    {
        alphaInstances.clear();
        additiveInstances.clear();
    }

    bool Empty() const
    {
        return alphaInstances.empty() && additiveInstances.empty();
    }

    uint32_t InstanceCount() const
    {
        return static_cast<uint32_t>(alphaInstances.size() + additiveInstances.size());
    }
};

class IGDXParticleRenderer
{
public:
    virtual ~IGDXParticleRenderer() = default;

    virtual void Render(const ParticleRenderSubmission& submission) = 0;

    virtual void Shutdown() = 0;
};
