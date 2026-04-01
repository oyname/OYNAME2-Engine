#pragma once

#include "ECS/Registry.h"
#include "CameraSystem.h"
#include "Particles/GDXParticleSystem.h"
#include "Components.h"
#include "Core/GDXMathOps.h"
#include <algorithm>
#include <cmath>

class GDXParticleEmitterSystem
{
public:
    inline void Update(Registry& registry,
        CameraSystem& cameraSystem,
        GDXParticleSystem& runtime,
        float deltaSeconds,
        Matrix4* outViewProj = nullptr)
    {
        FrameData cameraFrame{};
        const bool hasCamera = cameraSystem.Update(registry, cameraFrame);

        registry.View<GDXParticleEmitterComponent, ParticleEmitterStateComponent, WorldTransformComponent>(
            [&](EntityID id, GDXParticleEmitterComponent& emitter, ParticleEmitterStateComponent& state, WorldTransformComponent& wt)
            {
                ParticleEmitterControlComponent* control = registry.Get<ParticleEmitterControlComponent>(id);

                if (!state.initialized)
                {
                    state.initialized = true;
                    if (control && control->playOnStart)
                        control->startRequested = true;
                }

                if (control)
                {
                    if (control->restartRequested)
                    {
                        runtime.StartEmitter(emitter);
                        state.runtimeActive = true;
                        control->requestedActive = true;
                        control->paused = false;
                        control->restartRequested = false;
                        control->startRequested = false;
                        control->stopRequested = false;
                        control->pauseRequested = false;
                        control->resumeRequested = false;
                    }
                    else
                    {
                        if (control->startRequested)
                        {
                            runtime.StartEmitter(emitter);
                            state.runtimeActive = true;
                            control->requestedActive = true;
                            control->paused = false;
                            control->startRequested = false;
                        }
                        if (control->pauseRequested)
                        {
                            runtime.PauseEmitter(emitter);
                            state.runtimeActive = false;
                            control->requestedActive = true;
                            control->paused = true;
                            control->pauseRequested = false;
                            control->resumeRequested = false;
                        }
                        if (control->resumeRequested)
                        {
                            runtime.ResumeEmitter(emitter);
                            state.runtimeActive = emitter.active;
                            control->requestedActive = true;
                            control->paused = false;
                            control->resumeRequested = false;
                            control->pauseRequested = false;
                        }
                        if (control->stopRequested)
                        {
                            runtime.StopEmitter(emitter);
                            state.runtimeActive = false;
                            control->requestedActive = false;
                            control->paused = false;
                            control->stopRequested = false;
                            control->pauseRequested = false;
                            control->resumeRequested = false;
                        }
                    }

                    if (control->paused)
                    {
                        if (state.runtimeActive || emitter.active)
                        {
                            runtime.PauseEmitter(emitter);
                            state.runtimeActive = false;
                        }
                    }
                    else if (control->requestedActive && !state.runtimeActive)
                    {
                        if (emitter.paused)
                            runtime.ResumeEmitter(emitter);
                        else
                            runtime.StartEmitter(emitter);
                        state.runtimeActive = true;
                    }
                    else if (!control->requestedActive && (state.runtimeActive || emitter.active || emitter.paused))
                    {
                        runtime.StopEmitter(emitter);
                        state.runtimeActive = false;
                    }
                }
                else if (emitter.active && !state.runtimeActive)
                {
                    runtime.StartEmitter(emitter);
                    state.runtimeActive = true;
                }

                if (state.runtimeActive || emitter.active)
                {
                    Float3 pos = {};
                    Float4 rot = GDX::QuaternionIdentity();
                    Float3 scl = { 1.0f, 1.0f, 1.0f };
                    if (!GDX::DecomposeTRS(wt.matrix, pos, rot, scl))
                        scl = { 1.0f, 1.0f, 1.0f };

                    const float uniformScale = (std::max)((std::max)(std::fabs(scl.x), std::fabs(scl.y)), std::fabs(scl.z));
                    runtime.SubmitEmitter(emitter, wt.matrix, emitter.scale * ((uniformScale > 0.0001f) ? uniformScale : 1.0f));
                }

                if (state.runtimeActive && !emitter.active && !emitter.paused)
                {
                    state.runtimeActive = false;
                    if (control && control->oneShot)
                        control->requestedActive = false;
                }
            });

        if (hasCamera)
        {
            runtime.Update(deltaSeconds * 1000.0f, cameraFrame.viewMatrix);
            if (outViewProj)
                *outViewProj = cameraFrame.viewProjMatrix;
        }
    }
};
