#include "KROMEngine.h"
#include "GDXEventQueue.h"
#include "GDXMathHelpers.h"
#include "WindowDesc.h"
#include "GDXWin32Window.h"
#include "GDXWin32DX11ContextFactory.h"
#include "Core/Debug.h"
#include "GDXECSRenderer.h"
#include "GDXDX11RenderBackend.h"
#include "Components.h"
#include "RenderComponents.h"
#include "MeshAssetResource.h"
#include "MeshBoundsUtil.h"
#include "MaterialResource.h"
#include "ECS/HierarchySystem.h"
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
#include <algorithm>
#include <type_traits>
#include <DirectXMath.h>

static constexpr uint32_t LAYER_DEFAULT = 1u << 0;
static constexpr uint32_t LAYER_FX = 1u << 1;
static constexpr uint32_t LAYER_REFLECTION = 1u << 2;
static constexpr uint32_t LAYER_UI = 1u << 3;
static constexpr uint32_t LAYER_EDITOR = 1u << 4;
static constexpr uint32_t LAYER_SHADOWONLY = 1u << 5;
static constexpr uint32_t LAYER_SKYBOX = 1u << 6;
static constexpr uint32_t LAYER_DEBUG = 1u << 7;
static constexpr uint32_t LAYER_ONOFF = 1u << 8;
static constexpr uint32_t LAYER_ALL = 0xFFFFFFFFu;

static constexpr float PI = 3.14159265358979f;

static constexpr int   N_BONES = 6;
static constexpr float SEG_H = 1.5f;
static constexpr float HALF_W = 0.38f;
static constexpr float HALF_D = 0.22f;
static constexpr int   N_RINGS = 2 * N_BONES + 1;
static constexpr int   VERTS_RING = 4;
static constexpr int   NUM_ASTEROIDS = 100;
static constexpr int   NUM_BG_CUBES = 1000;

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

