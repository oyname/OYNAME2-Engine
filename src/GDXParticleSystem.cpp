// ============================================================
//  GDXParticleSystem.cpp
//  Port of "Particle Candy 1.4.5" (Blitz3D / Mike Dogan)
// ============================================================
#include "Particles/GDXParticleSystem.h"
#include "Core/GDXMathOps.h"   // GDX::Add, Subtract, Multiply, ...
#include <cmath>
#include <algorithm>
#include <cassert>
#include <climits>

#undef min
#undef max

// ============================================================
static Float3 ViewRight(const Matrix4& v)
{
    return GDX::Normalize3({ v._11, v._21, v._31 });
}
static Float3 ViewUp(const Matrix4& v)
{
    return GDX::Normalize3({ v._12, v._22, v._32 });
}

// ============================================================
GDXParticleSystem::GDXParticleSystem()
    : m_rng(std::random_device{}())
    , m_startTime(Clock::now())
{
}

void GDXParticleSystem::Init(int texFramesX, int /*texFramesY*/)
{
    m_texFramesX = (std::max)(1, texFramesX);
    m_uvUnit = 1.0f / (float)m_texFramesX;

    m_particles.reserve(4096);
    m_spawnQueue.reserve(512);
    m_startTime = Clock::now();
}

int GDXParticleSystem::NowMs() const
{
    auto e = Clock::now() - m_startTime;
    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(e).count();
}

// ============================================================
//  Type Registration
// ============================================================
int GDXParticleSystem::RegisterType(const GDXParticleTypeDesc& d)
{
    assert(m_typeCount < GDX_MAX_PARTICLE_TYPES);
    ++m_typeCount;
    GDXParticleTypeInternal& t = m_types[m_typeCount];

    // atlas UV
    int frm = (std::max)(1, d.texFrame);
    int row = 1;
    while (frm > m_texFramesX) { frm -= m_texFramesX; ++row; }
    t.u = (frm - 1) * m_uvUnit;
    t.v = (row - 1) * m_uvUnit;
    t.uvUnit = m_uvUnit;

    t.spd = d.speed / 1000.0f;
    t.spv = d.speedVar / 1000.0f;
    t.srx = d.randSpeedX / 1000.0f;
    t.sry = d.randSpeedY / 1000.0f;
    t.srz = d.randSpeedZ / 1000.0f;

    t.sze = d.size;
    t.szv = d.sizeVar;
    t.szc = d.sizeChange / 1000.0f;
    t.szm = d.sizeMax;

    t.alp = d.alpha;
    t.alv = d.alphaVar;
    t.alc = d.alphaChange / 1000.0f;

    t.wgt = d.weight / 1000.0f;
    t.wgv = d.weightVar / 1000.0f;

    t.rom = d.rotMode;
    t.hor = d.alignHoriz;
    t.roc = (d.rotMode == GDXRotationMode::Random) ? d.rotChange / 1000.0f : d.rotChange;
    t.aof = d.alignOffsetDeg;
    t.pvo = d.pivotOffset;

    t.ian = d.innerAngle;
    t.emr = d.outerAngle;

    t.ox1 = d.ox1; t.ox2 = d.ox2;
    t.oy1 = d.oy1; t.oy2 = d.oy2;
    t.oz1 = d.oz1; t.oz2 = d.oz2;

    t.r = d.r; t.g = d.g; t.b = d.b;
    t.bv = d.brightnessVar;
    t.rc = d.rChange / 1000.0f;
    t.gc = d.gChange / 1000.0f;
    t.bc = d.bChange / 1000.0f;

    t.lft = d.lifeTime;
    t.msh = (d.blendMode == 1) ? 1 : 0;
    t.pls = d.pulsation;
    t.bnc = d.bounceFactor;   // stored as 'bounce energy' factor
    t.bnm = d.maxBounces;
    t.flh = d.floorHeight;
    t.sob = d.spawnOnBounce;
    t.bst = d.bounceSpawnTypeID;
    t.bsc = d.bounceSpawnCount;
    t.bsv = d.bounceSpawnCountVar;
    t.bss = d.bounceSpawnSpeedScale;
    t.bsi = d.bounceSpawnMinImpactSpeed / 1000.0f;
    t.bsb = d.maxBounceSpawnPerFrame;
    t.bsm = d.maxBounceSpawnCount;
    t.trl = d.trailTypeID;
    t.tst = d.trailStartMs;
    t.tlt = d.trailLifeMs;
    t.ter = d.trailRatePerSec / 1000.0f;
    t.cms = d.circularSpeed / 1000.0f;
    t.maxAlive = d.maxAlive;
    t.maxSpawnPerFrame = d.maxSpawnPerFrame;
    t.maxTrailSpawnPerFrame = d.maxTrailSpawnPerFrame;
    t.esh = d.emissionShape;

    t.featureFlags = GDXParticleFeature_None;
    if (t.srx != 0.0f || t.sry != 0.0f || t.srz != 0.0f) t.featureFlags |= GDXParticleFeature_RandomImpulse;
    if (t.wgv != 0.0f)                                   t.featureFlags |= GDXParticleFeature_GravityVar;
    if (t.alc != 0.0f)                                   t.featureFlags |= GDXParticleFeature_AlphaDrift;
    if (t.rc != 0.0f || t.gc != 0.0f || t.bc != 0.0f)   t.featureFlags |= GDXParticleFeature_ColorDrift;
    if (t.szc != 0.0f)                                   t.featureFlags |= GDXParticleFeature_SizeDrift;
    if (t.rom == GDXRotationMode::Random && t.roc != 0.0f)
        t.featureFlags |= GDXParticleFeature_RandomSpin;
    if (t.bnm > 0 || t.sob)                             t.featureFlags |= GDXParticleFeature_Bounce;
    if (t.cms != 0.0f)                                   t.featureFlags |= GDXParticleFeature_Circular;
    if (t.trl > 0)                                       t.featureFlags |= GDXParticleFeature_Trail;
    if (t.pls > 0.0f)                                    t.featureFlags |= GDXParticleFeature_Pulsation;
    if (t.hor || t.rom == GDXRotationMode::AlignToMove || t.pls > 0.0f)
        t.featureFlags |= GDXParticleFeature_SpecialRender;

    return m_typeCount;
}

