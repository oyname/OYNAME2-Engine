#pragma once

#include "Particles/GDXParticleTypes.h"
#include "Particles/IGDXParticleRenderer.h"
#include "ParticleCommandList.h"
#include "Core/GDXMath.h"

class IParticleSystem
{
public:
    virtual ~IParticleSystem() = default;

    virtual void StartEmitter(GDXParticleEmitterComponent& em) = 0;
    virtual void StopEmitter(GDXParticleEmitterComponent& em) = 0;
    virtual void PauseEmitter(GDXParticleEmitterComponent& em) = 0;
    virtual void ResumeEmitter(GDXParticleEmitterComponent& em) = 0;

    virtual bool IsEmitterPaused(const GDXParticleEmitterComponent& em) const = 0;
    virtual bool IsEmitterPlaying(const GDXParticleEmitterComponent& em) const = 0;
    virtual bool IsEmitterFinished(const GDXParticleEmitterComponent& em) const = 0;

    virtual void SubmitEmitter(GDXParticleEmitterComponent& em,
                               const Matrix4& worldMatrix,
                               float worldScale = 1.0f) = 0;

    virtual void Update(float deltaMs) = 0;

    virtual void BuildRenderSubmission(const ParticleRenderContext& ctx,
                                       ParticleCommandList& outCommandList) const = 0;

    virtual int GetParticleCount() const = 0;
};
