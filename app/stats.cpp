#include "GDXEngine.h"
#include "GDXEventQueue.h"
#include "WindowDesc.h"
#include "GDXWin32Window.h"
#include "GDXWin32DX11ContextFactory.h"
#include "GDXDX11RenderBackend.h"
#include "GDXECSRenderer.h"
#include "Debug.h"

#include "Components.h"

#include <memory>
#include <variant>

class FrameStatsExample
{
public:
    explicit FrameStatsExample(GDXECSRenderer& renderer)
        : m_renderer(renderer)
    {
    }

    void Init()
    {
        Registry& reg = m_renderer.GetRegistry();

        // Kamera
        EntityID cam = reg.CreateEntity();
        reg.Add<TagComponent>(cam, "Camera");
        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 2.0f, -6.0f };
            tc.SetEulerDeg(10.0f, 0.0f, 0.0f);
            reg.Add<TransformComponent>(cam, tc);
        }
        reg.Add<WorldTransformComponent>(cam);
        {
            CameraComponent cc;
            cc.aspectRatio = 1280.0f / 720.0f;
            cc.farPlane = 500.0f;
            reg.Add<CameraComponent>(cam, cc);
        }
        reg.Add<ActiveCameraTag>(cam);

        // Licht
        EntityID light = reg.CreateEntity();
        reg.Add<TagComponent>(light, "Light");
        {
            TransformComponent tc;
            tc.SetEulerDeg(45.0f, 0.0f, 0.0f);
            reg.Add<TransformComponent>(light, tc);
        }
        reg.Add<WorldTransformComponent>(light);
        {
            LightComponent lc;
            lc.kind = LightKind::Directional;
            lc.intensity = 1.5f;
            lc.castShadows = true;
            reg.Add<LightComponent>(light, lc);
        }

        Debug::Log("framestats example: running");
    }

    void Update(float dt)
    {
        m_timeAccum += dt;
        ++m_frameCounter;

        if (m_timeAccum >= 1.0f)
        {
            const auto& stats = m_renderer.GetFrameStats();

            Debug::Log(
                "FrameStats | fps=", m_frameCounter,
                " drawCalls=", stats.drawCalls,
                " entities=", stats.entityCount,
                " dirtyTransforms=", stats.dirtyTransformCount
            );

            m_timeAccum = 0.0f;
            m_frameCounter = 0;
        }
    }

private:
    GDXECSRenderer& m_renderer;
    float m_timeAccum = 0.0f;
    int   m_frameCounter = 0;
};

int main()
{
    GDXEventQueue events;

    WindowDesc desc;
    desc.width = 1280;
    desc.height = 720;
    desc.title = "GIDX - FrameStats Example";
    desc.resizable = true;

    auto windowOwned = std::make_unique<GDXWin32Window>(desc, events);
    if (!windowOwned->Create()) return 1;

    auto adapters = GDXWin32DX11ContextFactory::EnumerateAdapters();
    if (adapters.empty()) return 2;

    GDXWin32DX11ContextFactory dx11Factory;
    auto dxContext = dx11Factory.Create(
        *windowOwned,
        GDXWin32DX11ContextFactory::FindBestAdapter(adapters));
    if (!dxContext) return 3;

    auto backendOwned = std::make_unique<GDXDX11RenderBackend>(std::move(dxContext));
    auto rendererOwned = std::make_unique<GDXECSRenderer>(std::move(backendOwned));
    GDXECSRenderer* renderer = rendererOwned.get();

    GDXEngine engine(std::move(windowOwned), std::move(rendererOwned), events);
    if (!engine.Initialize()) return 4;

    FrameStatsExample app(*renderer);
    app.Init();

    renderer->SetTickCallback([&](float dt)
        {
            app.Update(dt);
        });

    engine.Run();
    engine.Shutdown();
    return 0;
}