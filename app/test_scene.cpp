#include "GDXEngine.h"
#include "GDXEventQueue.h"
#include "WindowDesc.h"
#include "GDXWin32Window.h"
#include "GDXWin32DX11ContextFactory.h"
#include "Debug.h"

#include "GDXECSRenderer.h"
#include "GDXDX11RenderBackend.h"

#include "Components.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "HierarchySystem.h"
#include "Events.h"
#include "GDXShaderResource.h"
#include "GDXVertexFlags.h"
#include "NeonTimeBuffer.h"

#include <memory>
#include <vector>
#include <array>
#include <cmath>
#include <variant>
#include <filesystem>
#include <system_error>
#include <cstdio>
#include <DirectXMath.h>

static constexpr uint32_t LAYER_DEFAULT = 1u << 0;
static constexpr uint32_t LAYER_FX = 1u << 1;
static constexpr uint32_t LAYER_REFLECTION = 1u << 2;
static constexpr uint32_t LAYER_ALL = 0xFFFFFFFFu;

static constexpr float PI = 3.14159265358979f;

// -----------------------------------------------------------------------------
// Bone animation data
// -----------------------------------------------------------------------------
static constexpr int   N_BONES = 6;
static constexpr float SEG_H = 1.5f;
static constexpr float HALF_W = 0.38f;
static constexpr float HALF_D = 0.22f;
static constexpr int   N_RINGS = 2 * N_BONES + 1;
static constexpr int   VERTS_RING = 4;
static constexpr int   NUM_ASTEROIDS = 20;
static constexpr int   NUM_BG_CUBES = 100;