const GDXParticleTypeInternal& GDXParticleSystem::GetType(int id) const
{
    assert(id >= 1 && id <= m_typeCount);
    return m_types[id];
}

// ============================================================
//  Emitter control
// ============================================================
void GDXParticleSystem::StartEmitter(GDXParticleEmitterComponent& em)
{
    em.active = true;
    em.paused = false;
    em.elapsedMs = 0;
    em.startTime = NowMs();
    for (int i = 0; i < em.nSlots; ++i)
    {
        em.slots[i].ratePerMs = em.slots[i].rateOriginal;
        em.slots[i].accumulator = 0.0f;
    }
}

void GDXParticleSystem::StopEmitter(GDXParticleEmitterComponent& em)
{
    em.active = false;
    em.paused = false;
    em.elapsedMs = 0;
    for (int i = 0; i < em.nSlots; ++i)
    {
        em.slots[i].ratePerMs = em.slots[i].rateOriginal;
        em.slots[i].accumulator = 0.0f;
    }
}

void GDXParticleSystem::PauseEmitter(GDXParticleEmitterComponent& em)
{
    if (!em.active || em.paused)
        return;
    const int nowMs = NowMs();
    em.elapsedMs += (nowMs - em.startTime);
    if (em.elapsedMs > em.maxLife)
        em.elapsedMs = em.maxLife;
    em.active = false;
    em.paused = true;
}

void GDXParticleSystem::ResumeEmitter(GDXParticleEmitterComponent& em)
{
    if (!em.paused)
        return;
    em.startTime = NowMs();
    em.active = true;
    em.paused = false;
}

bool GDXParticleSystem::IsEmitterPaused(const GDXParticleEmitterComponent& em) const
{
    return em.paused;
}

bool GDXParticleSystem::IsEmitterPlaying(const GDXParticleEmitterComponent& em) const
{
    return em.active && !em.paused;
}

bool GDXParticleSystem::IsEmitterFinished(const GDXParticleEmitterComponent& em) const
{
    return !em.active && !em.paused && em.maxLife > 0 && em.elapsedMs >= em.maxLife;
}

void GDXParticleSystem::SubmitEmitter(GDXParticleEmitterComponent& em,
    const Matrix4& worldMatrix,
    float worldScale)
{
    if (!em.active) return;
    m_submissions.push_back({ &em, worldMatrix, worldScale });
}

