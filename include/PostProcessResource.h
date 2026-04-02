#pragma once

#include "Handle.h"
#include "ShaderSourceType.h"
#include "Core/GDXMath.h"

#include <cstdint>
#include <string>
#include <vector>


// ---------------------------------------------------------------------------
// Fog
// ---------------------------------------------------------------------------

enum class FogMode : uint32_t
{
    LinearDepth = 0u,
    ExpDepth    = 1u,
    Exp2Depth   = 2u,
};

struct FogSettings
{
    bool     enabled           = false;
    FogMode  mode              = FogMode::LinearDepth;

    float    colorR            = 0.03f;
    float    colorG            = 0.03f;
    float    colorB            = 0.035f;

    float    start             = 0.55f;   // LinearDepth: normalized raw scene depth [0..1]
    float    end               = 0.98f;   // LinearDepth: normalized raw scene depth [0..1]
    float    density           = 2.0f;    // Exp / Exp2: applied on linearized depth normalized by far plane
    float    maxOpacity        = 0.75f;
    float    power             = 1.0f;

    bool     heightFogEnabled  = false;
    float    heightStart       = 0.0f;    // world-space Y
    float    heightEnd         = 1.0f;    // world-space Y
    float    heightStrength    = 1.0f;
};

// Must match cbuffer FogParams in PostProcessDepthFogPS.hlsl exactly.
struct alignas(16) FogParams
{
    float    colorR;
    float    colorG;
    float    colorB;
    uint32_t mode;

    float    start;
    float    end;
    float    density;
    float    maxOpacity;

    float    power;
    float    heightStart;
    float    heightEnd;
    float    heightStrength;

    float    cameraNearPlane;
    float    cameraFarPlane;
    float    projScaleX;
    float    projScaleY;

    uint32_t enabled;
    uint32_t heightFogEnabled;
    uint32_t cameraIsOrtho;
    uint32_t pad0;

    float    invView[16];
};
static_assert(sizeof(FogParams) == 144, "FogParams must be 144 bytes");



// ---------------------------------------------------------------------------
// Volumetric Fog
// ---------------------------------------------------------------------------

struct VolumetricFogSettings
{
    bool     enabled           = false;

    float    colorR            = 0.80f;
    float    colorG            = 0.85f;
    float    colorB            = 1.00f;
    float    density           = 0.045f;

    float    anisotropy        = 0.25f;
    float    startDistance     = 1.0f;
    float    maxDistance       = 60.0f;
    float    maxOpacity        = 0.85f;

    float    baseHeight        = 0.0f;
    float    heightFalloff     = 0.10f;
    uint32_t stepCount         = 48u;
    float    shadowStrength    = 1.0f;

    float    lightIntensity    = 1.0f;
    float    jitterStrength    = 1.0f;
};

// Must match cbuffer VolumetricFogParams in PostProcessVolumetricFogPS.hlsl exactly.
struct alignas(16) VolumetricFogParams
{
    float    colorR;
    float    colorG;
    float    colorB;
    float    density;

    float    anisotropy;
    float    startDistance;
    float    maxDistance;
    float    maxOpacity;

    float    baseHeight;
    float    heightFalloff;
    uint32_t stepCount;
    float    shadowStrength;

    float    lightIntensity;
    float    jitterStrength;
    float    cameraNearPlane;
    uint32_t cameraIsOrtho;

    float    cameraFarPlane;
    float    projScaleX;
    float    projScaleY;
    uint32_t cascadeCount;

    float    cameraPos[4];
    float    lightDir[4];
    float    invView[16];
    float    cascadeViewProj[4][16];
    float    cascadeSplits[4];
};
static_assert(sizeof(VolumetricFogParams) == 448, "VolumetricFogParams must be 448 bytes");

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
    ShadowMap = 5,
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
    Float3        cameraPos          = { 0.0f, 0.0f, 0.0f };
    float         _padCameraPos      = 0.0f;
    Matrix4       invViewMatrix      = Matrix4::Identity();
    Matrix4       shadowCascadeViewProj[4] = { Matrix4::Identity(), Matrix4::Identity(), Matrix4::Identity(), Matrix4::Identity() };
    float         shadowCascadeSplits[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    uint32_t      shadowCascadeCount = 0u;
    Float3        shadowLightDir     = { 0.0f, -1.0f, 0.0f };
    float         _padShadowLightDir = 0.0f;

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
        cameraPos          = { 0.0f, 0.0f, 0.0f };
        _padCameraPos      = 0.0f;
        invViewMatrix      = Matrix4::Identity();
        for (uint32_t i = 0u; i < 4u; ++i)
        {
            shadowCascadeViewProj[i] = Matrix4::Identity();
            shadowCascadeSplits[i] = 0.0f;
        }
        shadowCascadeCount = 0u;
        shadowLightDir     = { 0.0f, -1.0f, 0.0f };
        _padShadowLightDir = 0.0f;
    }
};

enum class PostProcessRuntimeTextureSource : uint8_t
{
    SceneColorCurrent = 0,
    OriginalSceneColorBranch = 1,
    OriginalSceneColorFallback = 2,
    SceneDepth = 3,
    SceneNormals = 4,
    CustomTexture = 5,
    ShadowMap = 6,
};

struct PostProcessRuntimeTextureRef
{
    PostProcessRuntimeTextureSource source = PostProcessRuntimeTextureSource::SceneColorCurrent;
    TextureHandle customTexture = TextureHandle::Invalid();
};

struct ResolvedPostProcessBinding
{
    std::wstring                 name;
    uint32_t                     shaderRegister = 0u;
    PostProcessInputSemantic     semantic = PostProcessInputSemantic::SceneColor;
    PostProcessRuntimeTextureRef textureRef{};
    bool                         required = true;
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
// PostProcessInsert — steuert wo ein neuer Pass in die Kette eingefügt wird.
// ---------------------------------------------------------------------------
enum class PostProcessInsert : uint8_t
{
    End,            // nach allem (default)
    Front,          // ganz vorne
    BeforeToneMap,  // vor ToneMapping — typisch für eigene Effekte
    AfterToneMap,   // nach ToneMapping, vor FXAA
    BeforeFXAA,     // direkt vor FXAA
    AfterFXAA,      // nach FXAA
};

struct PostProcessPassDesc
{
    // Shader-Quelle — identisch zu ShaderSourceDesc in IGDXRenderBackend.h.
    // HlslFilePath (default): vertexShaderFile/pixelShaderFile = Dateipfad
    // GlslSource:             vertexShaderFile/pixelShaderFile = GLSL-Quelltext
    // SpirvBinary:            vertexCode/pixelCode = SPIR-V Binärdaten
    ShaderSourceType     sourceType       = ShaderSourceType::HlslFilePath;
    std::wstring         vertexShaderFile;
    std::wstring         pixelShaderFile;
    std::vector<std::string> defines;     // Präprozessor-Defines (HLSL/GLSL)
    std::vector<uint8_t> vertexCode;      // für SpirvBinary
    std::vector<uint8_t> pixelCode;       // für SpirvBinary

    std::wstring debugName;
    uint32_t constantBufferBytes = 0u;
    bool enabled = true;
    bool captureSceneColorAsOriginal = false;
    uint32_t originalSceneGroup = 0u;
    std::vector<PostProcessInputSlotDesc> inputSlots;
};


struct PostProcessPassConstantOverride
{
    PostProcessHandle pass = PostProcessHandle::Invalid();
    std::vector<uint8_t> constantData;
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