static bool FileExists(const std::wstring& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

static void WrapDeg(float& v)
{
    while (v >= 360.0f) v -= 360.0f;
    while (v < 0.0f) v += 360.0f;
}

static void SetLookAt(
    TransformComponent& tc,
    const Float3& position,
    const Float3& target,
    const Float3& upVec = Float3{ 0.0f, 1.0f, 0.0f })
{
    Float3 forward = GIDX::Normalize3(GIDX::Subtract(target, position), { 0.0f, 0.0f, 1.0f });
    Float3 up = GIDX::Normalize3(upVec, { 0.0f, 1.0f, 0.0f });

    float dot = std::fabs(GIDX::Dot3(forward, up));
    if (dot > 0.9999f)
    {
        up = { 0.0f, 0.0f, 1.0f };
        dot = std::fabs(GIDX::Dot3(forward, up));
        if (dot > 0.9999f)
            up = { 0.0f, 1.0f, 0.0f };
    }

    Float3 right = GIDX::Normalize3(GIDX::Cross(up, forward), { 1.0f, 0.0f, 0.0f });
    up = GIDX::Cross(forward, right);

    tc.localRotation = GIDX::QuaternionFromBasis(right, up, forward);
    tc.localPosition = position;
    tc.dirty = true;
}

static SubmeshData BuildTailSubmesh()
{
    SubmeshData s;

    s.positions.reserve(N_RINGS * VERTS_RING);
    s.normals.reserve(N_RINGS * VERTS_RING);
    s.colors.reserve(N_RINGS * VERTS_RING);
    s.uv0.reserve(N_RINGS * VERTS_RING);
    s.boneIndices.reserve(N_RINGS * VERTS_RING);
    s.boneWeights.reserve(N_RINGS * VERTS_RING);
    s.indices.reserve((N_RINGS - 1) * VERTS_RING * 12);

    const float cx[VERTS_RING] = { -HALF_W,  HALF_W,  HALF_W, -HALF_W };
    const float cz[VERTS_RING] = { -HALF_D, -HALF_D,  HALF_D,  HALF_D };
    const float nx[VERTS_RING] = { -0.707f,  0.707f,  0.707f, -0.707f };
    const float nz[VERTS_RING] = { -0.707f, -0.707f,  0.707f,  0.707f };

    for (int r = 0; r < N_RINGS; ++r)
    {
        const float t = static_cast<float>(r) * 0.5f;
        const float y = t * SEG_H;
        const float gradient = y / (2 * SEG_H);

        const float cr = (20.0f + 30.0f * gradient) / 255.0f;
        const float cg = (60.0f + 195.0f * gradient) / 255.0f;
        const float cb = (140.0f + 115.0f * gradient) / 255.0f;

        for (int c = 0; c < VERTS_RING; ++c)
        {
            s.positions.push_back({ cx[c], y, cz[c] });
            s.normals.push_back({ nx[c], 0.0f, nz[c] });
            s.colors.push_back({ cr, cg, cb, 1.0f });

            const float u = static_cast<float>(c) / static_cast<float>(VERTS_RING - 1);
            s.uv0.push_back({ u, gradient });
        }
    }

    for (int r = 0; r < N_RINGS; ++r)
    {
        const int  boneBase = r / 2;
        const bool isBoundary = ((r % 2) == 0);

        for (int c = 0; c < VERTS_RING; ++c)
        {
            if (isBoundary)
            {
                if (boneBase == 0)
                {
                    s.boneIndices.push_back({ 0, 0, 0, 0 });
                    s.boneWeights.push_back({ 1.0f, 0.0f, 0.0f, 0.0f });
                }
                else if (boneBase >= N_BONES)
                {
                    s.boneIndices.push_back({ N_BONES - 1, 0, 0, 0 });
                    s.boneWeights.push_back({ 1.0f, 0.0f, 0.0f, 0.0f });
                }
                else
                {
                    s.boneIndices.push_back({ static_cast<uint32_t>(boneBase - 1), static_cast<uint32_t>(boneBase), 0, 0 });
                    s.boneWeights.push_back({ 0.5f, 0.5f, 0.0f, 0.0f });
                }
            }
            else
            {
                s.boneIndices.push_back({ static_cast<uint32_t>(boneBase), 0, 0, 0 });
                s.boneWeights.push_back({ 1.0f, 0.0f, 0.0f, 0.0f });
            }
        }
    }

    for (int r = 0; r < N_RINGS - 1; ++r)
    {
        const int b = r * VERTS_RING;
        const int t = (r + 1) * VERTS_RING;

        for (int f = 0; f < VERTS_RING; ++f)
        {
            const int nf = (f + 1) % VERTS_RING;

            s.indices.push_back(b + f);
            s.indices.push_back(t + f);
            s.indices.push_back(b + nf);

            s.indices.push_back(t + f);
            s.indices.push_back(t + nf);
            s.indices.push_back(b + nf);

            s.indices.push_back(b + f);
            s.indices.push_back(b + nf);
            s.indices.push_back(t + f);

            s.indices.push_back(t + f);
            s.indices.push_back(b + nf);
            s.indices.push_back(t + nf);
        }
    }

    return s;
}

static SubmeshData MakeVertexColorCube()
{
    SubmeshData s = BuiltinMeshes::Cube();
    s.colors.resize(s.positions.size());

    static const std::array<Float4, 6> faceColors = {
        Float4{1,0,0,1},
        Float4{0,1,0,1},
        Float4{0,0,1,1},
        Float4{1,1,0,1},
        Float4{1,0,1,1},
        Float4{0,1,1,1}
    };

    for (size_t face = 0; face < 6; ++face)
    {
        const auto c = faceColors[face];
        const size_t base = face * 4;
        for (size_t i = 0; i < 4; ++i)
            s.colors[base + i] = c;
    }
    return s;
}

static SubmeshData MakeCutoutQuad()
{
    SubmeshData s;
    s.positions = {
        { -1.5f, 0.0f, 0.0f },
        { -1.5f, 3.0f, 0.0f },
        {  1.5f, 0.0f, 0.0f },
        {  1.5f, 3.0f, 0.0f },
    };

    s.normals = {
        { 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, 1.0f },
    };

    s.uv0 = {
        { 0.0f, 1.0f },
        { 0.0f, 0.0f },
        { 1.0f, 1.0f },
        { 1.0f, 0.0f },
    };

    s.indices = { 0, 2, 1, 2, 3, 1 };
    return s;
}

struct TintParams
{
    float tint[4] = { 0.7f, 0.7f, 1.0f, 1.0f };
};

// Added flexible bloom postprocess

struct BloomBrightParams
{
    float threshold = 2.0f;
    float intensity = 10.5f;
    float pad0 = 0.0f; // platzhalter
    float pad1 = 0.0f; // platzhalter
};

struct BloomBlurParams
{
    float texelSize[2] = { 1.0f / 1280.0f, 1.0f / 720.0f };
    float direction[2] = { 1.0f, 0.0f };
};

struct BloomCompositeParams
{
    float bloomTint[4] = { 1.0f, 0.72f, 0.32f, 1.0f };
    float bloomStrength = 0.1f;
    float sceneStrength = 1.0f;
    float pad0 = 0.0f;
    float pad1 = 0.0f;
};

class BoneAnimationTestScene
{
public:
    explicit BoneAnimationTestScene(GDXECSRenderer& renderer)
        : m_renderer(renderer)
    {
    }

    void Init()
    {
        Registry& reg = m_renderer.GetRegistry();
        Debug::Log("bone_animation.cpp: build integrated test scene");

        CreateMeshes();
        CreateMaterials();
        CreateCameras();
        CreateLights();
        CreateGround();
        CreateCenterBoneObject();
        CreateVertexColorArea();
        CreateAlphaTestArea();
        CreateNeonArea();
        CreateTransparencyArea();
        CreateParentingArea();
        CreateRTTArea();
        CreateBackgroundCubeField();

        m_renderer.SetSceneAmbient(0.18f, 0.18f, 0.22f);

        if (auto* tc = reg.Get<TransformComponent>(m_camera))
            SetLookAt(*tc, { 0.0f, 10.0f, -28.0f }, { 0.0f, 3.0f, 0.0f });

        Debug::Log("Controls: D = toggle cube visible, F = toggle cube active, G = toggle main camera FX layer");
        Debug::Log("bone_animation.cpp: integrated test scene ready");

        CreateBloomChain();

        //PostProcessPassDesc ppDesc{};
        //ppDesc.vertexShaderFile = L"PostProcessFullscreenVS.hlsl";
        //ppDesc.pixelShaderFile = L"PostProcessTintPS.hlsl";
        //ppDesc.debugName = L"Tint PostProcess";
        //ppDesc.constantBufferBytes = sizeof(TintParams);
        //
        //m_tintPass = m_renderer.CreatePostProcessPass(ppDesc);
        //m_renderer.SetPostProcessConstants(m_tintPass, &m_tintParams, sizeof(m_tintParams));
    }

    void CreateBloomChain()
    {
        PostProcessPassDesc bright{};
        bright.vertexShaderFile = L"PostProcessFullscreenVS.hlsl";
        bright.pixelShaderFile = L"PostProcessBloomBrightPS.hlsl";
        bright.debugName = L"BloomBrightPass";
        bright.constantBufferBytes = sizeof(BloomBrightParams);
        m_brightPass = m_renderer.CreatePostProcessPass(bright);
        m_renderer.SetPostProcessConstants(m_brightPass, &m_brightParams, sizeof(m_brightParams));

        PostProcessPassDesc blurH{};
        blurH.vertexShaderFile = L"PostProcessFullscreenVS.hlsl";
        blurH.pixelShaderFile = L"PostProcessBloomBlurPS.hlsl";
        blurH.debugName = L"BloomBlurH";
        blurH.constantBufferBytes = sizeof(BloomBlurParams);
        m_blurPassH = m_renderer.CreatePostProcessPass(blurH);
        m_blurParamsH.direction[0] = 1.0f;
        m_blurParamsH.direction[1] = 0.0f;
        m_renderer.SetPostProcessConstants(m_blurPassH, &m_blurParamsH, sizeof(m_blurParamsH));

        PostProcessPassDesc blurV{};
        blurV.vertexShaderFile = L"PostProcessFullscreenVS.hlsl";
        blurV.pixelShaderFile = L"PostProcessBloomBlurPS.hlsl";
        blurV.debugName = L"BloomBlurV";
        blurV.constantBufferBytes = sizeof(BloomBlurParams);
        m_blurPassV = m_renderer.CreatePostProcessPass(blurV);
        m_blurParamsV.direction[0] = 0.0f;
        m_blurParamsV.direction[1] = 1.0f;
        m_renderer.SetPostProcessConstants(m_blurPassV, &m_blurParamsV, sizeof(m_blurParamsV));

        PostProcessPassDesc composite{};
        composite.vertexShaderFile = L"PostProcessFullscreenVS.hlsl";
        composite.pixelShaderFile = L"PostProcessBloomCompositePS.hlsl";
        composite.debugName = L"BloomComposite";
        composite.constantBufferBytes = sizeof(BloomCompositeParams);
        m_compositePass = m_renderer.CreatePostProcessPass(composite);
        m_renderer.SetPostProcessConstants(m_compositePass, &m_compositeParams, sizeof(m_compositeParams));
    }

    void Update(float dt)
    {
        Registry& reg = m_renderer.GetRegistry();

        m_timeAcc += dt;
        m_demoYawA += 42.0f * dt;
        m_demoYawB -= 31.0f * dt;
        m_cutoutYaw += 18.0f * dt;
        WrapDeg(m_demoYawA);
        WrapDeg(m_demoYawB);
        WrapDeg(m_cutoutYaw);

        UpdateOrbitCamera(dt);
        UpdateBoneAnimation();
        UpdateVertexColorArea();
        UpdateNeonArea(dt);
        UpdateTransparencyArea(dt);
        UpdateParentingArea(dt);
        UpdateRTTCamera();
        UpdateBackgroundCubeField(dt);

        if (auto* tc = reg.Get<TransformComponent>(m_vertexColorCube))
            tc->SetEulerDeg(18.0f, m_demoYawA, 0.0f);

        if (auto* tc = reg.Get<TransformComponent>(m_plainCube))
            tc->SetEulerDeg(0.0f, m_demoYawB, 0.0f);

        if (auto* tc = reg.Get<TransformComponent>(m_cutoutA))
            tc->SetEulerDeg(0.0f, m_cutoutYaw, 0.0f);

        if (auto* tc = reg.Get<TransformComponent>(m_rttCube))
        {
            m_rttCubeYaw += 0.0f * dt;
            WrapDeg(m_rttCubeYaw);
            tc->SetEulerDeg(0.0f, m_rttCubeYaw, 0.0f);
        }
    }

    void Shutdown()
    {
        m_neonTime.Shutdown();
    }

    void OnEvent(const Event& e, GDXEngine& engine)
    {
        std::visit([&](auto&& ev)
            {
                using T = std::decay_t<decltype(ev)>;
                if constexpr (std::is_same_v<T, KeyPressedEvent>)
                {
                    if (ev.repeat)
                        return;

                    switch (ev.key)
                    {
                    case Key::Escape:
                        engine.Shutdown();
                        break;
                    case Key::D:
                        ToggleVisibility(m_toggleCube);
                        break;
                    case Key::F:
                        ToggleActive(m_toggleCube);
                        break;
                    case Key::G:
                        ToggleMainCameraLayer(LAYER_FX);
                        break;
                    default:
                        break;
                    }
                }
            }, e);
    }

private:

    void CreateMeshes()
    {
        {
            MeshAssetResource asset;
            asset.debugName = "Cube";
            asset.AddSubmesh(BuiltinMeshes::Cube());
            m_hCube = m_renderer.UploadMesh(std::move(asset));
        }
        {
            MeshAssetResource asset;
            asset.debugName = "Sphere";
            asset.AddSubmesh(BuiltinMeshes::Sphere(0.5f, 24, 16));
            m_hSphere = m_renderer.UploadMesh(std::move(asset));
        }
        {
            MeshAssetResource asset;
            asset.debugName = "TailSkinned";
            asset.AddSubmesh(BuildTailSubmesh());
            m_hTail = m_renderer.UploadMesh(std::move(asset));
        }
        {
            MeshAssetResource asset;
            asset.debugName = "VertexColorCube";
            asset.AddSubmesh(MakeVertexColorCube());
            m_hVertexColorCube = m_renderer.UploadMesh(std::move(asset));
        }
        {
            MeshAssetResource asset;
            asset.debugName = "CutoutQuad";
            asset.AddSubmesh(MakeCutoutQuad());
            m_hCutoutQuad = m_renderer.UploadMesh(std::move(asset));
        }
    }

    void CreateMaterials()
    {
        m_texBricks = TextureHandle::Invalid();
        m_texFace = TextureHandle::Invalid();
        m_texEngine = TextureHandle::Invalid();
        m_texAlbedo = TextureHandle::Invalid();
        m_texNormal = TextureHandle::Invalid();
        m_texORM = TextureHandle::Invalid();
        m_texAlphaMask = TextureHandle::Invalid();

        if (FileExists(L"..\\media\\bricks.bmp"))
            m_texBricks = m_renderer.LoadTexture(L"..\\media\\bricks.bmp", true);
        if (FileExists(L"..\\media\\face.bmp"))
            m_texFace = m_renderer.LoadTexture(L"..\\media\\dx.bmp", true);
        if (FileExists(L"..\\media\\engine.png"))
            m_texEngine = m_renderer.LoadTexture(L"..\\media\\engine.png", true);
        if (FileExists(L"..//media//albedo.png"))
            m_texAlbedo = m_renderer.LoadTexture(L"..//media//albedo.png", true);
        if (FileExists(L"..//media//normal.png"))
            m_texNormal = m_renderer.LoadTexture(L"..//media//normal.png", false);
        if (FileExists(L"..//media//orm.png"))
            m_texORM = m_renderer.LoadTexture(L"..//media//orm.png", false);
        if (FileExists(L"..//media//alpha_mask.png"))
            m_texAlphaMask = m_renderer.LoadTexture(L"..//media//alpha_mask.png", true);

        {
            MaterialResource mat = MaterialResource::FlatColor(0.18f, 0.18f, 0.22f, 1.0f);
            mat.data.flags |= MF_SHADING_PBR;
            mat.data.metallic = 0.0f;
            mat.data.roughness = 0.95f;
            mat.data.receiveShadows = 1.0f;
            if (m_texAlbedo.IsValid()) mat.SetTexture(MaterialTextureSlot::Albedo, m_texAlbedo);
            if (m_texNormal.IsValid())
            {
                mat.SetTexture(MaterialTextureSlot::Normal, m_texNormal);
                mat.data.flags |= MF_USE_NORMAL_MAP;
                mat.data.normalScale = 1.0f;
            }
            if (m_texORM.IsValid())
            {
                mat.SetTexture(MaterialTextureSlot::ORM, m_texORM);
                mat.data.flags |= MF_USE_ORM_MAP;
                mat.data.occlusionStrength = 1.0f;
            }
            m_hGroundMat = m_renderer.CreateMaterial(mat);
        }

        {
            MaterialResource mat = MaterialResource::FlatColor(1.0f, 1.0f, 1.0f, 1.0f);
            mat.data.flags |= MF_SHADING_PBR;
            mat.data.metallic = 0.0f;
            mat.data.roughness = 0.70f;
            mat.data.receiveShadows = 1.0f;
            mat.data.uvTilingOffset = { 0.5f, 0.5f, 0.0f, 0.0f };
            if (m_texAlbedo.IsValid()) mat.SetTexture(MaterialTextureSlot::Albedo, m_texAlbedo);
            if (m_texNormal.IsValid())
            {
                mat.SetTexture(MaterialTextureSlot::Normal, m_texNormal);
                mat.data.flags |= MF_USE_NORMAL_MAP;
            }
            if (m_texORM.IsValid())
            {
                mat.SetTexture(MaterialTextureSlot::ORM, m_texORM);
                mat.data.flags |= MF_USE_ORM_MAP;
            }
            m_hTailMat = m_renderer.CreateMaterial(mat);
        }

        m_hWhite = m_renderer.CreateMaterial(MaterialResource::FlatColor(1.0f, 1.0f, 1.0f, 1.0f));
        m_hGray = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.35f, 0.35f, 0.38f, 1.0f));
        m_hRed = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.85f, 0.20f, 0.20f, 1.0f));
        m_hBlue = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.18f, 0.35f, 0.90f, 1.0f));
        m_hGreen = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.15f, 0.75f, 0.30f, 1.0f));

        {
            MaterialResource mat = MaterialResource::FlatColor(1.0f, 1.0f, 1.0f, 1.0f);
            if (m_texEngine.IsValid())
                mat.SetTexture(MaterialTextureSlot::Albedo, m_texEngine);
            m_hEngineTex = m_renderer.CreateMaterial(mat);
        }

        {
            MaterialResource mat = MaterialResource::FlatColor(1.0f, 1.0f, 1.0f, 1.0f);
            mat.SetFlag(MF_UNLIT, true);
            if (m_texBricks.IsValid())
                mat.SetTexture(MaterialTextureSlot::Albedo, m_texBricks);
            m_hEngineTexUnlit = m_renderer.CreateMaterial(mat);
        }

        {
            MaterialResource mat = MaterialResource::FlatColor(1.0f, 1.0f, 1.0f, 1.0f);
            mat.data.flags = MF_ALPHA_TEST | MF_USE_EMISSIVE;
            mat.data.alphaCutoff = 0.5f;
            mat.data.receiveShadows = 1.0f;
            mat.data.emissiveColor = { 20.0f, 8.0f, 2.5f, 1.0f };

            mat.SetTexture(MaterialTextureSlot::Albedo, m_texAlphaMask);
            // erstmal keine Emissive-Textur
            // mat.SetTexture(MaterialTextureSlot::Emissive, ...);

            m_hEngineTexGlow = m_renderer.CreateMaterial(mat);
        }

        {
            MaterialResource mat = MaterialResource::FlatColor(1.0f, 1.0f, 1.0f, 1.0f);
            mat.data.flags = MF_ALPHA_TEST;
            mat.data.alphaCutoff = 0.5f;
            mat.data.receiveShadows = 1.0f;
            if (m_texAlphaMask.IsValid())
                mat.SetTexture(MaterialTextureSlot::Albedo, m_texAlphaMask);
            m_hCutout = m_renderer.CreateMaterial(mat);
        }

        {
            auto makePBR = [&](float r, float g, float b, float metallic, float roughness, bool receiveShadows = true)
                {
                    MaterialResource mat = MaterialResource::FlatColor(r, g, b, 1.0f);
                    mat.data.flags |= MF_SHADING_PBR;
                    mat.data.metallic = metallic;
                    mat.data.roughness = roughness;
                    mat.data.receiveShadows = receiveShadows ? 1.0f : 0.0f;
                    return m_renderer.CreateMaterial(mat);
                };

            m_hSunMat = makePBR(1.0f, 0.85f, 0.10f, 0.0f, 0.4f, false);
            m_hEarthMat = makePBR(0.15f, 0.40f, 0.85f, 0.0f, 0.6f, true);
            m_hMoonMat = makePBR(0.72f, 0.72f, 0.72f, 0.05f, 0.8f, true);
            m_hAsteroidMat = makePBR(0.55f, 0.42f, 0.30f, 0.1f, 0.85f, true);

            MaterialResource emissive = MaterialResource::FlatColor(1.0f, 1.0f, 1.0f, 1.0f);
            emissive.data.flags |= MF_SHADING_PBR | MF_USE_EMISSIVE;
            emissive.data.metallic = 0.0f;
            emissive.data.roughness = 0.3f;
            emissive.data.receiveShadows = 0.0f;
            emissive.data.emissiveColor = { 1.0f, 1.0f, 1.0f, 0.9f };
            m_hFlagMat = m_renderer.CreateMaterial(emissive);
        }

        m_neonShader = m_renderer.CreateShader(
            L"VertexShaderNeon_GIDX.hlsl",
            L"PixelShaderNeon_GIDX.hlsl",
            GDX_VERTEX_POSITION);

        {
            MaterialResource neonMat;
            neonMat.shader = m_neonShader;
            neonMat.SetFlag(MF_UNLIT, true);
            neonMat.data.baseColor = { 1, 1, 1, 1 };
            m_hNeon = m_renderer.CreateMaterial(std::move(neonMat));
            m_neonTime.Initialize(m_renderer, m_hNeon);
        }

        {
            MaterialResource mat = MaterialResource::FlatColor(0.8f, 0.8f, 0.8f, 1.0f);
            mat.data.receiveShadows = 1.0f;
            mat.data.transparency = 0.0f;
            mat.data.flags = MF_NONE;
            m_hTransFloor = m_renderer.CreateMaterial(mat);
        }

        {
            MaterialResource mat = MaterialResource::FlatColor(0.85f, 0.15f, 0.15f, 1.0f);
            mat.data.receiveShadows = 1.0f;
            mat.data.transparency = 0.0f;
            mat.data.flags = MF_NONE;
            m_hTransSolid = m_renderer.CreateMaterial(mat);
        }

        {
            MaterialResource mat = MaterialResource::FlatColor(0.2f, 0.4f, 1.0f, 0.45f);
            mat.data.receiveShadows = 1.0f;
            mat.data.transparency = 1.0f - 0.45f;
            mat.data.flags = MF_TRANSPARENT;
            m_hTransFar = m_renderer.CreateMaterial(mat);
        }

        {
            MaterialResource mat = MaterialResource::FlatColor(0.2f, 0.9f, 0.3f, 0.50f);
            mat.data.receiveShadows = 1.0f;
            mat.data.transparency = 1.0f - 0.50f;
            mat.data.flags = MF_TRANSPARENT;
            m_hTransMid = m_renderer.CreateMaterial(mat);
        }

        {
            MaterialResource mat = MaterialResource::FlatColor(1.0f, 0.85f, 0.1f, 0.55f);
            mat.data.receiveShadows = 1.0f;
            mat.data.transparency = 1.0f - 0.55f;
            mat.data.flags = MF_TRANSPARENT;
            m_hTransNear = m_renderer.CreateMaterial(mat);
        }

        m_rtt = m_renderer.CreateRenderTarget(512, 512, L"SceneOverviewRTT");
        const TextureHandle rttTex = m_renderer.GetRenderTargetTexture(m_rtt);
        {
            MaterialResource mat = MaterialResource::FlatColor(1.0f, 1.0f, 1.0f, 1.0f);
            mat.SetTexture(MaterialTextureSlot::Albedo, rttTex, MaterialTextureUVSet::UV0);
            m_hRTT = m_renderer.CreateMaterial(mat);
        }

        {
            const std::array<Float3, 8> palette = {
                Float3{1.00f, 0.25f, 0.25f},
                Float3{0.25f, 0.75f, 1.00f},
                Float3{0.25f, 1.00f, 0.45f},
                Float3{1.00f, 0.85f, 0.20f},
                Float3{0.95f, 0.30f, 1.00f},
                Float3{1.00f, 0.55f, 0.20f},
                Float3{0.55f, 0.90f, 0.95f},
                Float3{0.85f, 0.85f, 0.90f}
            };

            for (size_t i = 0; i < palette.size(); ++i)
            {
                const Float3& c = palette[i];
                m_hBgCubeMats[i] = m_renderer.CreateMaterial(
                    MaterialResource::FlatColor(c.x, c.y, c.z, 1.0f));
            }
        }
    }

    void CreateCameras()
    {
        Registry& reg = m_renderer.GetRegistry();

        m_camera = reg.CreateEntity();
        reg.Add<TagComponent>(m_camera, "MainCamera");
        reg.Add<TransformComponent>(m_camera);
        reg.Add<WorldTransformComponent>(m_camera);
        {
            CameraComponent cam;
            cam.aspectRatio = 1280.0f / 720.0f;
            cam.nearPlane = 0.1f;
            cam.farPlane = 300.0f;
            cam.fovDeg = 60.0f;
            cam.cullMask = LAYER_ALL;
            reg.Add<CameraComponent>(m_camera, cam);
        }
        reg.Add<ActiveCameraTag>(m_camera);

        m_rttCamera = reg.CreateEntity();
        reg.Add<TagComponent>(m_rttCamera, "RTTCamera");
        reg.Add<TransformComponent>(m_rttCamera);
        reg.Add<WorldTransformComponent>(m_rttCamera);
        {
            CameraComponent cam;
            cam.aspectRatio = 1.0f;
            cam.nearPlane = 0.1f;
            cam.farPlane = 250.0f;
            cam.fovDeg = 65.0f;
            cam.cullMask = LAYER_DEFAULT | LAYER_REFLECTION;
            reg.Add<CameraComponent>(m_rttCamera, cam);
        }

        RenderTargetCameraComponent rttCam(m_rtt);
        rttCam.enabled = true;
        rttCam.autoAspectFromTarget = true;
        rttCam.renderOpaque = true;
        rttCam.renderTransparent = true;
        rttCam.renderShadows = true;
        rttCam.skipSelfReferentialDraws = true;
        rttCam.clear.clearColorEnabled = true;
        rttCam.clear.clearColor[0] = 0.05f;
        rttCam.clear.clearColor[1] = 0.06f;
        rttCam.clear.clearColor[2] = 0.10f;
        rttCam.clear.clearColor[3] = 1.0f;
        reg.Add<RenderTargetCameraComponent>(m_rttCamera, rttCam);
    }

    void CreateLights()
    {
        Registry& reg = m_renderer.GetRegistry();

        m_sunLight = reg.CreateEntity();
        reg.Add<TagComponent>(m_sunLight, "Sun");
        {
            LightComponent lc;
            lc.kind = LightKind::Directional;
            lc.diffuseColor = { 0.85f, 0.95f, 1.0f, 1.0f };
            lc.intensity = 3.0f;
            lc.castShadows = true;
            lc.shadowOrthoSize = 50.0f;
            lc.shadowNear = 0.5f;
            lc.shadowFar = 100.0f;
            reg.Add<LightComponent>(m_sunLight, lc);
        }
        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 0.0f, 0.0f };
            tc.SetEulerDeg(55.0f, -35.0f, 0.0f);
            reg.Add<TransformComponent>(m_sunLight, tc);
        }
        reg.Add<WorldTransformComponent>(m_sunLight);

        m_redLight = reg.CreateEntity();
        reg.Add<TagComponent>(m_redLight, "RedPointLight");
        {
            LightComponent lc;
            lc.kind = LightKind::Point;
            lc.diffuseColor = { 1.0f, 0.25f, 0.25f, 1.0f };
            lc.intensity = 2.0f;
            lc.radius = 30.0f;
            reg.Add<LightComponent>(m_redLight, lc);
        }
        {
            TransformComponent tc;
            tc.localPosition = { -10.0f, 5.0f, 0.0f };
            reg.Add<TransformComponent>(m_redLight, tc);
        }
        reg.Add<WorldTransformComponent>(m_redLight);

        m_blueLight = reg.CreateEntity();
        reg.Add<TagComponent>(m_blueLight, "BluePointLight");
        {
            LightComponent lc;
            lc.kind = LightKind::Point;
            lc.diffuseColor = { 0.25f, 0.45f, 1.0f, 1.0f };
            lc.intensity = 2.0f;
            lc.radius = 30.0f;
            reg.Add<LightComponent>(m_blueLight, lc);
        }
        {
            TransformComponent tc;
            tc.localPosition = { 10.0f, 5.0f, 0.0f };
            reg.Add<TransformComponent>(m_blueLight, tc);
        }
        reg.Add<WorldTransformComponent>(m_blueLight);
    }

    void CreateGround()
    {
        MakeEntity("Ground", m_hCube, m_hGroundMat,
            { 0.0f, -1.0f, 0.0f }, { 42.0f, 0.5f, 42.0f }, false, LAYER_DEFAULT | LAYER_REFLECTION);
    }

    void CreateCenterBoneObject()
    {
        Registry& reg = m_renderer.GetRegistry();

        m_tail = MakeEntity("TailMesh", m_hTail, m_hTailMat,
            { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION);

        SkinComponent skin;
        skin.enabled = true;
        skin.finalBoneMatrices.resize(N_BONES);
        for (int i = 0; i < N_BONES; ++i)
            skin.finalBoneMatrices[i] = GIDX::Identity4x4();
        reg.Add<SkinComponent>(m_tail, std::move(skin));

        m_invBind.resize(N_BONES);
        for (int i = 0; i < N_BONES; ++i)
            m_invBind[i] = DirectX::XMMatrixTranslation(0.0f, -static_cast<float>(i) * SEG_H, 0.0f);
    }

    void CreateVertexColorArea()
    {
        m_vertexColorCube = MakeEntity("VertexColorCube", m_hVertexColorCube, m_hWhite,
            { -10.0f, 1.2f, -2.0f }, { 2.0f, 2.0f, 2.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION);

        m_plainCube = MakeEntity("PlainCube", m_hCube, m_hRed,
            { -6.5f, 1.0f, -1.0f }, { 1.6f, 1.6f, 1.6f }, true, LAYER_DEFAULT | LAYER_REFLECTION);

        m_toggleCube = MakeEntity("ToggleCube", m_hCube, m_hBlue,
            { -9.0f, 0.9f, 2.8f }, { 1.2f, 1.2f, 1.2f }, true, LAYER_FX);

        MakeEntity("VertexColorPedestal", m_hCube, m_hGray,
            { -8.2f, -0.3f, -1.5f }, { 6.0f, 0.4f, 4.0f }, false, LAYER_DEFAULT | LAYER_REFLECTION);
    }

    void CreateAlphaTestArea()
    {
        m_cutoutA = MakeEntity("CutoutA", m_hCutoutQuad, m_hCutout,
            { 7.0f, 0.0f, -7.0f }, { 1.0f, 1.0f, 1.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION);

        m_cutoutB = MakeEntity("CutoutB", m_hCutoutQuad, m_hCutout,
            { 9.5f, 0.0f, -5.5f }, { 1.0f, 1.0f, 1.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION);

        if (auto* tc = m_renderer.GetRegistry().Get<TransformComponent>(m_cutoutB))
            tc->SetEulerDeg(0.0f, -35.0f, 0.0f);

        MakeEntity("CutoutBase", m_hCube, m_hGray,
            { 8.2f, -0.45f, -6.2f }, { 5.0f, 0.3f, 4.0f }, false, LAYER_DEFAULT | LAYER_REFLECTION);
    }

    void CreateNeonArea()
    {
        m_neonCube = MakeEntity("NeonCube", m_hCube, m_hNeon,
            { 10.0f, 7.0f, 5.0f }, { 2.7f, 2.7f, 2.7f }, true, LAYER_DEFAULT | LAYER_REFLECTION);

        m_texturedCube = MakeEntity("TexturedCube", m_hCube, m_hEngineTex,
            { 6.5f, 0.5f, 4.0f }, { 1.7f, 1.7f, 1.7f }, true, LAYER_DEFAULT | LAYER_REFLECTION);

        m_unlitTexturedCube = MakeEntity("UnlitTexturedCube", m_hCube, m_hEngineTexUnlit,
            { 2.0f, 2.0f, 6.5f }, { 2.5f, 2.5f, 2.5f }, true, LAYER_DEFAULT | LAYER_REFLECTION);

        m_glowTexturedCube = MakeEntity("GlowTexturedCube", m_hCube, m_hEngineTexGlow,
            { 13.0f, 2.0f, 4.4f }, { 2.5f, 2.5f, 2.5f }, true, LAYER_DEFAULT | LAYER_REFLECTION);

        MakeEntity("NeonBase", m_hCube, m_hGray,
            { 8.3f, -0.35f, 4.4f }, { 10.5f, 0.3f, 4.5f }, true, LAYER_DEFAULT | LAYER_REFLECTION);
    }

    void CreateTransparencyArea()
    {
        m_transFloor = MakeEntity("TransparencyFloor", m_hCube, m_hTransFloor,
            { 0.0f, 2.0f, -16.0f }, { 8.0f, 1.0f, 8.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION);

        m_transSolidCube = MakeEntity("TransparencyOpaqueRedCube", m_hCube, m_hTransSolid,
            { 0.0f, 4.0f, -21.0f }, { 1.2f, 1.2f, 1.2f }, true, LAYER_DEFAULT | LAYER_REFLECTION);

        m_transCubeFar = MakeEntity("TransparencyFar", m_hCube, m_hTransFar,
            { 0.0f, 4.0f, -19.0f }, { 1.0f, 1.0f, 1.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION);

        m_transCubeMid = MakeEntity("TransparencyMid", m_hCube, m_hTransMid,
            { 0.0f, 4.0f, -16.0f }, { 1.0f, 1.0f, 1.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION);

        m_transCubeNear = MakeEntity("TransparencyNear", m_hCube, m_hTransNear,
            { 0.0f, 4.0f, -13.0f }, { 1.0f, 1.0f, 1.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION);

        //MakeEntity("TransparencyBase", m_hCube, m_hGray,
        //    { 0.0f, -0.55f, 26.0f }, { 6.0f, 0.35f, 10.5f }, false, LAYER_DEFAULT | LAYER_REFLECTION);
    }

    void CreateParentingArea()
    {
        Registry& reg = m_renderer.GetRegistry();

        m_solarRoot = MakeEmptyEntity("SolarRootPivot",
            { 0.0f, 15.5f, 0.0f });

        m_sunVisual = MakeEntity("SunVisual", m_hSphere, m_hSunMat,
            { 0.0f, 0.0f, 0.0f }, { 4.2f, 4.2f, 4.2f }, false, LAYER_DEFAULT | LAYER_REFLECTION);
        HierarchySystem::SetParent(reg, m_sunVisual, m_solarRoot);

        m_earthOrbit = MakeEmptyEntity("EarthOrbitPivot",
            { 8.0f, 0.0f, 0.0f });
        HierarchySystem::SetParent(reg, m_earthOrbit, m_solarRoot);

        m_earth = MakeEntity("EarthVisual", m_hSphere, m_hEarthMat,
            { 0.0f, 0.0f, 0.0f }, { 1.1f, 1.1f, 1.1f }, true, LAYER_DEFAULT | LAYER_REFLECTION);
        HierarchySystem::SetParent(reg, m_earth, m_earthOrbit);

        m_moonOrbit = MakeEmptyEntity("MoonOrbitPivot", { 0.0f, 0.0f, 0.0f });
        HierarchySystem::SetParent(reg, m_moonOrbit, m_earthOrbit);

        m_moon = MakeEntity("MoonVisual", m_hSphere, m_hMoonMat,
            { 2.8f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION);
        HierarchySystem::SetParent(reg, m_moon, m_moonOrbit);

        m_flag = MakeEntity("MoonFlag", m_hCube, m_hFlagMat,
            { 0.0f, 0.48f, 0.0f }, { 0.12f, 0.45f, 0.12f }, false, LAYER_DEFAULT | LAYER_REFLECTION);
        HierarchySystem::SetParent(reg, m_flag, m_moon);

        for (int i = 0; i < NUM_ASTEROIDS; ++i)
        {
            const float a = 2.0f * PI * static_cast<float>(i) / static_cast<float>(NUM_ASTEROIDS);
            const float r = 5.0f;
            const float s = 0.18f + 0.08f * std::sin(static_cast<float>(i) * 1.3f);

            char name[32];
            std::snprintf(name, sizeof(name), "Asteroid_%d", i);
            m_asteroids[i] = MakeEntity(name, m_hSphere, m_hAsteroidMat,
                { r * std::cos(a), 0.25f * std::sin(static_cast<float>(i) * 0.9f), r * std::sin(a) },
                { s, s, s }, true, LAYER_DEFAULT | LAYER_REFLECTION);
            HierarchySystem::SetParent(reg, m_asteroids[i], m_sunVisual);
        }
    }

    void CreateRTTArea()
    {
        Registry& reg = m_renderer.GetRegistry();

        m_rttCube = MakeEntity("RTTCube", m_hCube, m_hRTT,
            { 0.0f, 7.0f, 16.0f }, { 10.0f, 10.0f, 5.0f }, true, LAYER_FX);

        MakeEntity("RTTStand", m_hCube, m_hGray,
            { 0.0f, 0.2f, 16.0f }, { 10.0f, 0.4f, 10.0f }, false, LAYER_DEFAULT | LAYER_REFLECTION);

        if (auto* tc = reg.Get<TransformComponent>(m_rttCamera))
            SetLookAt(*tc, { -18.0f, 14.0f, -20.0f }, { 0.0f, 3.0f, 0.0f });
    }

    void CreateBackgroundCubeField()
    {
        Registry& reg = m_renderer.GetRegistry();

        const int cols = 25;
        const int rows = 20;
        const float startX = -50.0f;
        const float startY = 8.0f;
        const float stepX = 7.5f;
        const float stepY = 3.2f;
        const float baseZ = 0.0f;

        for (int i = 0; i < NUM_BG_CUBES; ++i)
        {
            const int cx = i % cols;
            const int cy = i / cols;

            const float jitterX = std::sin(static_cast<float>(i) * 2.13f) * 1.25f;
            const float jitterY = std::cos(static_cast<float>(i) * 1.71f) * 0.9f;
            const float jitterZ = std::sin(static_cast<float>(i) * 0.73f) * 12.0f;

            const Float3 pos = {
                startX + static_cast<float>(cx) * stepX + jitterX,
                startY + static_cast<float>(cy) * stepY + jitterY,
                baseZ + jitterZ
            };

            const float s = 0.45f + 0.35f * (0.5f + 0.5f * std::sin(static_cast<float>(i) * 1.37f));
            const Float3 scale = { s, s, s };

            char name[48];
            std::snprintf(name, sizeof(name), "BgCube_%d", i);

            const MaterialHandle mat = m_hBgCubeMats[static_cast<size_t>(i) % m_hBgCubeMats.size()];
            m_bgCubes[i] = MakeEntity(name, m_hCube, mat, pos, scale, false, LAYER_DEFAULT | LAYER_REFLECTION);

            m_bgYaw[i] = std::fmod(17.0f * static_cast<float>(i), 360.0f);
            m_bgPitch[i] = std::fmod(29.0f * static_cast<float>(i), 360.0f);
            m_bgRoll[i] = std::fmod(11.0f * static_cast<float>(i), 360.0f);

            m_bgYawSpeed[i] = 18.0f + std::fmod(static_cast<float>(i) * 3.7f, 55.0f);
            m_bgPitchSpeed[i] = 10.0f + std::fmod(static_cast<float>(i) * 2.9f, 40.0f);
            m_bgRollSpeed[i] = 14.0f + std::fmod(static_cast<float>(i) * 4.1f, 48.0f);
        }

        Debug::Log("Background cube field created: ", NUM_BG_CUBES, " cubes");
    }

    void UpdateOrbitCamera(float dt)
    {
        Registry& reg = m_renderer.GetRegistry();
        m_mainCamAngle += 10.0f * dt;
        const float rad = m_mainCamAngle * PI / 180.0f;
        const Float3 pos = {
            24.0f * std::sin(rad),
            10.5f,
            -24.0f * std::cos(rad)
        };

        if (auto* tc = reg.Get<TransformComponent>(m_camera))
            SetLookAt(*tc, pos, { 0.0f, 3.0f, 0.0f });

        if (auto* tc = reg.Get<TransformComponent>(m_redLight))
        {
            tc->localPosition = { -10.0f + 5.0f * std::cos(m_timeAcc), 4.5f, 4.0f * std::sin(m_timeAcc) };
            tc->dirty = true;
        }
        if (auto* tc = reg.Get<TransformComponent>(m_blueLight))
        {
            tc->localPosition = { 10.0f + 5.0f * std::cos(-m_timeAcc * 0.8f), 4.5f, 4.0f * std::sin(-m_timeAcc * 0.8f) };
            tc->dirty = true;
        }
    }

    void UpdateBoneAnimation()
    {
        Registry& reg = m_renderer.GetRegistry();
        auto* skin = reg.Get<SkinComponent>(m_tail);
        if (!skin || skin->finalBoneMatrices.size() < N_BONES)
            return;

        DirectX::XMMATRIX boneWorld[N_BONES];
        DirectX::XMMATRIX finalMats[N_BONES];

        constexpr float WAVE_AMP = 0.30f;
        constexpr float WAVE_FREQ = 1.8f;
        constexpr float WAVE_AMP_Z = 0.20f;
        constexpr float WAVE_FREQ_Z = 1.3f;
        const float phaseStep = PI / static_cast<float>(N_BONES);

        const float ax0 = WAVE_AMP * std::sin(m_timeAcc * WAVE_FREQ);
        const float az0 = WAVE_AMP_Z * std::sin(m_timeAcc * WAVE_FREQ_Z + (PI * 0.5f));

        boneWorld[0] =
            DirectX::XMMatrixRotationX(ax0) *
            DirectX::XMMatrixRotationZ(az0);

        for (int i = 1; i < N_BONES; ++i)
        {
            const float phi = i * phaseStep;
            const float axi = WAVE_AMP * std::sin(m_timeAcc * WAVE_FREQ + phi);
            const float azi = WAVE_AMP_Z * std::sin(m_timeAcc * WAVE_FREQ_Z + phi + (PI * 0.5f));

            boneWorld[i] =
                DirectX::XMMatrixTranslation(0.0f, SEG_H, 0.0f) *
                DirectX::XMMatrixRotationX(axi) *
                DirectX::XMMatrixRotationZ(azi) *
                boneWorld[i - 1];
        }

        for (int i = 0; i < N_BONES; ++i)
        {
            finalMats[i] = m_invBind[i] * boneWorld[i];
            GDXMathHelpers::StoreFloat4x4(skin->finalBoneMatrices[i], finalMats[i]);
        }
    }

    void UpdateVertexColorArea()
    {
    }

    void UpdateNeonArea(float dt)
    {
        m_neonTime.Update(dt);

        Registry& reg = m_renderer.GetRegistry();
        if (auto* tc = reg.Get<TransformComponent>(m_neonCube))
        {
            m_neonYaw += 55.0f * dt;
            tc->SetEulerDeg(m_neonYaw * 0.35f, m_neonYaw, 0.0f);
        }
        if (auto* tc = reg.Get<TransformComponent>(m_texturedCube))
        {
            m_texCubeYaw -= 42.0f * dt;
            tc->SetEulerDeg(0.0f, m_texCubeYaw, 0.0f);
        }
        if (auto* tc = reg.Get<TransformComponent>(m_unlitTexturedCube))
        {
            m_unlitTexCubeYaw += 38.0f * dt;
            tc->SetEulerDeg(m_unlitTexCubeYaw, m_unlitTexCubeYaw, 0.0f);
        }
        if (auto* tc = reg.Get<TransformComponent>(m_glowTexturedCube))
        {
            m_glowTexCubeYaw -= 48.0f * dt;
            tc->SetEulerDeg(0.0f, m_glowTexCubeYaw, m_glowTexCubeYaw);
        }
    }

    void UpdateTransparencyArea(float dt)
    {
        Registry& reg = m_renderer.GetRegistry();

        m_transRotFar += 35.0f * dt;
        m_transRotMid += 24.5f * dt;
        m_transRotNear += 45.5f * dt;

        WrapDeg(m_transRotFar);
        WrapDeg(m_transRotMid);
        WrapDeg(m_transRotNear);

        if (auto* tc = reg.Get<TransformComponent>(m_transCubeFar))
            tc->SetEulerDeg(0.0f, m_transRotFar, 0.0f);

        if (auto* tc = reg.Get<TransformComponent>(m_transCubeMid))
            tc->SetEulerDeg(0.0f, m_transRotMid, 0.0f);

        if (auto* tc = reg.Get<TransformComponent>(m_transCubeNear))
            tc->SetEulerDeg(0.0f, m_transRotNear, 0.0f);
    }

    void UpdateParentingArea(float dt)
    {
        Registry& reg = m_renderer.GetRegistry();

        if (auto* tc = reg.Get<TransformComponent>(m_solarRoot))
        {
            m_sunYaw += 10.0f * dt;
            WrapDeg(m_sunYaw);
            tc->SetEulerDeg(0.0f, m_sunYaw, 0.0f);
        }

        if (auto* tc = reg.Get<TransformComponent>(m_sunVisual))
            tc->SetEulerDeg(0.0f, m_sunYaw * 2.5f, 0.0f);

        if (auto* tc = reg.Get<TransformComponent>(m_earth))
        {
            m_earthYaw += 60.0f * dt;
            WrapDeg(m_earthYaw);
            tc->SetEulerDeg(0.0f, m_earthYaw, 0.0f);
        }

        if (auto* tc = reg.Get<TransformComponent>(m_moonOrbit))
        {
            m_moonAngle += 220.0f * dt;
            WrapDeg(m_moonAngle);
            tc->SetEulerDeg(0.0f, m_moonAngle, 0.0f);
        }

        if (auto* tc = reg.Get<TransformComponent>(m_moon))
            tc->SetEulerDeg(0.0f, -m_moonAngle * 2.0f, 0.0f);

        for (int i = 0; i < NUM_ASTEROIDS; ++i)
        {
            if (auto* tc = reg.Get<TransformComponent>(m_asteroids[i]))
            {
                m_asteroidYaw[i] += (35.0f + 12.0f * static_cast<float>(i)) * dt;
                WrapDeg(m_asteroidYaw[i]);
                tc->SetEulerDeg(10.0f + static_cast<float>(i) * 3.0f, m_asteroidYaw[i], 0.0f);
            }
        }
    }

    void UpdateRTTCamera()
    {
        Registry& reg = m_renderer.GetRegistry();
        if (auto* tc = reg.Get<TransformComponent>(m_rttCamera))
            SetLookAt(*tc, { -18.0f, 14.0f, -20.0f }, { 0.0f, 3.0f, 0.0f });
    }

    void UpdateBackgroundCubeField(float dt)
    {
        Registry& reg = m_renderer.GetRegistry();

        for (int i = 0; i < NUM_BG_CUBES; ++i)
        {
            auto* tc = reg.Get<TransformComponent>(m_bgCubes[i]);
            if (!tc)
                continue;

            m_bgYaw[i] += m_bgYawSpeed[i] * dt;
            m_bgPitch[i] += m_bgPitchSpeed[i] * dt;
            m_bgRoll[i] += m_bgRollSpeed[i] * dt;

            WrapDeg(m_bgYaw[i]);
            WrapDeg(m_bgPitch[i]);
            WrapDeg(m_bgRoll[i]);

            tc->SetEulerDeg(m_bgPitch[i], m_bgYaw[i], m_bgRoll[i]);
        }
    }

    void ToggleMainCameraLayer(uint32_t bit)
    {
        Registry& reg = m_renderer.GetRegistry();
        if (auto* cam = reg.Get<CameraComponent>(m_camera))
        {
            cam->cullMask ^= bit;
            Debug::Log("main camera cullMask = ", cam->cullMask);
        }
    }

    void ToggleVisibility(EntityID e)
    {
        Registry& reg = m_renderer.GetRegistry();
        if (auto* vis = reg.Get<VisibilityComponent>(e))
        {
            vis->visible = !vis->visible;
            Debug::Log("toggle cube visible = ", vis->visible ? "true" : "false");
        }
    }

    void ToggleActive(EntityID e)
    {
        Registry& reg = m_renderer.GetRegistry();
        if (auto* vis = reg.Get<VisibilityComponent>(e))
        {
            vis->active = !vis->active;
            Debug::Log("toggle cube active = ", vis->active ? "true" : "false");
        }
    }

    EntityID MakeEmptyEntity(
        const char* name,
        const Float3& pos,
        uint32_t layerMask = LAYER_DEFAULT | LAYER_REFLECTION)
    {
        Registry& reg = m_renderer.GetRegistry();

        EntityID e = reg.CreateEntity();
        reg.Add<TagComponent>(e, name);

        TransformComponent tc;
        tc.localPosition = pos;
        tc.localScale = { 1.0f, 1.0f, 1.0f };
        reg.Add<TransformComponent>(e, tc);

        reg.Add<WorldTransformComponent>(e);

        VisibilityComponent vis;
        vis.visible = false;
        vis.active = true;
        vis.castShadows = false;
        vis.receiveShadows = false;
        vis.layerMask = layerMask;
        reg.Add<VisibilityComponent>(e, vis);

        return e;
    }

    EntityID MakeEntity(
        const char* name,
        MeshHandle mesh,
        MaterialHandle mat,
        const Float3& pos,
        const Float3& scale,
        bool castShadows,
        uint32_t layerMask)
    {
        Registry& reg = m_renderer.GetRegistry();

        EntityID e = reg.CreateEntity();
        reg.Add<TagComponent>(e, name);

        TransformComponent tc;
        tc.localPosition = pos;
        tc.localScale = scale;
        reg.Add<TransformComponent>(e, tc);

        reg.Add<WorldTransformComponent>(e);
        reg.Add<RenderableComponent>(e, mesh, mat, 0u);

        VisibilityComponent vis;
        vis.visible = true;
        vis.active = true;
        vis.castShadows = castShadows;
        vis.receiveShadows = true;
        vis.layerMask = layerMask;
        reg.Add<VisibilityComponent>(e, vis);

        return e;
    }

private:
    GDXECSRenderer& m_renderer;
    NeonTimeBuffer m_neonTime;

    EntityID m_camera = NULL_ENTITY;
    EntityID m_rttCamera = NULL_ENTITY;
    EntityID m_sunLight = NULL_ENTITY;
    EntityID m_redLight = NULL_ENTITY;
    EntityID m_blueLight = NULL_ENTITY;

    EntityID m_tail = NULL_ENTITY;
    EntityID m_vertexColorCube = NULL_ENTITY;
    EntityID m_plainCube = NULL_ENTITY;
    EntityID m_toggleCube = NULL_ENTITY;
    EntityID m_cutoutA = NULL_ENTITY;
    EntityID m_cutoutB = NULL_ENTITY;
    EntityID m_neonCube = NULL_ENTITY;
    EntityID m_texturedCube = NULL_ENTITY;
    EntityID m_unlitTexturedCube = NULL_ENTITY;
    EntityID m_glowTexturedCube = NULL_ENTITY;

    EntityID m_transFloor = NULL_ENTITY;
    EntityID m_transSolidCube = NULL_ENTITY;
    EntityID m_transCubeFar = NULL_ENTITY;
    EntityID m_transCubeMid = NULL_ENTITY;
    EntityID m_transCubeNear = NULL_ENTITY;

    EntityID m_solarRoot = NULL_ENTITY;
    EntityID m_sunVisual = NULL_ENTITY;
    EntityID m_earthOrbit = NULL_ENTITY;
    EntityID m_earth = NULL_ENTITY;
    EntityID m_moonOrbit = NULL_ENTITY;
    EntityID m_moon = NULL_ENTITY;
    EntityID m_flag = NULL_ENTITY;
    EntityID m_rttCube = NULL_ENTITY;
    std::array<EntityID, NUM_ASTEROIDS> m_asteroids{};
    std::array<EntityID, NUM_BG_CUBES> m_bgCubes{};

    MeshHandle m_hCube;
    MeshHandle m_hSphere;
    MeshHandle m_hTail;
    MeshHandle m_hVertexColorCube;
    MeshHandle m_hCutoutQuad;

    MaterialHandle m_hGroundMat;
    MaterialHandle m_hTailMat;
    MaterialHandle m_hWhite;
    MaterialHandle m_hGray;
    MaterialHandle m_hRed;
    MaterialHandle m_hBlue;
    MaterialHandle m_hGreen;
    MaterialHandle m_hEngineTex;
    MaterialHandle m_hEngineTexUnlit;
    MaterialHandle m_hEngineTexGlow;
    MaterialHandle m_hCutout;
    MaterialHandle m_hSunMat;
    MaterialHandle m_hEarthMat;
    MaterialHandle m_hMoonMat;
    MaterialHandle m_hAsteroidMat;
    MaterialHandle m_hFlagMat;
    MaterialHandle m_hNeon;

    MaterialHandle m_hTransFloor;
    MaterialHandle m_hTransSolid;
    MaterialHandle m_hTransFar;
    MaterialHandle m_hTransMid;
    MaterialHandle m_hTransNear;

    MaterialHandle m_hRTT;
    std::array<MaterialHandle, 8> m_hBgCubeMats{};

    ShaderHandle m_neonShader;
    RenderTargetHandle m_rtt;

    TextureHandle m_texBricks;
    TextureHandle m_texFace;
    TextureHandle m_texEngine;
    TextureHandle m_texAlbedo;
    TextureHandle m_texNormal;
    TextureHandle m_texORM;
    TextureHandle m_texAlphaMask;

    // Example 1
    PostProcessHandle m_tintPass = PostProcessHandle::Invalid();
    TintParams m_tintParams{};
    // Example 2
    PostProcessHandle m_brightPass = PostProcessHandle::Invalid();
    PostProcessHandle m_blurPassH = PostProcessHandle::Invalid();
    PostProcessHandle m_blurPassV = PostProcessHandle::Invalid();
    PostProcessHandle m_compositePass = PostProcessHandle::Invalid();
    BloomBrightParams m_brightParams{};
    BloomBlurParams m_blurParamsH{};
    BloomBlurParams m_blurParamsV{};
    BloomCompositeParams m_compositeParams{};


    std::vector<DirectX::XMMATRIX> m_invBind;

    float m_timeAcc = 0.0f;
    float m_mainCamAngle = 0.0f;
    float m_demoYawA = 0.0f;
    float m_demoYawB = 0.0f;
    float m_cutoutYaw = 0.0f;
    float m_neonYaw = 0.0f;
    float m_texCubeYaw = 0.0f;
    float m_unlitTexCubeYaw = 0.0f;
    float m_glowTexCubeYaw = 0.0f;
    float m_rttCubeYaw = 0.0f;
    float m_sunYaw = 0.0f;
    float m_earthYaw = 0.0f;
    float m_moonAngle = 0.0f;
    float m_earthOrbitYaw = 0.0f;
    float m_earthOrbitAngle = 0.0f;
    float m_moonOrbitAngle = 0.0f;

    float m_transRotFar = 0.0f;
    float m_transRotMid = 0.0f;
    float m_transRotNear = 0.0f;

    std::array<float, NUM_ASTEROIDS> m_asteroidYaw{};

    std::array<float, NUM_BG_CUBES> m_bgYaw{};
    std::array<float, NUM_BG_CUBES> m_bgPitch{};
    std::array<float, NUM_BG_CUBES> m_bgRoll{};
    std::array<float, NUM_BG_CUBES> m_bgYawSpeed{};
    std::array<float, NUM_BG_CUBES> m_bgPitchSpeed{};
    std::array<float, NUM_BG_CUBES> m_bgRollSpeed{};
};

#include <windows.h>

int main()
{
    GDXEventQueue events;

    WindowDesc desc;
    desc.width = 1280;
    desc.height = 720;
    desc.title = "GIDX - Bone Animation Integrated Test Scene";
    desc.resizable = true;
    desc.borderless = false;

    auto windowOwned = std::make_unique<GDXWin32Window>(desc, events);
    if (!windowOwned->Create())
    {
        Debug::LogError("bone_animation.cpp: window creation failed");
        return 1;
    }

    auto adapters = GDXWin32DX11ContextFactory::EnumerateAdapters();
    if (adapters.empty())
    {
        Debug::LogError("bone_animation.cpp: no DX11 adapter found");
        return 2;
    }

    const unsigned int adapterIdx = GDXWin32DX11ContextFactory::FindBestAdapter(adapters);

    GDXWin32DX11ContextFactory dx11Factory;
    auto dxContext = dx11Factory.Create(*windowOwned, adapterIdx);
    if (!dxContext)
    {
        Debug::LogError("bone_animation.cpp: DX11 context creation failed");
        return 3;
    }

    auto backendOwned = std::make_unique<GDXDX11RenderBackend>(std::move(dxContext));
    auto rendererOwned = std::make_unique<GDXECSRenderer>(std::move(backendOwned));
    GDXECSRenderer* renderer = rendererOwned.get();
    renderer->SetClearColor(0.04f, 0.05f, 0.08f);

    GDXEngine engine(std::move(windowOwned), std::move(rendererOwned), events);
    if (!engine.Initialize())
    {
        Debug::LogError("bone_animation.cpp: engine initialize failed");
        return 4;
    }

    BoneAnimationTestScene app(*renderer);
    app.Init();

    renderer->SetTickCallback([&](float dt)
        {
            app.Update(dt);
        });

    engine.SetEventCallback([&](const Event& e)
        {
            app.OnEvent(e, engine);
        });

    engine.Run();
    app.Shutdown();
    engine.Shutdown();
    return 0;
}