// ============================================================
//  Per-frame Update
// ============================================================
void GDXParticleSystem::Update(float deltaMs)
{
    if (deltaMs > 500.0f) deltaMs = 500.0f;

    const int nowMs = NowMs();

    m_aliveCountByType.fill(0);
    for (const auto& p : m_particles)
    {
        if (p.type >= 1 && p.type <= m_typeCount)
            ++m_aliveCountByType[p.type];
    }
    m_spawnedThisFrameByType.fill(0);
    m_trailSpawnedThisFrameByType.fill(0);
    m_bounceSpawnedThisFrameByType.fill(0);
    m_spawnedThisFrameTotal = 0;
    m_bounceSpawnCountThisFrame = 0;

    // --- 1. Spawn from emitters ---
    for (auto& sub : m_submissions)
    {
        GDXParticleEmitterComponent& em = *sub.em;
        int elapsed = em.elapsedMs + (em.active ? (nowMs - em.startTime) : 0);
        int emitterSpawnedThisFrame = 0;

        if (elapsed > em.maxLife)
        {
            em.elapsedMs = em.maxLife;
            em.active = false;
            em.paused = false;
            continue;
        }
        em.elapsedMs = elapsed;
        em.startTime = nowMs;   // delta-Basis für nächstes Frame aktualisieren

        for (int i = 0; i < em.nSlots; ++i)
        {
            auto& slot = em.slots[i];
            if (!slot.enabled)            continue;
            if (elapsed < slot.startMs)   continue;
            if (elapsed > slot.endMs)     continue;

            SpawnFromSlot(sub, slot, emitterSpawnedThisFrame, deltaMs, nowMs);
            slot.ratePerMs += slot.rateChange * deltaMs;
        }
    }
    m_submissions.clear();

    // --- 2. Prepare transient simulation buffers ---
    const size_t particleCount = m_particles.size();
    const size_t desiredSpawnQueueCap = (std::max)(particleCount / 4u, size_t(128));
    if (m_spawnQueue.capacity() < desiredSpawnQueueCap)
        m_spawnQueue.reserve((std::min)(desiredSpawnQueueCap + 64u, size_t(m_maxSpawnQueueSize)));
    m_trailSpawnCountThisFrame = 0;

    // --- 3. Simulate particles ---
    m_spawnQueue.clear();
    size_t n = m_particles.size();
    for (size_t i = 0; i < n; )
    {
        GDXParticle& p = m_particles[i];
        const GDXParticleTypeInternal& type = m_types[p.type];
        if ((type.featureFlags & (GDXParticleFeature_RandomImpulse |
            GDXParticleFeature_GravityVar |
            GDXParticleFeature_AlphaDrift |
            GDXParticleFeature_ColorDrift |
            GDXParticleFeature_SizeDrift |
            GDXParticleFeature_RandomSpin |
            GDXParticleFeature_Bounce |
            GDXParticleFeature_Circular |
            GDXParticleFeature_Trail)) == 0u)
        {
            SimulateParticleBasic(p, deltaMs);
        }
        else
        {
            SimulateParticleSpecialized(p, deltaMs, nowMs);
        }

        if (IsParticleDead(p, nowMs))
        {
            m_particles[i] = m_particles[--n];
            m_particles.resize(n);
            continue;
        }
        ++i;
    }

    // --- 4. Append trail spawns ---
    for (auto& tp : m_spawnQueue) m_particles.push_back(tp);
    m_spawnQueue.clear();
}

bool GDXParticleSystem::IsSpecializedType(int typeID) const
{
    const uint32_t basicMask = GDXParticleFeature_RandomImpulse |
        GDXParticleFeature_GravityVar |
        GDXParticleFeature_AlphaDrift |
        GDXParticleFeature_ColorDrift |
        GDXParticleFeature_SizeDrift |
        GDXParticleFeature_RandomSpin |
        GDXParticleFeature_Bounce |
        GDXParticleFeature_Circular |
        GDXParticleFeature_Trail;
    return (m_types[typeID].featureFlags & basicMask) != 0u;
}

int GDXParticleSystem::GetTypeMaxAliveBudget(int typeID) const
{
    const GDXParticleTypeInternal& t = m_types[typeID];
    if (t.maxAlive > 0)
        return t.maxAlive;
    if ((t.featureFlags & GDXParticleFeature_Trail) != 0u || t.trl > 0)
        return 2048;
    if (IsSpecializedType(typeID))
        return 4096;
    return 16384;
}

int GDXParticleSystem::GetTypeSpawnBudgetPerFrame(int typeID) const
{
    const GDXParticleTypeInternal& t = m_types[typeID];
    if (t.maxSpawnPerFrame > 0)
        return t.maxSpawnPerFrame;
    if ((t.featureFlags & GDXParticleFeature_Trail) != 0u || t.trl > 0)
        return 256;
    if (IsSpecializedType(typeID))
        return 512;
    return 2048;
}

int GDXParticleSystem::GetTypeTrailSpawnBudgetPerFrame(int typeID) const
{
    const GDXParticleTypeInternal& t = m_types[typeID];
    if (t.maxTrailSpawnPerFrame > 0)
        return t.maxTrailSpawnPerFrame;
    if ((t.featureFlags & GDXParticleFeature_Trail) != 0u || t.trl > 0)
        return 128;
    if (IsSpecializedType(typeID))
        return 256;
    return 512;
}

// ============================================================
//  Spawn helpers
// ============================================================

// Build rotation matrix for a given Euler orientation (degrees).
// Order: Yaw(Y) * Pitch(X) * Roll(Z) — matches Blitz3D RotateEntity.
static Matrix4 EulerMatrix(float pitchDeg, float yawDeg, float rollDeg)
{
    Float4 q = GDX::QuaternionFromEulerDeg(pitchDeg, yawDeg, rollDeg);
    return GDX::MatrixRotationQuaternion(q);
}

