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

// Must match cbuffer GTAOParams in PostProcessGTAOPS.hlsl.
struct alignas(16) GTAOParams
{
    float    texelW         = 1.0f / 1280.0f;
    float    texelH         = 1.0f / 720.0f;
    float    radiusPixels   = 36.0f;
    float    thickness      = 0.9f;

    float    intensity      = 2.8f;
    float    power          = 1.1f;
    float    normalBias     = 0.10f;
    float    depthClamp     = 0.85f;

    float    nearPlane      = 0.1f;
    float    farPlane       = 100.0f;
    float    depthFadeStart = 60.0f;
    float    depthFadeEnd   = 100.0f;

    float    projScaleX     = 1.0f;
    float    projScaleY     = 1.0f;
    uint32_t directionCount = 8u;
    uint32_t stepCount      = 4u;

    uint32_t cameraIsOrtho  = 0u;
    uint32_t debugView      = 0u;
    uint32_t pad0           = 0u;
    uint32_t pad1           = 0u;
};
static_assert(sizeof(GTAOParams) == 80);


// Must match cbuffer EdgeDebugParams in PostProcessEdgeDebugPS.hlsl.
struct alignas(16) EdgeDebugParams
{
    float texelW      = 1.0f / 1280.0f;
    float texelH      = 1.0f / 720.0f;
    float depthScale  = 250.0f;
    float normalScale = 4.0f;

    float depthOnly   = 0.0f;
    float normalOnly  = 0.0f;
    float pad0        = 0.0f;
    float pad1        = 0.0f;
};
static_assert(sizeof(EdgeDebugParams) == 32);

// Must match cbuffer GTAOBlurParams in PostProcessGTAOBlurPS.hlsl.
struct alignas(16) GTAOBlurParams
{
    float texelW          = 1.0f / 1280.0f;
    float texelH          = 1.0f / 720.0f;
    float depthSharpness  = 64.0f;
    float normalSharpness = 16.0f;

    float nearPlane       = 0.1f;
    float farPlane        = 100.0f;
    uint32_t cameraIsOrtho= 0u;
    uint32_t pad0         = 0u;
};
static_assert(sizeof(GTAOBlurParams) == 32);

// PostProcessGTAOCompositePS.hlsl — b0
struct alignas(16) GTAOCompositeParams
{
    float minVisibility          = 0.90f;  // lower clamp for AO visibility
    float strength               = 0.18f;  // final AO blend strength
    float highlightProtectStart  = 1.10f;  // start protecting HDR highlights
    float highlightProtectEnd    = 3.00f;  // fully protect bright emissive/bloom sources
};
static_assert(sizeof(GTAOCompositeParams) == 16);

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
    float         cameraProjScaleX   = 1.0f;
    float         cameraProjScaleY   = 1.0f;
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
        cameraProjScaleX   = 1.0f;
        cameraProjScaleY   = 1.0f;
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
    bool captureSceneColorAsOriginal = false;
    uint32_t originalSceneGroup = 0u; // 0 = shared/default, >0 = isolated branch capture group
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
