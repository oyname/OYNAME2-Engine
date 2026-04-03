#pragma once
// ============================================================
//  GDXParticleSystem.h  --  KROM Engine Particle System
//  Backend-neutral simulation. Billboard corners built in VS.
// ============================================================
#include "Particles/IParticleSystem.h"
#include <vector>
#include <random>
#include <chrono>
#include <array>
#include <cstddef>

class GDXParticleSystem : public IParticleSystem
{
public:
    GDXParticleSystem();

    void Init(int texFramesX = 1, int texFramesY = 1);

    void SetGravity(float g)  { m_gravity = g; }
    void SetWind   (float w)  { m_wind    = w / 1000.0f; }

    int  RegisterType(const GDXParticleTypeDesc& desc);
    const GDXParticleTypeInternal& GetType(int id) const;

    void StartEmitter (GDXParticleEmitterComponent& em) override;
    void StopEmitter  (GDXParticleEmitterComponent& em) override;
    void PauseEmitter (GDXParticleEmitterComponent& em) override;
    void ResumeEmitter(GDXParticleEmitterComponent& em) override;

    bool IsEmitterPaused  (const GDXParticleEmitterComponent& em) const override;
    bool IsEmitterPlaying (const GDXParticleEmitterComponent& em) const override;
    bool IsEmitterFinished(const GDXParticleEmitterComponent& em) const override;

    void SubmitEmitter(GDXParticleEmitterComponent& em,
                       const Matrix4& worldMatrix,
                       float worldScale = 1.0f) override;

    // Global simulation step. No view-specific render preparation happens here.
    void Update(float deltaMs) override;

    // Per-view render preparation. Visibility, billboard alignment and
    // transparency sorting are derived from the supplied render context.
    void BuildRenderSubmission(const ParticleRenderContext& ctx,
                               ParticleCommandList& outCommandList) const override;

    int GetParticleCount () const override { return (int)m_particles.size(); }

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

    bool BuildInstance(const GDXParticle& p,
                       const ParticleRenderContext& ctx,
                       int blendMode,
                       int nowMs,
                       ParticleInstance& outInstance) const;

    bool IsVisibleInView(const GDXParticle& p,
                         const ParticleRenderContext& ctx,
                         float size,
                         float& outProjectedPixelRadius) const;

    static uint32_t PackRGBA8(float r255, float g255, float b255, float a01);

    struct AlphaSortEntry
    {
        float sortKey = 0.0f;
        ParticleInstance instance{};
    };

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


};