void GDXParticleSystem::SpawnFromSlot(const EmitterSubmission& sub,
    GDXParticleEmitterSlot& slot,
    int& emitterSpawnedThisFrame,
    float deltaMs, int nowMs)
{
    slot.accumulator += deltaMs * slot.ratePerMs;

    const int typeID = slot.typeID;
    if (typeID < 1 || typeID > m_typeCount)
        return;

    int typeSpawnBudget = GetTypeSpawnBudgetPerFrame(typeID);
    const int typeAliveBudget = GetTypeMaxAliveBudget(typeID);
    const GDXParticleEmitterComponent& em = *sub.em;
    const int emitterSpawnBudget = (em.maxSpawnPerFrame > 0) ? em.maxSpawnPerFrame : INT_MAX;
    if (slot.maxSpawnPerFrameOverride > 0)
        typeSpawnBudget = (std::min)(typeSpawnBudget, slot.maxSpawnPerFrameOverride);

    while (slot.accumulator >= 1.0f)
    {
        if (m_spawnedThisFrameTotal >= m_maxSpawnPerFrameTotal)
            break;
        if (emitterSpawnedThisFrame >= emitterSpawnBudget)
            break;
        if (m_spawnedThisFrameByType[typeID] >= typeSpawnBudget)
            break;
        if (m_aliveCountByType[typeID] >= typeAliveBudget)
            break;

        GDXParticle p = MakeParticle(typeID, sub.worldMatrix, sub.worldScale, nowMs);
        m_particles.push_back(p);
        ++m_spawnedThisFrameByType[typeID];
        ++m_spawnedThisFrameTotal;
        ++m_aliveCountByType[typeID];
        ++emitterSpawnedThisFrame;
        slot.accumulator -= 1.0f;
    }

    const int maxCarry = (std::max)(4 * typeSpawnBudget, 8);
    if (slot.accumulator > (float)maxCarry)
        slot.accumulator = (float)maxCarry;
}

static Matrix4 OrthonormalRotationFromDirection(const Float3& dirInput)
{
    const Float3 up = GDX::Normalize3(dirInput, { 0.0f, 1.0f, 0.0f });
    const Float3 fallback = (std::fabs(up.y) > 0.999f) ? Float3{ 1.0f, 0.0f, 0.0f } : Float3{ 0.0f, 1.0f, 0.0f };
    const Float3 right = GDX::Normalize3(GDX::Cross3(fallback, up), { 1.0f, 0.0f, 0.0f });
    const Float3 forward = GDX::Normalize3(GDX::Cross3(up, right), { 0.0f, 0.0f, 1.0f });
    return GDX::MatrixRotationQuaternion(GDX::QuaternionFromBasis(right, up, forward));
}

static Matrix4 ExtractEmitterRotation(const Matrix4& world)
{
    Float3 pos, scale;
    Float4 rot = GDX::QuaternionIdentity();
    if (GDX::DecomposeTRS(world, pos, rot, scale))
        return GDX::MatrixRotationQuaternion(rot);

    Matrix4 out = world;
    out._41 = 0.0f; out._42 = 0.0f; out._43 = 0.0f; out._44 = 1.0f;
    return out;
}

