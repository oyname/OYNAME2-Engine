#pragma once
#include <cstdint>

// ---------------------------------------------------------------------------
// ShaderSourceType — backend-neutral, shared zwischen ShaderSourceDesc
// und PostProcessPassDesc.
// ---------------------------------------------------------------------------
enum class ShaderSourceType : uint8_t
{
    HlslFilePath  = 0,   // Dateipfad (UTF-16)
    HlslSource    = 1,   // HLSL Quelltext (UTF-8)
    SpirvBinary   = 2,   // SPIR-V Binärdaten
    GlslSource    = 3,   // GLSL Quelltext (UTF-8)
};
