#pragma once
// ============================================================
//  GDXParticleTypes.h  --  KROM Engine Particle System
//  Port of "Particle Candy 1.4.5" (Blitz3D / Mike Dogan)
// ============================================================
#include "Core/GDXMath.h"   // Float2, Float3, Float4, Matrix4
#include <array>
#include <cstdint>

// ---- limits ------------------------------------------------
static constexpr int GDX_MAX_PARTICLE_TYPES = 99;
static constexpr int GDX_MAX_EMITTER_SLOTS = 10;

// ---- enums -------------------------------------------------
enum class GDXEmissionShape : uint8_t
{
    Area = 0,
    Point = 1,
    Ring = 2,
    Line = 3
};

enum class GDXRotationMode : uint8_t
{
    None = 0,
    Fixed = 1,
    Random = 2,
    AlignToMove = 3
};

enum GDXParticleFeatureFlags : uint32_t
{
    GDXParticleFeature_None = 0u,
    GDXParticleFeature_RandomImpulse = 1u << 0,
    GDXParticleFeature_GravityVar = 1u << 1,
    GDXParticleFeature_AlphaDrift = 1u << 2,
    GDXParticleFeature_ColorDrift = 1u << 3,
    GDXParticleFeature_SizeDrift = 1u << 4,
    GDXParticleFeature_RandomSpin = 1u << 5,
    GDXParticleFeature_Bounce = 1u << 6,
    GDXParticleFeature_Circular = 1u << 7,
    GDXParticleFeature_Trail = 1u << 8,
    GDXParticleFeature_Pulsation = 1u << 9,
    GDXParticleFeature_SpecialRender = 1u << 10
};

// ---- User-facing descriptor --------------------------------
struct GDXParticleTypeDesc
{
    int   texFrame = 1;

    float speed = 10.0f;
    float speedVar = 1.0f;
    float randSpeedX = 0.0f;
    float randSpeedY = 0.0f;
    float randSpeedZ = 0.0f;

    float size = 1.0f;
    float sizeVar = 0.0f;
    float sizeChange = 0.0f;
    float sizeMax = 10.0f;

    float alpha = 1.0f;
    float alphaVar = 0.0f;
    float alphaChange = 0.0f;

    float weight = 1.0f;
    float weightVar = 0.0f;

    GDXRotationMode rotMode = GDXRotationMode::Fixed;
    bool            alignHoriz = false;
    float           rotChange = 0.0f;
    float           alignOffsetDeg = 0.0f;

    // Pivot offset along the billboard's primary axis (right for camera-facing,
    // velDir for AlignToMove). Shifts the spawn position relative to the quad:
    //   0.0  = pos is the centre  (default)
    //  -1.0  = pos is the tail    (quad extends forward)
    //  +1.0  = pos is the tip     (quad extends backward)
    float           pivotOffset = 0.0f;

    float innerAngle = 0.0f;
    float outerAngle = 15.0f;

    float ox1 = 0, ox2 = 0;
    float oy1 = 0, oy2 = 0;
    float oz1 = 0, oz2 = 0;

    float r = 100.0f;
    float g = 100.0f;
    float b = 100.0f;
    int   brightnessVar = 0;
    float rChange = 0.0f;
    float gChange = 0.0f;
    float bChange = 0.0f;

    int   lifeTime = 1000;
    int   blendMode = 0;

    float floorHeight = 0.0f;
    float bounceFactor = 0.0f;
    int   maxBounces = 0;

    // Optional bounce child-spawn event.
    // Triggered only on actual floor impact/bounce, never continuously while resting.
    bool  spawnOnBounce = false;
    int   bounceSpawnTypeID = 0;
    int   bounceSpawnCount = 0;
    int   bounceSpawnCountVar = 0;
    float bounceSpawnSpeedScale = 1.0f;
    float bounceSpawnMinImpactSpeed = 0.0f;
    int   maxBounceSpawnPerFrame = -1;
    int   maxBounceSpawnCount = 1;

    float pulsation = 0.0f;

    int   trailTypeID = 0;
    int   trailStartMs = 0;
    int   trailLifeMs = 1000;
    float trailRatePerSec = 15.0f;

    float circularSpeed = 0.0f;

    // Optional budgets (-1 = use engine default heuristic)
    int   maxAlive = -1;
    int   maxSpawnPerFrame = -1;
    int   maxTrailSpawnPerFrame = -1;

    GDXEmissionShape emissionShape = GDXEmissionShape::Area;
};