GDXParticle GDXParticleSystem::MakeParticle(int            typeID,
    const Matrix4& emWorld,
    float          emScale,
    int            nowMs)
{
    const GDXParticleTypeInternal& t = m_types[typeID];

    const Float3 emPos = GDX::GetMatrixTranslation(emWorld);
    Matrix4 Rem = ExtractEmitterRotation(emWorld);

    const float phi = RndF(0.0f, 2.0f * GDX::PI);
    const float innerRad = GDX::ToRadians((std::max)(0.0f, t.ian));
    const float outerRad = GDX::ToRadians((std::max)(t.ian, t.emr));
    const float cosInner = std::cos(innerRad);
    const float cosOuter = std::cos(outerRad);
    const float cosTheta = RndF(cosOuter, cosInner);
    const float sinTheta = std::sqrt((std::max)(0.0f, 1.0f - cosTheta * cosTheta));

    const Float3 localDir =
    {
        std::cos(phi) * sinTheta,
        cosTheta,
        std::sin(phi) * sinTheta
    };

    Matrix4 Rcone = OrthonormalRotationFromDirection(localDir);
    Matrix4 Rcombined = GDX::MatrixMultiply(Rcone, Rem);

    Float3 spawnPos = emPos;
    switch (t.esh)
    {
    case GDXEmissionShape::Area:
    {
        Float3 lo = { (t.ox1 + RndF(0.0f, t.ox2)) * emScale,
                       (t.oy1 + RndF(0.0f, t.oy2)) * emScale,
                       (t.oz1 + RndF(0.0f, t.oz2)) * emScale };
        Float3 wo = GDX::TransformVector(lo, Rcombined);
        spawnPos = GDX::Add(emPos, wo);
        break;
    }
    case GDXEmissionShape::Point:
        // no offset — spawnPos = emPos (cone determines direction only)
        break;

    case GDXEmissionShape::Ring:
    {
        Float3 lo = { (t.ox1 + t.ox2) * emScale,
                       (t.oy1 + RndF(0.0f, t.oy2)) * emScale,
                       (t.oz1 + t.oz2) * emScale };
        Float3 wo = GDX::TransformVector(lo, Rcombined);
        spawnPos = GDX::Add(emPos, wo);
        break;
    }
    case GDXEmissionShape::Line:
    {
        // line: random offset along emitter axes only, no cone
        Float3 lo = { (t.ox1 + RndF(0.0f, t.ox2)) * emScale,
                       (t.oy1 + RndF(0.0f, t.oy2)) * emScale,
                       (t.oz1 + RndF(0.0f, t.oz2)) * emScale };
        Float3 wo = GDX::TransformVector(lo, Rem);
        spawnPos = GDX::Add(emPos, wo);
        break;
    }
    }

    // --- Velocity: TFormVector(0, spd, 0, Emitter, 0) ---
    // Local direction is +Y; transformed by combined rotation gives world velocity.
    float spd = t.spd + RndF(-t.spv, t.spv);
    Float3 localVel = { 0.0f, spd * emScale, 0.0f };
    Float3 worldVel = GDX::TransformVector(localVel, Rcombined);

    // --- Fill particle ---
    GDXParticle p = {};
    p.type = typeID;
    p.pos = spawnPos;
    p.lastPos = spawnPos;
    p.vel = worldVel;
    p.pivotPos = emPos;
    p.emitterScale = emScale;
    p.randVel = { RndF(-t.srx, t.srx), RndF(-t.sry, t.sry), RndF(-t.srz, t.srz) };
    p.size = (t.sze + RndF(-t.szv, t.szv)) * emScale;
    p.alpha = t.alp + RndF(-t.alv, t.alv);
    float k = (float)RndI(-t.bv, t.bv);
    p.r = t.r + k;
    p.g = t.g + k;
    p.b = t.b + k;
    p.gravityRand = RndF(-t.wgv, t.wgv);
    p.pulsePhase = RndF(0.0f, 2.0f * GDX::PI);
    p.rotSign = (worldVel.x >= 0.0f) ? uint8_t(1) : uint8_t(0);
    p.startTimeMs = nowMs;

    if (t.rom == GDXRotationMode::Random)
        p.rot = RndF(0.0f, 359.0f);
    else if (t.rom == GDXRotationMode::Fixed)
        p.rot = t.roc;
    else
        p.rot = 0.0f;

    return p;
}

// ============================================================
//  Simulation
// ============================================================
void GDXParticleSystem::SimulateParticleBasic(GDXParticle& p, float deltaMs)
{
    const GDXParticleTypeInternal& t = m_types[p.type];

    p.lastPos = p.pos;

    p.pos.x += deltaMs * p.vel.x + deltaMs * m_wind;
    p.pos.y += deltaMs * p.vel.y;
    p.pos.z += deltaMs * p.vel.z;

    p.vel.y -= deltaMs * (p.emitterScale * t.wgt * m_gravity);
}

void GDXParticleSystem::SimulateParticleSpecialized(GDXParticle& p, float deltaMs, int nowMs)
{
    const GDXParticleTypeInternal& t = m_types[p.type];

    p.lastPos = p.pos;

    p.pos.x += deltaMs * p.vel.x + deltaMs * m_wind;
    p.pos.y += deltaMs * p.vel.y;
    p.pos.z += deltaMs * p.vel.z;

    p.vel.x += deltaMs * p.randVel.x;
    p.vel.y += deltaMs * p.randVel.y;
    p.vel.z += deltaMs * p.randVel.z;

    const float grav = (p.emitterScale * t.wgt + p.gravityRand) * m_gravity;
    p.vel.y -= deltaMs * grav;

    p.alpha += deltaMs * t.alc;
    p.r += deltaMs * t.rc;
    p.g += deltaMs * t.gc;
    p.b += deltaMs * t.bc;

    p.size += deltaMs * (t.szc * p.emitterScale);
    const float sizeMax = t.szm * p.emitterScale;
    if (p.size > sizeMax) p.size = sizeMax;

    if (t.rom == GDXRotationMode::Random)
    {
        p.rot += p.rotSign ? (deltaMs * t.roc) : (-deltaMs * t.roc);
    }

    if (t.bnm > 0 && p.pos.y < t.flh)
    {
        const float impactSpeed = -p.vel.y;
        ++p.bounceCount;
        p.pos.y = t.flh;
        p.vel.y = -(p.vel.y * t.bnc);

        if (t.sob &&
            t.bst > 0 && t.bsc > 0 &&
            p.bounceSpawnCount < t.bsm &&
            impactSpeed >= t.bsi)
        {
            if (SpawnBounceChildren(p, t, impactSpeed, nowMs))
                ++p.bounceSpawnCount;
        }
    }

    if (t.cms != 0.0f)
    {
        const float ang = t.cms * deltaMs * GDX::PI / 180.0f;
        const float ca = cosf(ang), sa = sinf(ang);
        const float dx = p.pos.x - p.pivotPos.x;
        const float dz = p.pos.z - p.pivotPos.z;
        p.pos.x = p.pivotPos.x + dx * ca - dz * sa;
        p.pos.z = p.pivotPos.z + dx * sa + dz * ca;
    }

    if (t.trl > 0)
    {
        const int age = nowMs - p.startTimeMs;
        if (age > t.tst && age < t.tlt)
        {
            p.trailAccum += deltaMs * t.ter;

            int allowedSpawns = m_trailSpawnBudgetPerFrame - m_trailSpawnCountThisFrame;
            while (p.trailAccum >= 1.0f && allowedSpawns > 0)
            {
                if (!SpawnTrail(p, t.trl, nowMs))
                    break;
                p.trailAccum -= 1.0f;
                --allowedSpawns;
            }

            // Clamp runaway accumulation so a temporary spike does not explode
            // into a giant catch-up burst several frames later.
            const float maxCarry = 4.0f;
            if (p.trailAccum > maxCarry)
                p.trailAccum = maxCarry;
        }
    }
}

