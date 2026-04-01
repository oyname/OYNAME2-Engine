#pragma once
// ============================================================
//  GDXParticleSystem.h  --  KROM Engine Particle System
//  Backend-neutral simulation. Billboard corners built in VS.
// ============================================================
#include "Particles/GDXParticleTypes.h"
#include "Core/GDXMath.h"
#include <vector>
#include <random>
#include <chrono>
#include <array>

class GDXParticleSystem
{
public:
    GDXParticleSystem();

    void Init(int texFramesX = 1, int texFramesY = 1);

    void SetGravity(float g)  { m_gravity = g; }
    void SetWind   (float w)  { m_wind    = w / 1000.0f; }

    int  RegisterType(const GDXParticleTypeDesc& desc);
    const GDXParticleTypeInternal& GetType(int id) const;

    void StartEmitter (GDXParticleEmitterComponent& em);
    void StopEmitter  (GDXParticleEmitterComponent& em);
    void PauseEmitter (GDXParticleEmitterComponent& em);
    void ResumeEmitter(GDXParticleEmitterComponent& em);

    bool IsEmitterPaused  (const GDXParticleEmitterComponent& em) const;
    bool IsEmitterPlaying (const GDXParticleEmitterComponent& em) const;
    bool IsEmitterFinished(const GDXParticleEmitterComponent& em) const;

    void SubmitEmitter(GDXParticleEmitterComponent& em,
                       const Matrix4& worldMatrix,
                       float worldScale = 1.0f);

    // viewMat = camera VIEW matrix (GDX::MatrixLookAtLH output).
    // Extracts camRight/camUp (stored for renderer) and runs simulation.
    void Update(float deltaMs, const Matrix4& viewMat);

    // GPU instances — one per live particle, rebuilt every frame.
    // blendMode 0 = alpha,  1 = additive.
    const std::vector<ParticleInstance>& GetInstances(int blendMode) const
    {
        return m_instances[blendMode];
    }

    // Camera basis extracted from viewMat in Update() — needed by renderer cbuffer.
    Float3 GetCamRight() const { return m_camRight; }
    Float3 GetCamUp   () const { return m_camUp;    }

    int GetParticleCount () const { return (int)m_particles.size(); }
    int GetRenderedCount () const { return m_renderedCount; }

private:
    struct EmitterSubmission
    {
        GDXParticleEmitterComponent* em = nullptr;
        Matrix4 worldMatrix = Matrix4::Identity();
        float worldScale = 1.0f;
    };

    void SpawnFromSlot(const EmitterSubmission& sub,
                       GDXParticleEmitterSlot&  slot,
                       int& emitterSpawnedThisFrame,
                       float deltaMs, int nowMs);

    GDXParticle MakeParticle(int typeID,
                              const Matrix4& emWorld,
                              float          emScale,
                              int            nowMs);

    bool SpawnTrail(const GDXParticle& parent, int typeID, int nowMs);
    bool SpawnBounceChildren(const GDXParticle& parent,
                             const GDXParticleTypeInternal& parentType,
                             float impactSpeed,
                             int nowMs);
    void SimulateParticleBasic(GDXParticle& p, float deltaMs);
    void SimulateParticleSpecialized(GDXParticle& p, float deltaMs, int nowMs);
    bool IsParticleDead  (const GDXParticle& p, int nowMs) const;

    int  GetTypeMaxAliveBudget(int typeID) const;
    int  GetTypeSpawnBudgetPerFrame(int typeID) const;
    int  GetTypeTrailSpawnBudgetPerFrame(int typeID) const;
    bool IsSpecializedType(int typeID) const;

    // Builds one ParticleInstance from a simulated particle.
    void BuildInstance(const GDXParticle& p, int blendMode, int nowMs);

    static uint32_t PackRGBA8(float r255, float g255, float b255, float a01);

    float RndF(float lo, float hi);
    int   RndI(int   lo, int   hi);
    int   NowMs() const;

    std::mt19937 m_rng;
    using Clock = std::chrono::steady_clock;
    Clock::time_point m_startTime;

    int   m_texFramesX = 1;
    float m_uvUnit     = 1.0f;
    int   m_typeCount  = 0;

    std::array<GDXParticleTypeInternal, GDX_MAX_PARTICLE_TYPES + 1> m_types = {};

    float m_gravity = 0.00001f;
    float m_wind    = 0.0f;

    std::vector<EmitterSubmission> m_submissions;
    std::vector<GDXParticle>       m_particles;
    std::vector<GDXParticle>       m_spawnQueue;

    std::vector<ParticleInstance>  m_instances[2];

    std::array<int, GDX_MAX_PARTICLE_TYPES + 1> m_aliveCountByType        = {};
    std::array<int, GDX_MAX_PARTICLE_TYPES + 1> m_spawnedThisFrameByType   = {};
    std::array<int, GDX_MAX_PARTICLE_TYPES + 1> m_trailSpawnedThisFrameByType = {};
    std::array<int, GDX_MAX_PARTICLE_TYPES + 1> m_bounceSpawnedThisFrameByType = {};

    int m_trailSpawnBudgetPerFrame = 2048;
    int m_trailSpawnCountThisFrame = 0;
    int m_maxSpawnQueueSize        = 8192;
    int m_maxSpawnPerFrameTotal    = 4096;
    int m_spawnedThisFrameTotal    = 0;
    int m_bounceSpawnCountThisFrame= 0;


    // Camera basis stored after each Update() for the renderer cbuffer.
    Float3 m_camRight = { 1.0f, 0.0f, 0.0f };
    Float3 m_camUp    = { 0.0f, 1.0f, 0.0f };

    int m_renderedCount = 0;
};