static Float3 LerpFloat3(const Float3& a, const Float3& b, float t)
{
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

static void SetLookAt(
    TransformComponent& tc,
    const Float3& position,
    const Float3& target,
    const Float3& upVec = Float3{ 0.0f, 1.0f, 0.0f })
{
    Float3 forward = GDX::Normalize3(GDX::Subtract(target, position), { 0.0f, 0.0f, 1.0f });
    Float3 up = GDX::Normalize3(upVec, { 0.0f, 1.0f, 0.0f });

    float dot = std::fabs(GDX::Dot3(forward, up));
    if (dot > 0.9999f)
    {
        up = { 0.0f, 0.0f, 1.0f };
        dot = std::fabs(GDX::Dot3(forward, up));
        if (dot > 0.9999f)
            up = { 0.0f, 1.0f, 0.0f };
    }

    Float3 right = GDX::Normalize3(GDX::Cross(up, forward), { 1.0f, 0.0f, 0.0f });
    up = GDX::Cross(forward, right);

    tc.localRotation = GDX::QuaternionFromBasis(right, up, forward);
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

// Bloom/ToneMapping/FXAA params live in PostProcessResource.h (engine-level definitions).

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

        m_renderer.SetSceneAmbient(0.12f, 0.12f, 0.16f);

        BuildShowcases();
        BuildRTTViews();
        JumpToShowcase(0);
        SetRTTView(0);

        Debug::Log("Controls: Left/Right = next showcase area, 1..7 = change RTT camera view, D = toggle cube visible, F = toggle cube active, G = toggle main camera FX layer");
        Debug::Log("T = toggle tonemapper (ACES/Reinhard), +/- = exposure");
        Debug::Log("B = toggle Free-Fly-Kamera (WASD=move, Q/E=down/up, Arrows=rotate, Space=fast)");
        Debug::Log("bone_animation.cpp: integrated test scene ready");

        // Free-Fly-Kamera an der Standard-Startposition initialisieren.
        m_renderer.GetFreeCamera().SetPosition({ 0.f, 4.f, -12.f });
        m_renderer.GetFreeCamera().SetYawPitch(0.f, -10.f);
        m_renderer.GetFreeCamera().moveSpeed = 8.f;

        if (m_renderer.SupportsTextureFormat(GDXTextureFormat::RGBA16_FLOAT))
        {
            m_renderer.DisableBloom();
            m_renderer.SetToneMapping(ToneMappingMode::ACES, 0.55f);

            m_renderer.SetDepthDebugView(false);
            m_renderer.SetNormalDebugView(false);
            m_renderer.DisableGTAO();
            m_bloomEnabled = false;
            m_gtaoEnabled  = false;
        }
        else
        {
            Debug::LogWarning(GDX_SRC_LOC, "HDR (RGBA16_FLOAT) not supported — Bloom and ToneMapping disabled.");
        }
    }

    void Update(float dt)
    {
        // Free-Fly-Kamera Navigation — läuft nur wenn aktiviert.
        m_renderer.GetFreeCamera().ProcessInput(dt);

        Registry& reg = m_renderer.GetRegistry();

        m_timeAcc += dt;
        m_demoYawA += 42.0f * dt;
        m_demoYawB -= 31.0f * dt;
        m_cutoutYaw += 18.0f * dt;
        WrapDeg(m_demoYawA);
        WrapDeg(m_demoYawB);
        WrapDeg(m_cutoutYaw);

        UpdateShowcaseCamera(dt);
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
            WrapDeg(m_rttCubeYaw);
            tc->SetEulerDeg(0.0f, m_rttCubeYaw, 0.0f);
        }
    }

    void Shutdown()
    {
        m_neonTime.Shutdown();
    }

    void OnEvent(const Event& e, KROMEngine& engine)
    {
        std::visit([&](auto&& ev)
            {
                using T = std::decay_t<decltype(ev)>;
                if constexpr (std::is_same_v<T, WindowResizedEvent>)
                {
                    m_viewportW = ev.width;
                    m_viewportH = ev.height;

                    Registry& reg = m_renderer.GetRegistry();
                    const float aspect = (m_viewportH > 0) ? (static_cast<float>(m_viewportW) / static_cast<float>(m_viewportH)) : (16.0f / 9.0f);
                    if (auto* cam = reg.Get<CameraComponent>(m_camera))
                        cam->aspectRatio = aspect;
                    if (auto* freeCam = reg.Get<CameraComponent>(m_renderer.GetFreeCamera().GetCameraEntity()))
                        freeCam->aspectRatio = aspect;

                    if (m_bloomEnabled) m_renderer.SetBloom(m_viewportW, m_viewportH, 1.25f, 1.0f, 0.28f, 1.0f, 1.0f, 1.0f);
                }
                if constexpr (std::is_same_v<T, KeyPressedEvent>)
                {
                    if (ev.repeat)
                        return;

                    switch (ev.key)
                    {
                    case Key::Escape:
                        engine.Shutdown();
                        break;
                    case Key::B:
                        // Free-Fly-Kamera an/aus
                        m_renderer.GetFreeCamera().Toggle();
                        Debug::Log("FreeCamera: ",
                            m_renderer.GetFreeCamera().IsEnabled() ? "ON" : "OFF");
                        break;
                    case Key::Left:
                        if (!m_renderer.GetFreeCamera().IsEnabled())
                            PrevShowcase();
                        break;
                    case Key::Right:
                        if (!m_renderer.GetFreeCamera().IsEnabled())
                            NextShowcase();
                        break;
                    case Key::Num1:
                        SetRTTView(0);
                        break;
                    case Key::Num2:
                        SetRTTView(1);
                        break;
                    case Key::Num3:
                        SetRTTView(2);
                        break;
                    case Key::Num4:
                        SetRTTView(3);
                        break;
                    case Key::Num5:
                        SetRTTView(4);
                        break;
                    case Key::Num6:
                        SetRTTView(5);
                        break;
                    case Key::Num7:
                        SetRTTView(6);
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
                    case Key::T:
                        m_toneMapParams.mode = (m_toneMapParams.mode == 0) ? 1 : 0;
                        m_renderer.SetToneMapping(static_cast<ToneMappingMode>(m_toneMapParams.mode), m_toneMapParams.exposure, m_toneMapParams.gamma);
                        Debug::Log("Tonemapper: ", m_toneMapParams.mode == 0 ? "ACES Filmic" : "Reinhard");
                        break;
                    case Key::Up:
                        if (!m_renderer.GetFreeCamera().IsEnabled())
                        {
                            m_toneMapParams.exposure = std::clamp(m_toneMapParams.exposure + 0.1f, 0.1f, 4.0f);
                            m_renderer.SetToneMapping(static_cast<ToneMappingMode>(m_toneMapParams.mode), m_toneMapParams.exposure, m_toneMapParams.gamma);
                            Debug::Log("Exposure: ", m_toneMapParams.exposure);
                        }
                        break;
                    case Key::Down:
                        if (!m_renderer.GetFreeCamera().IsEnabled())
                        {
                            m_toneMapParams.exposure = std::clamp(m_toneMapParams.exposure - 0.1f, 0.1f, 4.0f);
                            m_renderer.SetToneMapping(static_cast<ToneMappingMode>(m_toneMapParams.mode), m_toneMapParams.exposure, m_toneMapParams.gamma);
                            Debug::Log("Exposure: ", m_toneMapParams.exposure);
                        }
                        break;
                    case Key::N:
                        JumpToShowcase(3);
                        break;
                    case Key::O:
                        m_renderer.DisableBloom();
                        m_renderer.DisableGTAO();
                        m_bloomEnabled = false;
                        m_gtaoEnabled = false;
                        m_toneMapParams.exposure = 1.25f;
                        m_toneMapParams.mode = static_cast<int>(ToneMappingMode::ACES);
                        m_renderer.SetToneMapping(ToneMappingMode::ACES, m_toneMapParams.exposure, m_toneMapParams.gamma);
                        Debug::Log("Baseline reset: ToneMapping only");
                        break;
                    case Key::U:
                        m_gtaoEnabled = !m_gtaoEnabled;
                        if (m_gtaoEnabled)
                        {
                            m_renderer.SetGTAO(m_viewportW, m_viewportH, 0.1f, 100.0f, 0.0f, 0.0f,0.0f,0.0f);
                            Debug::Log("GTAO: ON");
                        }
                        else
                        {
                            m_renderer.DisableGTAO();
                            Debug::Log("GTAO: OFF");
                        }
                        break;
                    case Key::I:
                        m_bloomEnabled = !m_bloomEnabled;
                        if (m_bloomEnabled)
                        {
                            m_renderer.SetBloom(m_viewportW, m_viewportH, 1.25f, 1.0f, 0.28f, 1.0f, 1.0f, 1.0f);
                            Debug::Log("Bloom: ON");
                        }
                        else
                        {
                            m_renderer.DisableBloom();
                            Debug::Log("Bloom: OFF");
                        }
                        break;
                    default:
                        break;
                    }
                }
            }, e);
    }