bool GDXParticleSystem::IsParticleDead(const GDXParticle& p, int nowMs) const
{
    const GDXParticleTypeInternal& t = m_types[p.type];
    if (nowMs - p.startTimeMs > t.lft)          return true;
    if (p.size < 0.0f)                          return true;
    if (p.alpha < 0.0f)                          return true;
    if (t.bnm > 0 && p.bounceCount > t.bnm)     return true;
    if (p.r + p.g + p.b < 1.0f)                 return true;
    return false;
}

bool GDXParticleSystem::SpawnBounceChildren(const GDXParticle& parent,
    const GDXParticleTypeInternal& parentType,
    float impactSpeed,
    int nowMs)
{
    const int childTypeID = parentType.bst;
    if (childTypeID < 1 || childTypeID > m_typeCount)
        return false;
    if ((int)m_spawnQueue.size() >= m_maxSpawnQueueSize)
        return false;
    if (m_spawnedThisFrameTotal >= m_maxSpawnPerFrameTotal)
        return false;

    const GDXParticleTypeInternal& childType = m_types[childTypeID];
    const int bounceBudget = (parentType.bsb > 0) ? parentType.bsb : GetTypeTrailSpawnBudgetPerFrame(childTypeID);
    if (m_bounceSpawnedThisFrameByType[parent.type] >= bounceBudget)
        return false;
    if (m_aliveCountByType[childTypeID] >= GetTypeMaxAliveBudget(childTypeID))
        return false;
    if (m_spawnedThisFrameByType[childTypeID] >= GetTypeSpawnBudgetPerFrame(childTypeID))
        return false;

    int spawnCount = parentType.bsc;
    if (parentType.bsv > 0)
        spawnCount += RndI(-parentType.bsv, parentType.bsv);
    spawnCount = (std::max)(0, spawnCount);
    if (spawnCount <= 0)
        return false;

    const float parentSpeed = GDX::Length3(parent.vel);
    const float childSpeedBase = (std::max)(0.0f, parentSpeed * parentType.bss);

    int spawned = 0;
    for (int i = 0; i < spawnCount; ++i)
    {
        if ((int)m_spawnQueue.size() >= m_maxSpawnQueueSize)
            break;
        if (m_spawnedThisFrameTotal >= m_maxSpawnPerFrameTotal)
            break;
        if (m_aliveCountByType[childTypeID] >= GetTypeMaxAliveBudget(childTypeID))
            break;
        if (m_spawnedThisFrameByType[childTypeID] >= GetTypeSpawnBudgetPerFrame(childTypeID))
            break;

        GDXParticle child = {};
        child.type = childTypeID;
        child.pos = parent.pos;
        child.lastPos = parent.pos;
        child.pivotPos = parent.pos;
        child.emitterScale = parent.emitterScale;
        child.randVel = { RndF(-childType.srx, childType.srx), RndF(-childType.sry, childType.sry), RndF(-childType.srz, childType.srz) };
        child.size = (childType.sze + RndF(-childType.szv, childType.szv)) * parent.emitterScale;
        child.alpha = childType.alp + RndF(-childType.alv, childType.alv);
        const float k = (float)RndI(-childType.bv, childType.bv);
        child.r = childType.r + k;
        child.g = childType.g + k;
        child.b = childType.b + k;
        child.gravityRand = RndF(-childType.wgv, childType.wgv);
        child.pulsePhase = RndF(0.0f, 2.0f * GDX::PI);
        child.startTimeMs = nowMs;
        child.rotSign = 1u;

        const float phi = RndF(0.0f, 2.0f * GDX::PI);
        const float cosTheta = RndF(0.1f, 1.0f);
        const float sinTheta = std::sqrt((std::max)(0.0f, 1.0f - cosTheta * cosTheta));
        Float3 dir = { std::cos(phi) * sinTheta, cosTheta, std::sin(phi) * sinTheta };
        dir = GDX::Normalize3(dir, { 0.0f, 1.0f, 0.0f });

        const float childSpeed = childSpeedBase + childType.spd + RndF(-childType.spv, childType.spv);
        child.vel = GDX::Scale3(dir, childSpeed * parent.emitterScale);

        if (childType.rom == GDXRotationMode::Random)
            child.rot = RndF(0.0f, 359.0f);
        else if (childType.rom == GDXRotationMode::Fixed)
            child.rot = childType.roc;
        else
            child.rot = 0.0f;

        m_spawnQueue.push_back(child);
        ++m_bounceSpawnCountThisFrame;
        ++m_bounceSpawnedThisFrameByType[parent.type];
        ++m_spawnedThisFrameByType[childTypeID];
        ++m_spawnedThisFrameTotal;
        ++m_aliveCountByType[childTypeID];
        ++spawned;
    }

    return spawned > 0;
}

