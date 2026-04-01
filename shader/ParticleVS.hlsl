// ============================================================
//  ParticleVS.hlsl  --  GPU-instanced particle billboard
//
//  Slot b0: ParticleConstants (not shared with FrameConstants b1)
//
//  Input layout:
//    Slot 0 (per-vertex):   float2 corner  — unit quad offset (-1..+1)
//    Slot 1 (per-instance): ParticleInstance fields
//
//  flags:
//    0 = camera-facing (rot applied)
//    1 = align-to-move (velDir = billboard up, rot = alignOffsetDeg)
//    2 = horizontal (flat on XZ plane)
// ============================================================

static const float PI = 3.14159265f;

cbuffer ParticleConstants : register(b0)
{
    row_major float4x4 gViewProj;
    float3 gCamRight;
    float _pad0;
    float3 gCamUp;
    float _pad1;
};

struct VSVert
{
    float2 corner : TEXCOORD0; // (-1,-1)..(+1,+1) quad corner
};

struct VSInst
{
    float3 pos : POSITION1;
    float size : TEXCOORD1;
    float3 velDir : TEXCOORD2;
    float rot : TEXCOORD3;
    float2 uv : TEXCOORD4; // atlas UV top-left (u,v)
    float uvUnit : TEXCOORD5;
    uint flags : TEXCOORD6;
    float4 color : COLOR1;
    float pivotOffset : TEXCOORD7; // -1=tail at pos, 0=centre, +1=tip at pos
};

struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR0;
};

VSOut VSMain(VSVert vert, VSInst inst)
{
    float3 right, up;

    if (inst.flags == 2u)
    {
        // Horizontal: flat on XZ plane
        right = float3(1, 0, 0);
        up = float3(0, 0, 1);
    }
    else if (inst.flags == 1u)
    {
        // Align-to-move: texture U-axis (corner.x → right) follows velDir.
        // right = velDir  → horizontal atlas frame elongates along flight direction
        // up    = perp    → thin perpendicular extent (screen-facing)
        float3 velNorm = length(inst.velDir) > 0.0001f ? normalize(inst.velDir) : gCamUp;
        float3 camFwd = normalize(cross(gCamRight, gCamUp));

        // Tiefenkomponente aus Velocity entfernen → right liegt immer in der Bildschirmebene
        float3 velScreen = velNorm - dot(velNorm, camFwd) * camFwd;
        right = normalize(length(velScreen) > 0.001f ? velScreen : gCamRight);
        up = normalize(cross(camFwd, right)); // cross(camFwd, right) — nicht umgekehrt

        // alignOffsetDeg: rotate right/up around camFwd
        if (abs(inst.rot) > 0.0001f)
        {
            float s, c;
            sincos(inst.rot * PI / 180.0f, s, c);
            float3 r2 = right * c - up * s;
            float3 u2 = right * s + up * c;
            right = r2;
            up = u2;
        }
    }
    else
    {
        // Camera-facing + optional z-rotation
        float s, c;
        sincos(inst.rot * PI / 180.0f, s, c);
        right = gCamRight * c - gCamUp * s;
        up = gCamRight * s + gCamUp * c;
    }

    // Pivot offset: shift the quad centre along the primary (right) axis.
    //  0.0 = centre at inst.pos (default)
    // -1.0 = tail at inst.pos, quad extends in +right direction
    // +1.0 = tip  at inst.pos, quad extends in -right direction
    float3 pivot = inst.pos + right * (inst.pivotOffset * inst.size);

    float3 worldPos = pivot
        + right * (vert.corner.x * inst.size)
        + up * (vert.corner.y * inst.size);

    // UV: corner (-1..+1) → (0..1) → scaled to atlas frame
    float2 uvNorm = vert.corner * float2(0.5f, -0.5f) + 0.5f;
    float2 uv = float2(inst.uv.x, inst.uv.y) + uvNorm * inst.uvUnit;

    VSOut vout;
    vout.PosH = mul(float4(worldPos, 1.0f), gViewProj);
    vout.UV = uv;
    vout.Color = inst.color;
    return vout;
}


