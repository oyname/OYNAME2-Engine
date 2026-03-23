// TileLightCullCS.hlsl — Forward+ Tile Light Culling
//
// One thread group = one 16x16 screen tile.
// 256 threads = up to 256 lights tested in parallel (one light per thread).
// Output: light index list + light grid (offset+count per tile) for the pixel shader.
//
// No depth prepass required — uses view-space sphere vs tile-frustum test.
// Directional lights are always included without testing.

#define TILE_SIZE          16
#define MAX_LIGHTS         256
#define MAX_LIGHTS_PER_TILE 128

// ---------------------------------------------------------------------------
// Light data (same layout as LightData in PixelShader.hlsl)
// ---------------------------------------------------------------------------
struct LightData
{
    float4 position;       // xyz=world pos, w: 0=directional, 1=point, 2=spot
    float4 direction;      // xyz=dir (world space), w=castShadows
    float4 diffuse;        // rgb=color*intensity, a=radius
    float  innerCosAngle;
    float  outerCosAngle;
    float  pad0;
    float  pad1;
};

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------
StructuredBuffer<LightData> gLights           : register(t0);

RWStructuredBuffer<uint>    gLightIndexList   : register(u0);  // flat list of light indices
RWStructuredBuffer<uint2>   gLightGrid        : register(u1);  // (offset, count) per tile
RWStructuredBuffer<uint>    gLightCounter     : register(u2);  // global atomic counter

cbuffer TileCullCB : register(b0)
{
    row_major float4x4 gView;       // world→view
    row_major float4x4 gProj;       // view→clip
    row_major float4x4 gProjInv;    // clip→view
    float2             gViewportSize;
    uint               gLightCount;
    uint               gTileCountX;
};

// ---------------------------------------------------------------------------
// Shared memory — per-tile light accumulation
// ---------------------------------------------------------------------------
groupshared uint gs_count;
groupshared uint gs_list[MAX_LIGHTS_PER_TILE];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Signed distance from point p to plane (n, d).  Positive = in front.
float PlaneDist(float4 plane, float3 p)
{
    return dot(plane.xyz, p) + plane.w;
}

// Build frustum plane that passes through the origin (camera = 0 in view space)
// and two view-space direction vectors a, b.  Normal points inward.
float4 MakeFrustumPlane(float3 a, float3 b)
{
    float3 n = normalize(cross(a, b));
    return float4(n, 0.0);   // d = -dot(n, origin) = 0
}

// Unproject a 2D screen position to a view-space direction at z=1.
float3 ScreenToViewDir(float2 screenPos, float2 vpSize, float4x4 projInv)
{
    float2 ndc = float2(
         screenPos.x / vpSize.x * 2.0 - 1.0,
        -screenPos.y / vpSize.y * 2.0 + 1.0);  // y flipped: screen y down, NDC y up
    float4 clip = float4(ndc, 0.0, 1.0);
    float4 view = mul(clip, projInv);
    return normalize(view.xyz / view.w);
}

// Test a point light sphere (view space center + radius) against 4 frustum planes.
// Returns true if the sphere may be visible in this tile.
bool SphereTileTest(float3 centerVS, float radius, float4 planes[4])
{
    [unroll]
    for (int i = 0; i < 4; ++i)
        if (PlaneDist(planes[i], centerVS) < -radius)
            return false;
    return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(
    uint3 groupID    : SV_GroupID,
    uint  groupIndex : SV_GroupIndex)
{
    // --- Initialize shared memory ---
    if (groupIndex == 0)
        gs_count = 0;
    GroupMemoryBarrierWithGroupSync();

    // --- Build tile frustum (4 side planes) in view space ---
    // Corner screen positions of this tile
    float2 tileMin = float2(groupID.xy)       * float(TILE_SIZE);
    float2 tileMax = float2(groupID.xy + 1u)  * float(TILE_SIZE);
    tileMax = min(tileMax, gViewportSize);

    // 4 corners → view-space directions from the camera (origin in view space)
    float3 dir[4];
    dir[0] = ScreenToViewDir(float2(tileMin.x, tileMax.y), gViewportSize, gProjInv); // BL
    dir[1] = ScreenToViewDir(float2(tileMax.x, tileMax.y), gViewportSize, gProjInv); // BR
    dir[2] = ScreenToViewDir(float2(tileMax.x, tileMin.y), gViewportSize, gProjInv); // TR
    dir[3] = ScreenToViewDir(float2(tileMin.x, tileMin.y), gViewportSize, gProjInv); // TL

    // Planes: each passes through origin and two adjacent corner rays.
    // Order: Left, Right, Bottom, Top — normals pointing inward.
    float4 planes[4];
    planes[0] = MakeFrustumPlane(dir[3], dir[0]);  // Left   (TL, BL)
    planes[1] = MakeFrustumPlane(dir[1], dir[2]);  // Right  (BR, TR)
    planes[2] = MakeFrustumPlane(dir[0], dir[1]);  // Bottom (BL, BR)
    planes[3] = MakeFrustumPlane(dir[2], dir[3]);  // Top    (TR, TL)

    // --- Test one light per thread ---
    uint lightIndex = groupIndex;  // each thread handles one light (256 threads, 256 max lights)

    if (lightIndex < gLightCount)
    {
        LightData light = gLights[lightIndex];

        float lightType = light.position.w;
        bool visible = false;

        if (lightType < 0.5)
        {
            // Directional light — always affects every tile
            visible = true;
        }
        else
        {
            // Point or Spot light — test sphere vs tile frustum in view space
            float4 posVS4 = mul(float4(light.position.xyz, 1.0), gView);
            float3 posVS  = posVS4.xyz / posVS4.w;
            float  radius = light.diffuse.w;   // a = radius

            // Only test lights in front of the camera (z > 0 in view space for LH)
            // Add radius as tolerance
            if (posVS.z + radius > 0.0)
                visible = SphereTileTest(posVS, radius, planes);
        }

        if (visible)
        {
            uint slot;
            InterlockedAdd(gs_count, 1u, slot);
            if (slot < MAX_LIGHTS_PER_TILE)
                gs_list[slot] = lightIndex;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // --- Thread 0 writes tile result to global buffers ---
    if (groupIndex == 0)
    {
        uint count = min(gs_count, MAX_LIGHTS_PER_TILE);
        uint offset;
        InterlockedAdd(gLightCounter[0], count, offset);

        uint tileIndex = groupID.y * gTileCountX + groupID.x;
        gLightGrid[tileIndex] = uint2(offset, count);

        for (uint i = 0; i < count; ++i)
            gLightIndexList[offset + i] = gs_list[i];
    }
}