private:
    struct ShowcaseScene
    {
        const char* name = "";
        Float3 cameraPos{};
        Float3 cameraTarget{};
    };


    struct RTTViewPreset
    {
        const char* name = "";
        Float3 cameraPos{};
        Float3 cameraTarget{};
    };

    void BuildShowcases()
    {
        m_showcases.clear();
        m_showcases.push_back({ "Bone Animation",  { 0.0f, 7.0f, -14.0f }, { 0.0f, 4.0f, 0.0f } });
        m_showcases.push_back({ "Vertex Colors",   { -8.5f, 4.0f, -8.5f }, { -8.2f, 1.0f, -0.5f } });
        m_showcases.push_back({ "Alpha Test",      { 8.5f, 3.5f, -12.5f }, { 8.0f, 1.3f, -6.0f } });
        m_showcases.push_back({ "Neon / Textures", { 8.5f, 5.0f, -4.0f }, { 8.5f, 2.0f, 4.5f } });
        m_showcases.push_back({ "Transparency",    { 0.0f, 6.0f, -28.0f }, { 0.0f, 4.0f, -16.0f } });
        m_showcases.push_back({ "Parenting / Orbit", { 0.0f, 18.0f, -16.0f }, { 0.0f, 15.0f, 0.0f } });
        m_showcases.push_back({ "RTT",             { 0.0f, 7.0f, 4.0f }, { 0.0f, 6.5f, 16.0f } });
    }


    void BuildRTTViews()
    {
        m_rttViews.clear();
        m_rttViews.push_back({ "Bone Animation",      {  0.0f,  8.0f, -18.0f }, {  0.0f,  4.0f,   0.0f } });
        m_rttViews.push_back({ "Vertex Colors",       { -14.0f,  5.0f, -10.0f }, { -8.0f,  1.0f,  -1.5f } });
        m_rttViews.push_back({ "Alpha Test",          {  14.0f,  4.5f, -12.0f }, {  8.5f,  1.5f,  -6.0f } });
        m_rttViews.push_back({ "Neon / Textures",     {  15.0f,  5.5f,  -5.0f }, {  8.5f,  2.0f,   4.5f } });
        m_rttViews.push_back({ "Transparency",        {   0.0f,  8.0f, -30.0f }, {  0.0f,  4.0f, -16.0f } });
        m_rttViews.push_back({ "Parenting / Orbit",   {   0.0f, 22.0f, -18.0f }, {  0.0f, 15.0f,   0.0f } });
        m_rttViews.push_back({ "RTT Wall",            {   0.0f, 10.0f,   0.0f }, {  0.0f,  7.0f,  16.0f } });
    }

    void SetRTTView(int index)
    {
        if (m_rttViews.empty())
            return;

        if (index < 0 || index >= static_cast<int>(m_rttViews.size()))
            return;

        m_activeRTTView = index;
        m_rttCameraPos = m_rttViews[index].cameraPos;
        m_rttCameraTarget = m_rttViews[index].cameraTarget;

        Registry& reg = m_renderer.GetRegistry();
        if (auto* tc = reg.Get<TransformComponent>(m_rttCamera))
            SetLookAt(*tc, m_rttCameraPos, m_rttCameraTarget);

        Debug::Log("RTT View: ", m_rttViews[index].name);
    }

    void JumpToShowcase(int index)
    {
        if (m_showcases.empty())
            return;

        if (index < 0)
            index = static_cast<int>(m_showcases.size()) - 1;
        if (index >= static_cast<int>(m_showcases.size()))
            index = 0;

        m_activeShowcase = index;
        m_desiredCameraPos = m_showcases[m_activeShowcase].cameraPos;
        m_desiredCameraTarget = m_showcases[m_activeShowcase].cameraTarget;
        m_currentCameraPos = m_desiredCameraPos;
        m_currentCameraTarget = m_desiredCameraTarget;

        Registry& reg = m_renderer.GetRegistry();
        if (auto* tc = reg.Get<TransformComponent>(m_camera))
            SetLookAt(*tc, m_currentCameraPos, m_currentCameraTarget);

        Debug::Log("Showcase: ", m_showcases[m_activeShowcase].name);
    }

    void NextShowcase()
    {
        if (m_showcases.empty())
            return;

        m_activeShowcase = (m_activeShowcase + 1) % static_cast<int>(m_showcases.size());
        m_desiredCameraPos = m_showcases[m_activeShowcase].cameraPos;
        m_desiredCameraTarget = m_showcases[m_activeShowcase].cameraTarget;
        Debug::Log("Showcase: ", m_showcases[m_activeShowcase].name);
    }

    void PrevShowcase()
    {
        if (m_showcases.empty())
            return;

        m_activeShowcase = (m_activeShowcase - 1 + static_cast<int>(m_showcases.size())) % static_cast<int>(m_showcases.size());
        m_desiredCameraPos = m_showcases[m_activeShowcase].cameraPos;
        m_desiredCameraTarget = m_showcases[m_activeShowcase].cameraTarget;
        Debug::Log("Showcase: ", m_showcases[m_activeShowcase].name);
    }

    void UpdateShowcaseCamera(float dt)
    {
        Registry& reg = m_renderer.GetRegistry();
        auto* tc = reg.Get<TransformComponent>(m_camera);
        if (!tc)
            return;

        const float t = std::clamp(dt * m_cameraLerpSpeed, 0.0f, 1.0f);
        m_currentCameraPos = LerpFloat3(m_currentCameraPos, m_desiredCameraPos, t);
        m_currentCameraTarget = LerpFloat3(m_currentCameraTarget, m_desiredCameraTarget, t);
        SetLookAt(*tc, m_currentCameraPos, m_currentCameraTarget);

        if (auto* redTc = reg.Get<TransformComponent>(m_redLight))
        {
            redTc->localPosition = { -10.0f + 5.0f * std::cos(m_timeAcc), 4.5f, 4.0f * std::sin(m_timeAcc) };
            redTc->dirty = true;
        }
        if (auto* blueTc = reg.Get<TransformComponent>(m_blueLight))
        {
            blueTc->localPosition = { 10.0f + 5.0f * std::cos(-m_timeAcc * 0.8f), 4.5f, 4.0f * std::sin(-m_timeAcc * 0.8f) };
            blueTc->dirty = true;
        }
    }

    void CreateMeshes()
    {
        {
            MeshAssetResource asset;
            asset.debugName = "Cube";
            asset.AddSubmesh(BuiltinMeshes::Cube());
            m_boundsCube = ComputeMeshBounds(asset);
            m_hCube = m_renderer.UploadMesh(std::move(asset));
        }
        {
            MeshAssetResource asset;
            asset.debugName = "Sphere";
            asset.AddSubmesh(BuiltinMeshes::Sphere(0.5f, 96, 64));
            m_boundsSphere = ComputeMeshBounds(asset);
            m_hSphere = m_renderer.UploadMesh(std::move(asset));
        }
        {
            MeshAssetResource asset;
            asset.debugName = "TailSkinned";
            asset.AddSubmesh(BuildTailSubmesh());
            m_boundsTail = ComputeMeshBounds(asset);
            m_hTail = m_renderer.UploadMesh(std::move(asset));
        }
        {
            MeshAssetResource asset;
            asset.debugName = "VertexColorCube";
            asset.AddSubmesh(MakeVertexColorCube());
            m_boundsVertexColorCube = ComputeMeshBounds(asset);
            m_hVertexColorCube = m_renderer.UploadMesh(std::move(asset));
        }
        {
            MeshAssetResource asset;
            asset.debugName = "CutoutQuad";
            asset.AddSubmesh(MakeCutoutQuad());
            m_boundsCutoutQuad = ComputeMeshBounds(asset);
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

        //if (FileExists(L"..\\media\\studio.hdr"))
        //    m_renderer.LoadIBL(L"..\\media\\studio.hdr");

        if (FileExists(L"..\\media\\bricks.bmp"))
            m_texBricks = m_renderer.LoadTexture(L"..\\media\\bricks.bmp", true);
        if (FileExists(L"..\\media\\face.bmp"))
            m_texFace = m_renderer.LoadTexture(L"..\\media\\dx.bmp", true);
        if (FileExists(L"..\\media\\engine.png"))
            m_texEngine = m_renderer.LoadTexture(L"..\\media\\engine.png", true);
        if (FileExists(L"..//media//albedo.png"))
            m_texAlbedo = m_renderer.LoadTexture(L"..//media//albedo.png", true);
        if (FileExists(L"..//media//normal.png"))
            m_texNormal = m_renderer.LoadTexture(L"..//media//normal_bricks.png", false);
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
          
                mat.SetNormalTiling(4.0f, 4.0f);
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
            L"VertexShaderNeon.hlsl",
            L"PixelShaderNeon.hlsl",
            GDX_VERTEX_POSITION);

        {
            MaterialResource neonMat;
            neonMat.shader = m_neonShader;
            neonMat.SetFlag(MF_UNLIT, true);
            neonMat.data.baseColor = { 1, 1, 1, 1 };
            neonMat.data.emissiveColor = { 1.0f, 1.0f, 1.0f, 1.0f };
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
            cam.nearPlane = 0.25f;
            cam.farPlane = 1500.0f;
            cam.fovDeg = 60.0f;
            cam.cullMask = LAYER_ALL;
            reg.Add<CameraComponent>(m_camera, cam);
        }
        reg.Add<ActiveCameraTag>(m_camera);

        m_rttCamera = reg.CreateEntity();
        reg.Add<TagComponent>(m_rttCamera, "SideCamera");
        reg.Add<TransformComponent>(m_rttCamera);
        reg.Add<WorldTransformComponent>(m_rttCamera);
        {
            CameraComponent cam;
            cam.aspectRatio = 1.0f;
            cam.nearPlane = 0.25f;
            cam.farPlane = 250.0f;
            cam.fovDeg = 60.0f;
            cam.cullMask = LAYER_DEFAULT | LAYER_REFLECTION | LAYER_ONOFF;
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
            lc.shadowOrthoSize = 60.0f;
            lc.shadowNear = 0.5f;
            lc.shadowFar = 150.0f;
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
            { 0.0f, -1.0f, 0.0f }, { 42.0f, 0.5f, 42.0f }, false, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCube);
    }

    void CreateCenterBoneObject()
    {
        Registry& reg = m_renderer.GetRegistry();

        m_tail = MakeEntity("TailMesh", m_hTail, m_hTailMat,
            { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsTail);

        SkinComponent skin;
        skin.enabled = true;
        skin.finalBoneMatrices.resize(N_BONES);
        for (int i = 0; i < N_BONES; ++i)
            skin.finalBoneMatrices[i] = GDX::Identity4x4();
        reg.Add<SkinComponent>(m_tail, std::move(skin));

        m_invBind.resize(N_BONES);
        for (int i = 0; i < N_BONES; ++i)
            m_invBind[i] = DirectX::XMMatrixTranslation(0.0f, -static_cast<float>(i) * SEG_H, 0.0f);
    }

    void CreateVertexColorArea()
    {
        m_vertexColorCube = MakeEntity("VertexColorCube", m_hVertexColorCube, m_hWhite,
            { -10.0f, 1.2f, -2.0f }, { 2.0f, 2.0f, 2.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsVertexColorCube);

        m_plainCube = MakeEntity("PlainCube", m_hCube, m_hRed,
            { -6.5f, 1.0f, -1.0f }, { 1.6f, 1.6f, 1.6f }, true, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCube);

        m_toggleCube = MakeEntity("ToggleCube", m_hCube, m_hBlue,
            { -4.0f, 0.9f, -2.8f }, { 1.2f, 1.2f, 1.2f }, true, LAYER_ONOFF,
            m_boundsCube);

        MakeEntity("VertexColorPedestal", m_hCube, m_hGray,
            { -8.2f, -0.3f, -1.5f }, { 10.0f, 0.4f, 10.0f }, false, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCube);
    }

    void CreateAlphaTestArea()
    {
        m_cutoutA = MakeEntity("CutoutA", m_hCutoutQuad, m_hCutout,
            { 7.0f, 0.0f, -7.0f }, { 1.0f, 1.0f, 1.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCutoutQuad);

        m_cutoutB = MakeEntity("CutoutB", m_hCutoutQuad, m_hCutout,
            { 9.5f, 0.0f, -5.5f }, { 1.0f, 1.0f, 1.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCutoutQuad);

        if (auto* tc = m_renderer.GetRegistry().Get<TransformComponent>(m_cutoutB))
            tc->SetEulerDeg(0.0f, -35.0f, 0.0f);

        m_glowTexturedCube = MakeEntity("GlowTexturedCube", m_hCube, m_hEngineTexGlow,
            { 10.5f, 3.0f, -5.5f }, { 2.5f, 2.5f, 2.5f }, true, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCube);

        MakeEntity("CutoutBase", m_hCube, m_hGray,
            { 8.2f, -0.45f, -6.2f }, { 7.0f, 0.3f, 5.0f }, false, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCube);
    }

    void CreateNeonArea()
    {
        m_neonCube = MakeEntity("NeonCube", m_hCube, m_hNeon,
            { 12.0f, 3.0f, 5.0f }, { 2.7f, 2.7f, 2.7f }, true, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCube);

        m_texturedCube = MakeEntity("TexturedCube", m_hCube, m_hEngineTex,
            { 8.5f, 1.0f, 4.0f }, { 2.0f, 2.0f, 2.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCube);

        m_unlitTexturedCube = MakeEntity("UnlitTexturedCube", m_hCube, m_hEngineTexUnlit,
            { 3.5f, 2.0f, 6.5f }, { 2.5f, 2.5f, 2.5f }, true, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCube);

        MakeEntity("NeonBase", m_hCube, m_hGray,
            { 8.3f, 0.0f, 4.4f }, { 10.5f, 0.5f, 4.5f }, true, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCube);
    }

    void CreateTransparencyArea()
    {
        m_transFloor = MakeEntity("TransparencyFloor", m_hCube, m_hTransFloor,
            { 0.0f, 2.0f, -16.0f }, { 8.0f, 1.0f, 8.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCube);

        m_transSolidCube = MakeEntity("TransparencyOpaqueRedCube", m_hCube, m_hTransSolid,
            { 0.0f, 4.0f, -21.0f }, { 1.2f, 1.2f, 1.2f }, true, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCube);

        m_transCubeFar = MakeEntity("TransparencyFar", m_hCube, m_hTransFar,
            { 0.0f, 4.0f, -19.0f }, { 1.0f, 1.0f, 1.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCube);

        m_transCubeMid = MakeEntity("TransparencyMid", m_hCube, m_hTransMid,
            { 0.0f, 4.0f, -16.0f }, { 1.0f, 1.0f, 1.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCube);

        m_transCubeNear = MakeEntity("TransparencyNear", m_hCube, m_hTransNear,
            { 0.0f, 4.0f, -13.0f }, { 1.0f, 1.0f, 1.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCube);
    }

    void CreateParentingArea()
    {
        Registry& reg = m_renderer.GetRegistry();

        m_solarRoot = MakeEmptyEntity("SolarRootPivot",
            { 0.0f, 15.5f, 0.0f });

        m_sunVisual = MakeEntity("SunVisual", m_hSphere, m_hSunMat,
            { 0.0f, 0.0f, 0.0f }, { 4.2f, 4.2f, 4.2f }, false, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsSphere);
        HierarchySystem::SetParent(reg, m_sunVisual, m_solarRoot);

        m_earthOrbit = MakeEmptyEntity("EarthOrbitPivot",
            { 8.0f, 0.0f, 0.0f });
        HierarchySystem::SetParent(reg, m_earthOrbit, m_solarRoot);

        m_earth = MakeEntity("EarthVisual", m_hSphere, m_hEarthMat,
            { 0.0f, 0.0f, 0.0f }, { 1.1f, 1.1f, 1.1f }, true, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsSphere);
        HierarchySystem::SetParent(reg, m_earth, m_earthOrbit);

        m_moonOrbit = MakeEmptyEntity("MoonOrbitPivot", { 0.0f, 0.0f, 0.0f });
        HierarchySystem::SetParent(reg, m_moonOrbit, m_earthOrbit);

        m_moon = MakeEntity("MoonVisual", m_hSphere, m_hMoonMat,
            { 2.8f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, true, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsSphere);
        HierarchySystem::SetParent(reg, m_moon, m_moonOrbit);

        m_flag = MakeEntity("MoonFlag", m_hCube, m_hFlagMat,
            { 0.0f, 0.48f, 0.0f }, { 0.12f, 0.45f, 0.12f }, false, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCube);
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
            { 0.0f, 7.0f, 16.0f }, { 10.0f, 10.0f, 5.0f }, true, LAYER_FX,
            m_boundsCube);

        MakeEntity("RTTStand", m_hCube, m_hGray,
            { 0.0f, 0.2f, 16.0f }, { 10.0f, 0.4f, 10.0f }, false, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCube);

        if (auto* tc = reg.Get<TransformComponent>(m_rttCamera))
            SetLookAt(*tc, m_rttCameraPos, m_rttCameraTarget);
    }

    void CreateBackgroundCubeField()
    {
        m_bgCubes.resize(NUM_BG_CUBES);
        m_bgYaw.resize(NUM_BG_CUBES);
        m_bgPitch.resize(NUM_BG_CUBES);
        m_bgRoll.resize(NUM_BG_CUBES);
        m_bgYawSpeed.resize(NUM_BG_CUBES);
        m_bgPitchSpeed.resize(NUM_BG_CUBES);
        m_bgRollSpeed.resize(NUM_BG_CUBES);

        const int cols = 25;
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
            m_bgCubes[i] = MakeEntity(name, m_hCube, mat, pos, scale, false, LAYER_DEFAULT | LAYER_REFLECTION,
            m_boundsCube);

            m_bgYaw[i] = std::fmod(17.0f * static_cast<float>(i), 360.0f);
            m_bgPitch[i] = std::fmod(29.0f * static_cast<float>(i), 360.0f);
            m_bgRoll[i] = std::fmod(11.0f * static_cast<float>(i), 360.0f);

            m_bgYawSpeed[i] = 18.0f + std::fmod(static_cast<float>(i) * 3.7f, 55.0f);
            m_bgPitchSpeed[i] = 10.0f + std::fmod(static_cast<float>(i) * 2.9f, 40.0f);
            m_bgRollSpeed[i] = 14.0f + std::fmod(static_cast<float>(i) * 4.1f, 48.0f);
        }

        Debug::Log("Background cube field created: ", NUM_BG_CUBES, " cubes");
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
            SetLookAt(*tc, m_rttCameraPos, m_rttCameraTarget);
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
        uint32_t layerMask,
        const RenderBoundsComponent& precomputedBounds = {})
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

        RenderBoundsComponent bounds = precomputedBounds.valid
            ? precomputedBounds
            : RenderBoundsComponent::MakeSphere({ 0.0f, 0.0f, 0.0f },
                (std::max)({ scale.x, scale.y, scale.z }) * 0.9f);
        bounds.enableDistanceCull = false;
        bounds.maxViewDistance = 0.0f;
        reg.Add<RenderBoundsComponent>(e, bounds);

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
    std::vector<EntityID> m_bgCubes;

    MeshHandle m_hCube;
    MeshHandle m_hSphere;
    MeshHandle m_hTail;
    MeshHandle m_hVertexColorCube;
    MeshHandle m_hCutoutQuad;

    RenderBoundsComponent m_boundsCube{};
    RenderBoundsComponent m_boundsSphere{};
    RenderBoundsComponent m_boundsTail{};
    RenderBoundsComponent m_boundsVertexColorCube{};
    RenderBoundsComponent m_boundsCutoutQuad{};

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

    int m_viewportW = 1280;
    int m_viewportH = 720;

    PostProcessHandle m_toneMappingPass = PostProcessHandle::Invalid();
    ToneMappingParams m_toneMapParams{ 1.25f, 2.2f, static_cast<int>(ToneMappingMode::ACES), 0.0f };
    bool m_gtaoEnabled = false;
    bool m_bloomEnabled = false;

    std::vector<DirectX::XMMATRIX> m_invBind;
    std::vector<ShowcaseScene> m_showcases;
    std::vector<RTTViewPreset> m_rttViews;

    float m_timeAcc = 0.0f;
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

    float m_transRotFar = 0.0f;
    float m_transRotMid = 0.0f;
    float m_transRotNear = 0.0f;

    std::array<float, NUM_ASTEROIDS> m_asteroidYaw{};

    std::vector<float> m_bgYaw;
    std::vector<float> m_bgPitch;
    std::vector<float> m_bgRoll;
    std::vector<float> m_bgYawSpeed;
    std::vector<float> m_bgPitchSpeed;
    std::vector<float> m_bgRollSpeed;

    int m_activeShowcase = 0;
    int m_activeRTTView = 0;
    float m_cameraLerpSpeed = 6.0f;
    Float3 m_currentCameraPos{ 0.0f, 10.0f, -28.0f };
    Float3 m_currentCameraTarget{ 0.0f, 3.0f, 0.0f };
    Float3 m_desiredCameraPos{ 0.0f, 10.0f, -28.0f };
    Float3 m_desiredCameraTarget{ 0.0f, 3.0f, 0.0f };
    Float3 m_rttCameraPos{ 30.0f, 15.0f, 0.0f };
    Float3 m_rttCameraTarget{ 0.0f, 0.0f, 0.0f };
};

#include <windows.h>

int main()
{
    GDXEventQueue events;

    WindowDesc desc;
    desc.width = 1280;
    desc.height = 720;
    desc.title = "GIDX - Test Scene";
    desc.resizable = true;
    desc.borderless = false;

    auto windowOwned = std::make_unique<GDXWin32Window>(desc, events);
    if (!windowOwned->Create())
    {
        return 1;
    }

    auto adapters = GDXWin32DX11ContextFactory::EnumerateAdapters();
    if (adapters.empty())
    {
        return 2;
    }

    const unsigned int adapterIdx = GDXWin32DX11ContextFactory::FindBestAdapter(adapters);

    GDXWin32DX11ContextFactory dx11Factory;
    auto dxContext = dx11Factory.Create(*windowOwned, adapterIdx);
    if (!dxContext)
    {
        Debug::LogError("DX11 context creation failed");
        return 3;
    }

    auto backendOwned = std::make_unique<GDXDX11RenderBackend>(std::move(dxContext));
    auto rendererOwned = std::make_unique<GDXECSRenderer>(std::move(backendOwned));
    GDXECSRenderer* renderer = rendererOwned.get();
    renderer->SetClearColor(0.04f, 0.05f, 0.08f);

    GDXECSRenderer::DebugCullingOptions dbg{};
    dbg.enabled = true;
    dbg.drawMainVisibleBounds = false;
    dbg.drawShadowVisibleBounds = false;
    dbg.drawRttVisibleBounds = false;
    dbg.drawMainFrustum =  false;
    dbg.drawShadowFrustum = false;
    dbg.logStats = false;
    dbg.logEveryNFrames = 60u;
    dbg.boundsAlpha = 0.22f;
    dbg.frustumAlpha = 0.55f;
    renderer->SetDebugCullingOptions(dbg);

    KROMEngine engine(std::move(windowOwned), std::move(rendererOwned), events);
    if (!engine.Initialize())
    {
        Debug::LogError("engine initialize failed");
        return 4;
    }


    //renderer->DisableBloom();
    //renderer->DisableToneMapping();
    //renderer->DisableFXAA();
    renderer->SetDepthDebugView(false);
    renderer->SetNormalDebugView(false);


    auto app = std::make_unique<BoneAnimationTestScene>(*renderer);
    app->Init();

    renderer->SetTickCallback([&](float dt)
        {
            app->Update(dt);
        });

    engine.SetEventCallback([&](const Event& e)
        {
            app->OnEvent(e, engine);
        });

    engine.Run();
    app->Shutdown();
    engine.Shutdown();
    return 0;
}