bool GDXParticleSystem::SpawnTrail(const GDXParticle& parent, int typeID, int nowMs)
{
    assert(typeID >= 1 && typeID <= m_typeCount);
    if ((int)m_spawnQueue.size() >= m_maxSpawnQueueSize)
        return false;
    if (m_spawnedThisFrameTotal >= m_maxSpawnPerFrameTotal)
        return false;
    if (m_aliveCountByType[typeID] >= GetTypeMaxAliveBudget(typeID))
        return false;
    if (m_trailSpawnedThisFrameByType[typeID] >= GetTypeTrailSpawnBudgetPerFrame(typeID))
        return false;
    if (m_spawnedThisFrameByType[typeID] >= GetTypeSpawnBudgetPerFrame(typeID))
        return false;

    const GDXParticleTypeInternal& t2 = m_types[typeID];

    GDXParticle trail = {};
    trail.type = typeID;
    trail.pos = parent.pos;
    trail.lastPos = parent.pos;
    trail.pivotPos = parent.pivotPos;
    trail.randVel = { RndF(-t2.srx, t2.srx), RndF(-t2.sry, t2.sry), RndF(-t2.srz, t2.srz) };
    trail.rot = RndF(0.0f, 359.0f);
    trail.emitterScale = parent.emitterScale;
    trail.size = (parent.size + RndF(-t2.szv, t2.szv)) * parent.emitterScale;
    trail.alpha = t2.alp + RndF(-t2.alv, t2.alv);
    float k = (float)RndI(-t2.bv, t2.bv);
    trail.r = t2.r + k;
    trail.g = t2.g + k;
    trail.b = t2.b + k;
    trail.gravityRand = RndF(-t2.wgv, t2.wgv);
    trail.pulsePhase = RndF(0.0f, 2.0f * GDX::PI);
    trail.rotSign = 1u;
    trail.startTimeMs = nowMs;
    m_spawnQueue.push_back(trail);
    ++m_trailSpawnCountThisFrame;
    ++m_trailSpawnedThisFrameByType[typeID];
    ++m_spawnedThisFrameByType[typeID];
    ++m_spawnedThisFrameTotal;
    ++m_aliveCountByType[typeID];
    return true;
}

// ============================================================
//  Instance builder  (replaces CPU-side billboard expansion)
//  Billboard corners are computed in ParticleVS.hlsl.
//  CPU only provides: centre pos, size, velDir, rot, atlas UV, flags, color.
// ============================================================
bool GDXParticleSystem::IsVisibleInView(const GDXParticle& p,
    const ParticleRenderContext& ctx,
    float size,
    float& outProjectedPixelRadius) const
{
    outProjectedPixelRadius = 0.0f;

    const Float4 clip = GDX::TransformFloat4({ p.pos.x, p.pos.y, p.pos.z, 1.0f }, ctx.viewProj);
    if (clip.w <= 0.0001f)
        return false;

    const float radius = (std::max)(size, 0.25f);
    const float margin = radius * 2.0f;
    const float xBound = std::fabs(clip.w) + margin;
    const float yBound = std::fabs(clip.w) + margin;
    const float zMin = -margin;
    const float zMax = clip.w + margin;

    if (!(clip.x >= -xBound && clip.x <= xBound &&
          clip.y >= -yBound && clip.y <= yBound &&
          clip.z >= zMin    && clip.z <= zMax))
    {
        return false;
    }

    const float invW = 1.0f / (std::max)(std::fabs(clip.w), 0.0001f);
    const float ndcRadiusX = std::fabs(ctx.projMatrix._11) * radius * invW;
    const float ndcRadiusY = std::fabs(ctx.projMatrix._22) * radius * invW;
    const float pixelRadiusX = ndcRadiusX * ctx.viewportWidth * 0.5f;
    const float pixelRadiusY = ndcRadiusY * ctx.viewportHeight * 0.5f;
    outProjectedPixelRadius = (std::max)(pixelRadiusX, pixelRadiusY);
    return true;
}

