#pragma once
// ============================================================
//  IGDXParticleRenderer.h  --  backend-neutral particle renderer interface
// ============================================================
#include "Particles/GDXParticleSystem.h"
#include "Core/GDXMath.h"

// Camera basis + viewProj — passed to renderer each frame.
struct ParticleRenderContext
{
    Matrix4 viewProj;
    Float3  camRight;
    Float3  camUp;
};

class IGDXParticleRenderer
{
public:
    virtual ~IGDXParticleRenderer() = default;

    // Upload instance data to GPU + issue DrawIndexedInstanced calls.
    // Must be called AFTER GDXParticleSystem::Update() each frame.
    virtual void Render(const GDXParticleSystem&    system,
                        const ParticleRenderContext& ctx) = 0;

    virtual void Shutdown() = 0;
};
