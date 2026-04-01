// Common.hlsli — geteilte Konstanten und Hilfsfunktionen.
#ifndef COMMON_HLSLI
#define COMMON_HLSLI

static const float PI = 3.14159265359f;

// Material flags — müssen mit MaterialFlags in MaterialResource.h übereinstimmen.
#define MF_ALPHA_TEST      (1u << 0)
#define MF_DOUBLE_SIDED    (1u << 1)
#define MF_UNLIT           (1u << 2)
#define MF_USE_NORMAL_MAP  (1u << 3)
#define MF_USE_ORM_MAP     (1u << 4)
#define MF_USE_EMISSIVE    (1u << 5)
// MF_TRANSPARENT (1u << 6) ist ein reines Renderer-Sortier-Flag, kein Shader-Flag.
#define MF_SHADING_PBR     (1u << 10)
#define MF_USE_DETAIL_MAP  (1u << 11)
#define MF_RECEIVE_SHADOWS (1u << 12)

#define ROUGHNESS_MIN 0.04f

#endif // COMMON_HLSLI
