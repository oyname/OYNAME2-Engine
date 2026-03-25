#pragma once

#include "Handle.h"

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Tone Mapping
// ---------------------------------------------------------------------------

enum class ToneMappingMode : int
{
    None     = -1,
    ACES     =  0,   // ACES Filmic (Narkowicz approximation) — default
    Reinhard =  1    // Reinhard extended luminance
};

// Must match cbuffer ToneMappingParams in PostProcessToneMappingPS.hlsl.
struct alignas(16) ToneMappingParams
{
    float exposure = 1.0f;   // linear pre-exposure multiplier
    float gamma    = 2.2f;   // output gamma
    int   mode     = 0;      // cast from ToneMappingMode (ACES=0, Reinhard=1)
    float pad      = 0.0f;
};
static_assert(sizeof(ToneMappingParams) == 16);

// Must match cbuffer FXAAParams in PostProcessFXAAPS.hlsl.
struct alignas(16) FXAAParams
{
    float texelW            = 1.0f / 1280.0f;
    float texelH            = 1.0f / 720.0f;
    float contrastThreshold = 0.0312f;  // lower = more AA, more cost
    float relativeThreshold = 0.125f;   // relative luma sensitivity
};
static_assert(sizeof(FXAAParams) == 16);

// ---------------------------------------------------------------------------
// Bloom
// Internal structs — must match the corresponding HLSL cbuffers exactly.
// Users interact with these only via GDXECSRenderer::SetBloom().
// ---------------------------------------------------------------------------

// PostProcessBloomBrightPS.hlsl — b0
struct alignas(16) BloomBrightParams
{
    float threshold = 1.0f;   // luma threshold for bright-extract
    float intensity = 2.0f;   // multiplier applied to the extracted bright regions
    float pad0      = 0.0f;
    float pad1      = 0.0f;
};
static_assert(sizeof(BloomBrightParams) == 16);

// PostProcessBloomBlurPS.hlsl — b0
struct alignas(16) BloomBlurParams
{
    float texelSizeX  = 1.0f / 1280.0f;
    float texelSizeY  = 1.0f / 720.0f;
    float directionX  = 1.0f;   // (1,0) = horizontal,  (0,1) = vertical
    float directionY  = 0.0f;
};
static_assert(sizeof(BloomBlurParams) == 16);

// PostProcessBloomCompositePS.hlsl — b0
struct alignas(16) BloomCompositeParams
{
    float bloomTint[4]   = { 1.0f, 1.0f, 1.0f, 1.0f };
    float bloomStrength  = 0.1f;
    float sceneStrength  = 1.0f;
    float pad0           = 0.0f;
    float pad1           = 0.0f;
};
static_assert(sizeof(BloomCompositeParams) == 32);


// ---------------------------------------------------------------------------
// Deklarative Input-Slots für backend-neutrales Post-Processing
// ---------------------------------------------------------------------------

enum class PostProcessInputSemantic : uint8_t
{
    SceneColor = 0,
    OriginalSceneColor = 1,
    SceneDepth = 2,
    SceneNormals = 3,
    Custom = 4,
};

struct PostProcessInputSlotDesc
{
    std::wstring             name;
    uint32_t                 shaderRegister = 0u;
    PostProcessInputSemantic semantic = PostProcessInputSemantic::SceneColor;
    bool                     required = true;
};

struct PostProcessInputSlot
{
    std::wstring             name;
    uint32_t                 shaderRegister = 0u;
    PostProcessInputSemantic semantic = PostProcessInputSemantic::SceneColor;
    bool                     required = true;
    TextureHandle            customTexture = TextureHandle::Invalid();
};

struct PostProcessExecutionInputs
{
    TextureHandle sceneColor         = TextureHandle::Invalid();
    TextureHandle originalSceneColor = TextureHandle::Invalid();
    TextureHandle sceneDepth         = TextureHandle::Invalid();
    TextureHandle sceneNormals       = TextureHandle::Invalid();

    float         cameraNearPlane    = 0.1f;
    float         cameraFarPlane     = 1000.0f;
    uint32_t      cameraIsOrtho      = 0u;
    uint32_t      depthDebugFlags    = 1u;

    void Reset()
    {
        sceneColor         = TextureHandle::Invalid();
        originalSceneColor = TextureHandle::Invalid();
        sceneDepth         = TextureHandle::Invalid();
        sceneNormals       = TextureHandle::Invalid();
        cameraNearPlane    = 0.1f;
        cameraFarPlane     = 1000.0f;
        cameraIsOrtho      = 0u;
        depthDebugFlags    = 1u;
    }
};

struct ResolvedPostProcessBinding
{
    std::wstring  name;
    uint32_t      shaderRegister = 0u;
    TextureHandle texture        = TextureHandle::Invalid();
    bool          required       = true;
};

inline std::vector<PostProcessInputSlot> BuildDefaultPostProcessInputs(
    const std::vector<PostProcessInputSlotDesc>& declared)
{
    std::vector<PostProcessInputSlot> out;
    if (declared.empty())
    {
        out.push_back(PostProcessInputSlot{L"SceneColor", 0u, PostProcessInputSemantic::SceneColor, true, TextureHandle::Invalid()});
        out.push_back(PostProcessInputSlot{L"OriginalSceneColor", 1u, PostProcessInputSemantic::OriginalSceneColor, false, TextureHandle::Invalid()});
        return out;
    }
    out.reserve(declared.size());
    for (const auto& d : declared)
        out.push_back(PostProcessInputSlot{d.name, d.shaderRegister, d.semantic, d.required, TextureHandle::Invalid()});
    return out;
}

// ---------------------------------------------------------------------------
// Post-Process Pass Descriptor
// ---------------------------------------------------------------------------

struct PostProcessPassDesc
{
    std::wstring vertexShaderFile;
    std::wstring pixelShaderFile;
    std::wstring debugName;
    uint32_t constantBufferBytes = 0u;
    bool enabled = true;
    std::vector<PostProcessInputSlotDesc> inputSlots;
};

struct PostProcessResource
{
    PostProcessPassDesc desc;
    std::vector<uint8_t> constantData;
    std::vector<PostProcessInputSlot> inputs;
    uint32_t constantBufferBytes = 0u;
    bool enabled = true;
    bool cpuDirty = false;
    bool ready = false;
};
