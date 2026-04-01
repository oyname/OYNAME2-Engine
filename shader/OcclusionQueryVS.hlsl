// OcclusionQueryVS.hlsl — AABB-Box fuer Occlusion Queries (Depth Only).
// Kein Vertex-Buffer noetig — SV_VertexID expandiert AABB-Eckpunkte.

cbuffer OcclusionBox : register(b0)
{
    float3 gBoxMin;
    float  _pad0;
    float3 gBoxMax;
    float  _pad1;
    row_major float4x4 gViewProj;
};

static const uint kBoxIndices[36] =
{
    0,1,2, 2,1,3,
    4,6,5, 5,6,7,
    0,4,1, 1,4,5,
    2,3,6, 6,3,7,
    0,2,4, 4,2,6,
    1,5,3, 3,5,7
};

float3 BoxCorner(uint i)
{
    return float3(
        (i & 1u) ? gBoxMax.x : gBoxMin.x,
        (i & 2u) ? gBoxMax.y : gBoxMin.y,
        (i & 4u) ? gBoxMax.z : gBoxMin.z
    );
}

float4 main(uint vid : SV_VertexID) : SV_POSITION
{
    return mul(float4(BoxCorner(kBoxIndices[vid]), 1.0f), gViewProj);
}