bool GDXParticleSystem::BuildInstance(const GDXParticle& p,
    const ParticleRenderContext& ctx,
    int blendMode,
    int nowMs,
    ParticleInstance& outInstance) const
{
    const GDXParticleTypeInternal& t = m_types[p.type];

    float sz = p.size * p.emitterScale;
    if (t.pls > 0.0f)
    {
        const float pulse = std::sinf((nowMs * 0.001f) + p.pulsePhase);
        sz += pulse * t.pls * p.emitterScale;
    }
    if (sz <= 0.0f || p.alpha <= (1.0f / 255.0f))
        return false;

    float projectedPixelRadius = 0.0f;
    if (!IsVisibleInView(p, ctx, sz, projectedPixelRadius))
        return false;

    const float effectiveAlpha = (std::max)(0.0f, p.alpha);
    const float pixelCullThreshold = (blendMode != 0) ? 0.30f : 0.35f;
    const float alphaCullThreshold = (blendMode != 0) ? 0.015f : 0.02f;
    if (projectedPixelRadius < pixelCullThreshold && effectiveAlpha <= alphaCullThreshold)
        return false;

    ParticleInstance inst = {};
    inst.pos = p.pos;
    inst.size = sz;
    inst.u = t.u;
    inst.v = t.v;
    inst.uvUnit = t.uvUnit;
    inst.color = PackRGBA8(p.r, p.g, p.b, p.alpha);

    if (t.hor)
    {
        inst.flags = 2u;
        inst.velDir = {};
        inst.rot = 0.0f;
    }
    else if (t.rom == GDXRotationMode::AlignToMove)
    {
        const float len = GDX::Length3(p.vel);
        inst.velDir = (len > 0.0001f)
            ? GDX::Normalize3(p.vel)
            : ctx.camUp;
        inst.flags = 1u;
        inst.rot = t.aof;
    }
    else
    {
        inst.velDir = {};
        inst.flags = 0u;
        inst.rot = p.rot;
    }

    inst.pivotOffset = t.pvo;
    outInstance = inst;
    return true;
}

void GDXParticleSystem::BuildRenderSubmission(const ParticleRenderContext& ctx,
    ParticleCommandList& outCommandList) const
{
    outCommandList.Clear();
    outCommandList.SetContext(ctx);

    if (m_particles.empty())
        return;

    std::vector<AlphaSortEntry> alphaEntries;
    alphaEntries.reserve(m_particles.size());
    outCommandList.Reserve(m_particles.size(), m_particles.size() / 2u + 16u);

    const int nowMs = NowMs();
    for (const GDXParticle& p : m_particles)
    {
        if (p.type < 1 || p.type > m_typeCount)
            continue;

        const GDXParticleTypeInternal& type = m_types[p.type];
        ParticleInstance inst{};
        if (!BuildInstance(p, ctx, type.msh, nowMs, inst))
            continue;

        if (type.msh != 0)
        {
            outCommandList.SubmitAdditive(inst);
            continue;
        }

        const Float3 toParticle = GDX::Subtract(p.pos, ctx.cameraPosition);
        const float sortKey = GDX::Dot3(toParticle, ctx.cameraForward);
        alphaEntries.push_back({ sortKey, inst });
    }

    if (alphaEntries.size() > 1u)
    {
        std::sort(alphaEntries.begin(), alphaEntries.end(),
            [](const AlphaSortEntry& a, const AlphaSortEntry& b)
            {
                return a.sortKey > b.sortKey;
            });
    }

    for (const AlphaSortEntry& entry : alphaEntries)
        outCommandList.SubmitAlpha(entry.instance);
}

uint32_t GDXParticleSystem::PackRGBA8(float r255, float g255, float b255, float a01)
{
    const auto clamp255 = [](float v) -> uint32_t
        {
            const float clamped = (std::max)(0.0f, (std::min)(255.0f, v));
            return static_cast<uint32_t>(clamped + 0.5f);
        };
    const auto clamp01 = [](float v) -> uint32_t
        {
            const float clamped = (std::max)(0.0f, (std::min)(1.0f, v));
            return static_cast<uint32_t>(clamped * 255.0f + 0.5f);
        };

    const uint32_t r = clamp255(r255);
    const uint32_t g = clamp255(g255);
    const uint32_t b = clamp255(b255);
    const uint32_t a = clamp01(a01);
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

// ============================================================
//  Random helpers
// ============================================================
float GDXParticleSystem::RndF(float lo, float hi)
{
    if (lo >= hi) return lo;
    return std::uniform_real_distribution<float>(lo, hi)(m_rng);
}
int GDXParticleSystem::RndI(int lo, int hi)
{
    if (lo >= hi) return lo;
    return std::uniform_int_distribution<int>(lo, hi)(m_rng);
}