struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOut main(uint vertexID : SV_VertexID)
{
    VSOut o;

    float2 pos;
    if (vertexID == 0)
        pos = float2(-1.0, -1.0);
    else if (vertexID == 1)
        pos = float2(-1.0, 3.0);
    else
        pos = float2(3.0, -1.0);

    o.position = float4(pos, 0.0, 1.0);
    o.uv = pos * float2(0.5, -0.5) + 0.5;
    return o;
}
