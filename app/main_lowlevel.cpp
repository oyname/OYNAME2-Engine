#include "GDXEngine.h"
#include "GDXEventQueue.h"
#include "WindowDesc.h"
#include "GDXWin32Window.h"
#include "GDXWin32DX11ContextFactory.h"
#include "Debug.h"

#include "GDXECSRenderer.h"
#include "GDXDX11RenderBackend.h"
#include "ECSGame.h"

#include <memory>

int main()
{
    GDXEventQueue events;

    WindowDesc desc;
    desc.width = 1280;
    desc.height = 720;
    desc.title = "GIDX - ECS Render Test | ESC: Beenden | C: Kamera-Orbit";
    desc.resizable = true;

    auto windowOwned = std::make_unique<GDXWin32Window>(desc, events);
    if (!windowOwned->Create())
    {
        Debug::LogError("main.cpp: Fenster konnte nicht erstellt werden");
        return 1;
    }

    GDXWin32Window* windowRaw = windowOwned.get();

    auto adapters = GDXWin32DX11ContextFactory::EnumerateAdapters();
    if (adapters.empty())
    {
        Debug::LogError("main.cpp: kein DX11-Adapter gefunden");
        return 2;
    }

    const unsigned int adapterIdx =
        GDXWin32DX11ContextFactory::FindBestAdapter(adapters);

    Debug::Log("main.cpp: DX11 adapter ", adapterIdx,
        " [", adapters[adapterIdx].name, "]");

    GDXWin32DX11ContextFactory dx11Factory;
    auto dxContext = dx11Factory.Create(*windowRaw, adapterIdx);
    if (!dxContext)
    {
        Debug::LogError("main.cpp: DX11 Context konnte nicht erstellt werden");
        return 3;
    }

    auto backendOwned = std::make_unique<GDXDX11RenderBackend>(std::move(dxContext));
    auto rendererOwned = std::make_unique<GDXECSRenderer>(std::move(backendOwned));
    GDXECSRenderer* renderer = rendererOwned.get();

    renderer->SetClearColor(0.04f, 0.04f, 0.10f);

    GDXEngine engine(std::move(windowOwned), std::move(rendererOwned), events);

    if (!engine.Initialize())
    {
        Debug::LogError("main.cpp: Engine-Initialisierung fehlgeschlagen");
        return 4;
    }

    ECSGame game(*renderer);
    game.Init();

    Debug::Log("main.cpp: Szene bereit - ",
        renderer->GetRegistry().EntityCount(), " Entities");

    renderer->SetTickCallback([&game](float dt)
    {
        game.Update(dt);
    });

    // Events ans Spiel weitergeben — ESC, Fenster schliessen etc.
    // Das Spiel entscheidet selbst ob es engine.Shutdown() aufruft.
    engine.SetEventCallback([&game, &engine](const Event& e)
    {
        game.OnEvent(e, engine);
    });

    engine.Run();
    engine.Shutdown();

    return 0;
}