// ---- Internal compiled type --------------------------------
struct GDXParticleTypeInternal
{
    float u = 0, v = 0, uvUnit = 1;
    float spd = 0, spv = 0;
    float srx = 0, sry = 0, srz = 0;
    float sze = 1, szv = 0, szc = 0, szm = 10;
    float alp = 1, alv = 0, alc = 0;
    float wgt = 0, wgv = 0;
    GDXRotationMode rom = GDXRotationMode::Fixed;
    bool  hor = false;
    float roc = 0.0f;
    float aof = 0.0f;
    float pvo = 0.0f;  // pivot offset (-1..+1)
    float ian = 0.0f;
    float emr = 7.5f;
    float ox1 = 0, ox2 = 0, oy1 = 0, oy2 = 0, oz1 = 0, oz2 = 0;
    float r = 100, g = 100, b = 100;
    int   bv = 0;
    float rc = 0, gc = 0, bc = 0;
    int   lft = 1000;
    int   msh = 0;
    float pls = 0.0f;
    float flh = 0.0f;
    float bnc = 0.0f;
    int   bnm = 0;
    bool  sob = false;
    int   bst = 0;
    int   bsc = 0;
    int   bsv = 0;
    float bss = 1.0f;
    float bsi = 0.0f;
    int   bsb = -1;
    int   bsm = 1;
    int   trl = 0;
    int   tst = 0;
    int   tlt = 1000;
    float ter = 0.0f;
    float cms = 0.0f;
    int   maxAlive = -1;
    int   maxSpawnPerFrame = -1;
    int   maxTrailSpawnPerFrame = -1;
    GDXEmissionShape esh = GDXEmissionShape::Area;
    uint32_t featureFlags = GDXParticleFeature_None;
};

// ---- Live particle instance --------------------------------
struct GDXParticle
{
    Float3 pos = {};
    Float3 lastPos = {};
    Float3 vel = {};
    Float3 pivotPos = {};
    Float3 randVel = {};
    float  size = 1.0f;
    float  alpha = 1.0f;
    float  rot = 0.0f;
    float  r = 255, g = 255, b = 255;
    float  emitterScale = 1.0f;
    float  trailAccum = 0.0f;
    float  gravityRand = 0.0f;
    float  pulsePhase = 0.0f;
    uint8_t rotSign = 1u;
    int    type = 0;
    int    startTimeMs = 0;
    int    bounceCount = 0;
    int    bounceSpawnCount = 0;
};

// ---- GPU instance (replaces GDXParticleVertex) ------------
// One entry per live particle — billboard corners built in VS.
// Layout (56 bytes):
//   offset  0: pos          (float3)
//   offset 12: size         (float)    half-extent
//   offset 16: velDir       (float3)   normalised velocity (AlignToMove only)
//   offset 28: rot          (float)    degrees
//   offset 32: u,v          (float2)   atlas UV top-left
//   offset 40: uvUnit       (float)    atlas frame UV size
//   offset 44: flags        (uint32)   0=camFacing  1=alignToMove  2=horizontal
//   offset 48: color        (RGBA8 UNORM)
//   offset 52: pivotOffset  (float)    -1..+1 along primary axis
// total: 56 bytes
struct ParticleInstance
{
    Float3   pos;
    float    size;
    Float3   velDir;
    float    rot;
    float    u, v;
    float    uvUnit;
    uint32_t flags;
    uint32_t color;
    float    pivotOffset;
};
static_assert(sizeof(ParticleInstance) == 56, "ParticleInstance must be 56 bytes");

// ---- Emitter slot ------------------------------------------
struct GDXParticleEmitterSlot
{
    int   typeID = 0;
    bool  enabled = true;
    int   startMs = 0;
    int   endMs = 1000;
    float ratePerMs = 0.01f;
    float rateChange = 0.0f;
    float rateOriginal = 0.01f;
    float accumulator = 0.0f;
    int   maxSpawnPerFrameOverride = -1;
};

// ---- Emitter Component -------------------------------------
struct GDXParticleEmitterComponent
{
    bool  active = false;
    bool  paused = false;
    float scale = 1.0f;
    int   startTime = 0;
    int   elapsedMs = 0;
    int   maxLife = 0;
    int   maxSpawnPerFrame = -1;



    int nSlots = 0;
    std::array<GDXParticleEmitterSlot, GDX_MAX_EMITTER_SLOTS> slots = {};

    bool AddSlot(int typeID,
        int   startMs = 0,
        int   lifeMs = 1000,
        float ratePerSec = 10.0f,
        float rateChangeSec = 0.0f,
        int   maxSpawnPerFrameOverride = -1)
    {
        if (nSlots >= GDX_MAX_EMITTER_SLOTS) return false;
        auto& s = slots[nSlots];
        s.typeID = typeID;
        s.startMs = startMs;
        s.endMs = startMs + lifeMs;
        s.ratePerMs = ratePerSec / 1000.0f;
        s.rateOriginal = s.ratePerMs;
        s.rateChange = rateChangeSec / 1000.0f;
        s.enabled = true;
        s.accumulator = 0.0f;
        s.maxSpawnPerFrameOverride = maxSpawnPerFrameOverride;
        if (s.endMs > maxLife) maxLife = s.endMs;
        ++nSlots;
        return true;
    }

    void EnableSlot(int slot) { if (slot < nSlots) slots[slot].enabled = true; }
    void DisableSlot(int slot) { if (slot < nSlots) slots[slot].enabled = false; }
    bool IsSlotEnabled(int slot) const { return slot < nSlots && slots[slot].enabled; }
};

// ---- ECS runtime state -------------------------------------
struct ParticleEmitterStateComponent
{
    bool  initialized = false;
    bool  runtimeActive = false;
};

struct ParticleEmitterControlComponent
{
    bool playOnStart = true;
    bool requestedActive = true;
    bool oneShot = false;
    bool paused = false;
    bool startRequested = false;
    bool stopRequested = false;
    bool restartRequested = false;
    bool pauseRequested = false;
    bool resumeRequested = false;
};
