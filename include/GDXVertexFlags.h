#pragma once
#include <cstdint>

// ---------------------------------------------------------------------------
// GDXVertexFlags — Bitfeld für Vertex-Stream-Konfiguration.
//
// Übernimmt das Konzept der OYNAME D3DVERTEX_FLAGS direkt:
//   - FillBuffer / Upload liest die Flags → erstellt nur die nötigen Streams.
//   - InputLayout-Bau liest die Flags → erzeugt passende D3D11_INPUT_ELEMENT_DESC[].
//   - Draw / ExecuteQueue liest die Flags → bindet nur aktive Slots.
//
// Jeder Flag entspricht einem separaten Vertex-Buffer-Slot (multi-stream).
// Die Slot-Nummer entspricht der Reihenfolge der gesetzten Bits:
//   GDX_VERTEX_POSITION   → Slot 0
//   GDX_VERTEX_NORMAL     → Slot 1  (wenn POSITION auch gesetzt)
//   GDX_VERTEX_COLOR      → Slot 2  (wenn POSITION+NORMAL auch gesetzt)
//   usw.
// ---------------------------------------------------------------------------

enum GDXVertexFlags : uint32_t
{
    GDX_VERTEX_NONE         = 0u,
    GDX_VERTEX_POSITION     = (1u << 0),  // float3  POSITION0
    GDX_VERTEX_NORMAL       = (1u << 1),  // float3  NORMAL0
    GDX_VERTEX_COLOR        = (1u << 2),  // float4  COLOR0
    GDX_VERTEX_TEX1         = (1u << 3),  // float2  TEXCOORD0
    GDX_VERTEX_TEX2         = (1u << 4),  // float2  TEXCOORD1
    GDX_VERTEX_TANGENT      = (1u << 5),  // float4  TANGENT0   (xyz + Handedness)
    GDX_VERTEX_BONE_INDICES = (1u << 6),  // uint4   BLENDINDICES0
    GDX_VERTEX_BONE_WEIGHTS = (1u << 7),  // float4  BLENDWEIGHT0

    // Standard-Kombination für VertexShader.hlsl
    GDX_VERTEX_DEFAULT = GDX_VERTEX_POSITION | GDX_VERTEX_NORMAL | GDX_VERTEX_TEX1,

    // Skinning-Erweiterung des Standard-Shaders
    GDX_VERTEX_SKINNED = GDX_VERTEX_POSITION | GDX_VERTEX_NORMAL | GDX_VERTEX_TEX1
                       | GDX_VERTEX_BONE_INDICES | GDX_VERTEX_BONE_WEIGHTS,
};
