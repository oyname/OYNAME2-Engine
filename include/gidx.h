#pragma once

// ---------------------------------------------------------------------------
// gidx.h  flacher API-Wrapper fuer die OYNAME2 Engine.
//
// Typische Nutzung (main.cpp):
//
//   if (!Engine::Graphics(1280, 720, "Mein Spiel"))
//       return 1;
//
//   LPENTITY cam, mesh;  LPMATERIAL mat;
//   Engine::CreateCamera(&cam);
//   Engine::CreateMesh(&mesh, Engine::Cube());
//   Engine::CreateMaterial(&mat, 1.0f, 0.2f, 0.1f);
//   Engine::AssignMaterial(mesh, mat);
//
//   Engine::Run([](float dt)
//   {
//       Engine::TurnEntity(mesh, 0, dt * 45.0f, 0);
//   });
//
// ---------------------------------------------------------------------------

#include "gdx_render.h"
#include "Core/GDXMathOps.h"
#include "gdx_engine.h"
#include "gdx_platform_win32.h"
#include "gdx_backend_dx11.h"
#include "SubmeshData.h"
#include "SubmeshBuilder.h"
#include "MeshUtilities.h"
#include "WindowDesc.h"
#include "Core/Debug.h"
#include "Particles/GDXParticleTypes.h"

#include <functional>
#include <variant>
#include <memory>
#include <string>

// ---------------------------------------------------------------------------
// Typ-Aliase  identisches Nutzungsgefhl wie OYNAME
// ---------------------------------------------------------------------------
using LPENTITY = EntityID;
using LPMATERIAL = MaterialHandle;
using LPTEXTURE = TextureHandle;
using LPMESH = MeshHandle;

static constexpr LPENTITY   NULL_LPENTITY = EntityID{};
static constexpr LPMATERIAL NULL_MATERIAL = MaterialHandle{};
static constexpr LPTEXTURE  NULL_TEXTURE = TextureHandle{};
static constexpr LPMESH     NULL_MESH = MeshHandle{};

namespace Engine
{

    // ---------------------------------------------------------------------------
    // Interne Globals
    // ---------------------------------------------------------------------------
    namespace _
    {
        inline GDXECSRenderer* renderer = nullptr;
        inline KROMEngine* engine = nullptr;
        inline bool            running = false;

        // Ownership  lebt so lange die App luft
        inline std::unique_ptr<GDXEventQueue>  eventQueue;
        inline std::unique_ptr<KROMEngine>      engineOwned;
        inline GDXECSRenderer* rendererRaw = nullptr;

        inline std::function<void(float)>        userTickCallback;
        inline std::function<void(const Event&)> userEventCallback;

        inline void OnEvent(const Event& e);  // forward
    }

    // ---------------------------------------------------------------------------
    // Renderer-Konstanten
    // ---------------------------------------------------------------------------
    namespace Renderer
    {
        constexpr int DX11 = 0;
        constexpr int OpenGL = 1;
        constexpr int DX12 = 2;
        constexpr int Vulkan = 3;
    }

    // ---------------------------------------------------------------------------
    // Graphics  startet Fenster + gewhltes Backend + Engine in einer Zeile.
    //
    //   Engine::Graphics(Renderer::DX11, 1280, 720, "Mein Spiel")
    //
    // Gibt false zurck wenn die Initialisierung fehlschlgt.
    // ---------------------------------------------------------------------------
    inline bool Graphics(
        int         backend,
        int         width,
        int         height,
        const char* title = "OYNAME Engine",
        float       clearR = 0.04f,
        float       clearG = 0.04f,
        float       clearB = 0.10f,
        bool        resizable = true,
        bool        borderless = false,
        bool        fullscreen = false)
    {
        _::eventQueue = std::make_unique<GDXEventQueue>();

        WindowDesc desc;
        desc.width = width;
        desc.height = height;
        desc.title = title;
        desc.resizable = resizable;
        desc.borderless = borderless;
        desc.fullscreen = fullscreen;

        auto window = std::make_unique<GDXWin32Window>(desc, *_::eventQueue);
        if (!window->Create())
        {
            DBERROR(GDX_SRC_LOC, "Engine::Graphics: Fenster konnte nicht erstellt werden");
            return false;
        }

        // -- Backend aufbauen ------------------------------------------------
        std::unique_ptr<IGDXRenderBackend> renderBackend;

        if (backend == Renderer::DX11)
        {
            auto adapters = GDXWin32DX11ContextFactory::EnumerateAdapters();
            if (adapters.empty())
            {
                DBERROR(GDX_SRC_LOC, "Engine::Graphics: kein DX11-Adapter gefunden");
                return false;
            }
            GDXWin32DX11ContextFactory dx11Factory;
            auto dxContext = dx11Factory.Create(*window,
                GDXWin32DX11ContextFactory::FindBestAdapter(adapters));
            if (!dxContext)
            {
                DBERROR(GDX_SRC_LOC, "Engine::Graphics: DX11 Context fehlgeschlagen");
                return false;
            }
            renderBackend = std::make_unique<GDXDX11RenderBackend>(std::move(dxContext));
        }
        else
        {
            DBERROR(GDX_SRC_LOC, "Engine::Graphics: Backend noch nicht implementiert");
            return false;
        }

        // -- Renderer + Engine -----------------------------------------------
        auto renderer = std::make_unique<GDXECSRenderer>(std::move(renderBackend));
        renderer->SetClearColor(clearR, clearG, clearB);
        _::rendererRaw = renderer.get();

        _::engineOwned = std::make_unique<KROMEngine>(
            std::move(window), std::move(renderer), *_::eventQueue);

        if (!_::engineOwned->Initialize())
        {
            DBERROR(GDX_SRC_LOC, "Engine::Graphics: Engine-Initialisierung fehlgeschlagen");
            return false;
        }

        // Vollbild nach Initialize() aktivieren
        if (fullscreen)
            _::rendererRaw->SetFullscreen(true);

        _::renderer = _::rendererRaw;
        _::engine = _::engineOwned.get();
        _::running = true;
        return true;
    }

    // ---------------------------------------------------------------------------
    // Interner Event-Handler
    // ---------------------------------------------------------------------------
    namespace _
    {
        inline void OnEvent(const Event& e)
        {
            std::visit([](auto&& ev)
                {
                    using T = std::decay_t<decltype(ev)>;
                    if constexpr (std::is_same_v<T, KeyPressedEvent>)
                    {
                        if (ev.key == Key::Escape)
                        {
                            DBLOG(GDX_SRC_LOC, "Engine: ESC  beende Anwendung");
                            _::running = false;
                            if (_::engine) _::engine->Shutdown();
                        }
                    }
                    else if constexpr (std::is_same_v<T, QuitEvent>)
                    {
                        _::running = false;
                        if (_::engine) _::engine->Shutdown();
                    }
                }, e);

            if (_::userEventCallback)
                _::userEventCallback(e);
        }
    }

    // ---------------------------------------------------------------------------
    // Bind  alternativ zu Graphics(), wenn Fenster/Engine extern erstellt wurden.
    // ---------------------------------------------------------------------------
    inline void Bind(GDXECSRenderer& r, KROMEngine& e)
    {
        _::renderer = &r;
        _::engine = &e;
        _::running = true;
    }

    // ---------------------------------------------------------------------------
    // OnUpdate / OnEvent  Callbacks registrieren, dann Run() aufrufen.
    //
    //   int main()
    //   {
    //       Engine::Graphics(Renderer::DX11, 1280, 720, "Titel");
    //
    //       Init();                      // direkt aufrufen
    //       Engine::OnUpdate(Update);      // optional
    //       Engine::OnEvent(OnEvent);      // optional
    //
    //       Engine::Run();
    //   }
    // ---------------------------------------------------------------------------
    using TickFn = std::function<void(float)>;
    using EventFn = std::function<void(const Event&)>;

    inline void OnUpdate(TickFn fn) { _::userTickCallback = std::move(fn); }
    inline void OnEvent(EventFn fn) { _::userEventCallback = std::move(fn); }

    inline void Run()
    {
        if (!_::renderer || !_::engine)
        {
            DBERROR(GDX_SRC_LOC, "Engine::Run: Graphics() wurde nicht aufgerufen");
            return;
        }
        _::renderer->SetTickCallback([](float dt) {
            if (_::userTickCallback) _::userTickCallback(dt);
            });
        _::engine->SetEventCallback([](const Event& e) { _::OnEvent(e); });
        _::engine->Run();
    }

    // ---------------------------------------------------------------------------
    // Quit / Shutdown
    // ---------------------------------------------------------------------------
    inline void Quit()
    {
        _::running = false;
        if (_::engine) _::engine->Shutdown();
    }

    inline bool AppRunning() { return _::running; }

    // ---------------------------------------------------------------------------
    // Frame / DeltaTime  OYNAME-Stil: eigene while-Schleife in main()
    //
    //   Init();
    //   while (Engine::Frame())
    //       Update(Engine::DeltaTime());
    // ---------------------------------------------------------------------------
    inline float DeltaTime() { return _::engine ? _::engine->GetDeltaTime() : 0.0f; }

    inline bool Frame()
    {
        if (!_::engine) return false;

        const bool ok = _::engine->Step();
        if (!ok)
        {
            _::running = false;
            return false;
        }

        return true;
    }

    namespace Input
    {
        inline bool KeyDown(Key key)
        {
            return GDXInput::KeyDown(key);
        }

        inline bool KeyHit(Key key)
        {
            return GDXInput::KeyHit(key);
        }

        inline bool KeyReleased(Key key)
        {
            return GDXInput::KeyReleased(key);
        }
    }



    inline LPMESH UploadMeshAsset(MeshAssetResource asset, const MeshBuildSettings& settings = {})
    {
        if (!_::renderer)
        {
            DBERROR(GDX_SRC_LOC, "Engine::UploadMeshAsset: Bind()/Graphics() fehlt");
            return NULL_MESH;
        }

        if (!MeshProcessor::Process(asset, settings))
        {
            DBERROR(GDX_SRC_LOC, "Engine::UploadMeshAsset: Mesh-Verarbeitung/Validierung fehlgeschlagen");
            return NULL_MESH;
        }

        return _::renderer->UploadMesh(std::move(asset));
    }

    inline LPMESH UploadSubmesh(SubmeshData submesh, const char* debugName = "Submesh", const MeshBuildSettings& settings = {})
    {
        MeshAssetResource asset;
        asset.debugName = debugName ? debugName : "Submesh";
        asset.AddSubmesh(std::move(submesh));
        return UploadMeshAsset(std::move(asset), settings);
    }

    // ===========================================================================
    // BUILTIN MESH HELPER
    // ===========================================================================

    inline LPMESH Sphere(float radius = 0.5f, uint32_t slices = 24, uint32_t stacks = 16)
    {
        MeshBuildSettings settings;
        settings.computeTangentsIfPossible = true;
        return UploadSubmesh(BuiltinMeshes::Sphere(radius, slices, stacks), "Sphere", settings);
    }

    inline LPMESH Cube()
    {
        MeshBuildSettings settings;
        settings.computeTangentsIfPossible = true;
        return UploadSubmesh(BuiltinMeshes::Cube(), "Cube", settings);
    }

    inline LPMESH Octahedron(float radius = 0.5f)
    {
        MeshBuildSettings settings;
        settings.computeTangentsIfPossible = true;
        return UploadSubmesh(BuiltinMeshes::Octahedron(radius), "Octahedron", settings);
    }


    // ===========================================================================
    // ENTITY ERSTELLEN
    // ===========================================================================

    // ---------------------------------------------------------------------------
    // Interner Helfer: erstellt Entity mit den Basis-Komponenten die
    // fast jede Entity braucht.
    // ---------------------------------------------------------------------------
    namespace _
    {
        inline RenderableComponent* EnsureRenderable(Registry& reg, EntityID e)
        {
            auto* renderable = reg.Get<RenderableComponent>(e);
            if (!renderable)
                renderable = &reg.Add<RenderableComponent>(e);
            return renderable;
        }

        inline void MarkRenderableDirty(RenderableComponent* renderable)
        {
            if (!renderable) return;
            renderable->dirty = true;
            ++renderable->stateVersion;
        }


        inline LPENTITY MakeEntity(const char* tag = "")
        {
            Registry& reg = _::renderer->GetRegistry();
            LPENTITY e = reg.CreateEntity();
            reg.Add<TransformComponent>(e);
            reg.Add<WorldTransformComponent>(e);
            if (tag && *tag)
                reg.Add<TagComponent>(e, std::string(tag));
            return e;
        }
    }

    // ---------------------------------------------------------------------------
    // CreateMesh  erstellt eine Mesh-Entity und weist optional ein Mesh zu.
    // ---------------------------------------------------------------------------
    inline void CreateMesh(LPENTITY* out, LPMESH mesh = NULL_MESH, const char* tag = "")
    {
        if (!out) { DBERROR(GDX_SRC_LOC, "Engine::CreateMesh: out ist nullptr"); return; }
        if (!_::renderer) { DBERROR(GDX_SRC_LOC, "Engine::CreateMesh: Bind() fehlt"); return; }

        Registry& reg = _::renderer->GetRegistry();
        *out = _::MakeEntity(tag);

        if (mesh.value != 0)
        {
            auto* renderable = _::EnsureRenderable(reg, *out);
            renderable->mesh = mesh;
            renderable->submeshIndex = 0u;
            _::MarkRenderableDirty(renderable);
        }

        reg.Add<VisibilityComponent>(*out);
    }

    // ---------------------------------------------------------------------------
    // CreateCamera  erstellt eine Kamera-Entity und macht sie aktiv.
    // ---------------------------------------------------------------------------
    inline void CreateCamera(LPENTITY* out,
        float fovDeg = 60.0f,
        float nearPlane = 0.1f,
        float farPlane = 1000.0f,
        const char* tag = "Camera")
    {
        if (!out) { DBERROR(GDX_SRC_LOC, "Engine::CreateCamera: out ist nullptr"); return; }
        if (!_::renderer) { DBERROR(GDX_SRC_LOC, "Engine::CreateCamera: Bind() fehlt"); return; }

        Registry& reg = _::renderer->GetRegistry();
        *out = _::MakeEntity(tag);

        CameraComponent cam;
        cam.fovDeg = fovDeg;
        cam.nearPlane = nearPlane;
        cam.farPlane = farPlane;
        reg.Add<CameraComponent>(*out, cam);
        reg.Add<ActiveCameraTag>(*out);
    }

    inline void CreateParticleEmitter(LPENTITY* out,
        const GDXParticleEmitterComponent& emitter,
        bool playOnStart = true,
        bool oneShot = false,
        const char* tag = "ParticleEmitter")
    {
        if (!out) { DBERROR(GDX_SRC_LOC, "Engine::CreateParticleEmitter: out ist nullptr"); return; }
        if (!_::renderer) { DBERROR(GDX_SRC_LOC, "Engine::CreateParticleEmitter: Bind() fehlt"); return; }

        Registry& reg = _::renderer->GetRegistry();
        *out = _::MakeEntity(tag);
        reg.Add<GDXParticleEmitterComponent>(*out, emitter);
        reg.Add<ParticleEmitterStateComponent>(*out);

        ParticleEmitterControlComponent control;
        control.playOnStart = playOnStart;
        control.requestedActive = playOnStart;
        control.oneShot = oneShot;
        reg.Add<ParticleEmitterControlComponent>(*out, control);
    }

    inline void StartParticleEmitter(LPENTITY e)
    {
        if (!_::renderer) return;
        auto& reg = _::renderer->GetRegistry();
        if (auto* c = reg.Get<ParticleEmitterControlComponent>(e))
        {
            c->startRequested = true;
            c->requestedActive = true;
        }
        else if (auto* em = reg.Get<GDXParticleEmitterComponent>(e))
        {
            em->active = true;
        }
    }

    inline void StopParticleEmitter(LPENTITY e)
    {
        if (!_::renderer) return;
        auto& reg = _::renderer->GetRegistry();
        if (auto* c = reg.Get<ParticleEmitterControlComponent>(e))
        {
            c->stopRequested = true;
            c->requestedActive = false;
        }
        else if (auto* em = reg.Get<GDXParticleEmitterComponent>(e))
        {
            em->active = false;
        }
    }

    inline void RestartParticleEmitter(LPENTITY e)
    {
        if (!_::renderer) return;
        auto& reg = _::renderer->GetRegistry();
        if (auto* c = reg.Get<ParticleEmitterControlComponent>(e))
        {
            c->restartRequested = true;
            c->requestedActive = true;
        }
        else if (auto* em = reg.Get<GDXParticleEmitterComponent>(e))
        {
            em->active = true;
        }
    }

    inline void PauseParticleEmitter(LPENTITY e)
    {
        if (!_::renderer) return;
        auto& reg = _::renderer->GetRegistry();
        if (auto* c = reg.Get<ParticleEmitterControlComponent>(e))
        {
            c->pauseRequested = true;
            c->resumeRequested = false;
            c->paused = true;
            c->requestedActive = true;
        }
        else if (auto* em = reg.Get<GDXParticleEmitterComponent>(e))
        {
            em->paused = true;
            em->active = false;
        }
    }

    inline void ResumeParticleEmitter(LPENTITY e)
    {
        if (!_::renderer) return;
        auto& reg = _::renderer->GetRegistry();
        if (auto* c = reg.Get<ParticleEmitterControlComponent>(e))
        {
            c->resumeRequested = true;
            c->pauseRequested = false;
            c->paused = false;
            c->requestedActive = true;
        }
        else if (auto* em = reg.Get<GDXParticleEmitterComponent>(e))
        {
            em->paused = false;
            em->active = true;
        }
    }

    inline bool IsParticleEmitterPaused(LPENTITY e)
    {
        if (!_::renderer) return false;
        auto& reg = _::renderer->GetRegistry();
        if (auto* c = reg.Get<ParticleEmitterControlComponent>(e))
            return c->paused;
        if (auto* em = reg.Get<GDXParticleEmitterComponent>(e))
            return em->paused;
        return false;
    }

    inline bool IsParticleEmitterPlaying(LPENTITY e)
    {
        if (!_::renderer) return false;
        auto& reg = _::renderer->GetRegistry();
        if (auto* em = reg.Get<GDXParticleEmitterComponent>(e))
            return em->active && !em->paused;
        return false;
    }

    inline bool IsParticleEmitterFinished(LPENTITY e)
    {
        if (!_::renderer) return false;
        auto& reg = _::renderer->GetRegistry();
        auto* em = reg.Get<GDXParticleEmitterComponent>(e);
        if (!em) return false;
        auto* c = reg.Get<ParticleEmitterControlComponent>(e);
        if (c && c->restartRequested)
            return false;
        return !em->active && !em->paused && em->maxLife > 0 && em->elapsedMs >= em->maxLife;
    }

    // ---------------------------------------------------------------------------
    // CreateLight  erstellt eine Licht-Entity.
    // ---------------------------------------------------------------------------
    inline void CreateLight(LPENTITY* out,
        LightKind kind = LightKind::Directional,
        float r = 1.0f, float g = 1.0f, float b = 1.0f,
        const char* tag = "Light")
    {
        if (!out) { DBERROR(GDX_SRC_LOC, "Engine::CreateLight: out ist nullptr"); return; }
        if (!_::renderer) { DBERROR(GDX_SRC_LOC, "Engine::CreateLight: Bind() fehlt"); return; }

        Registry& reg = _::renderer->GetRegistry();
        *out = _::MakeEntity(tag);

        LightComponent lc;
        lc.kind = kind;
        lc.diffuseColor = { r, g, b, 1.0f };
        reg.Add<LightComponent>(*out, lc);
    }


    // ===========================================================================
    // TRANSFORM
    // ===========================================================================

    inline void PositionEntity(LPENTITY e, float x, float y, float z)
    {
        if (!_::renderer) return;
        auto* tc = _::renderer->GetRegistry().Get<TransformComponent>(e);
        if (!tc) { DBERROR(GDX_SRC_LOC, "Engine::PositionEntity: kein TransformComponent"); return; }
        tc->localPosition = { x, y, z };
        TransformSystem::MarkDirty(_::renderer->GetRegistry(), e);
    }

    inline void RotateEntity(LPENTITY e, float pitchDeg, float yawDeg, float rollDeg)
    {
        if (!_::renderer) return;
        auto* tc = _::renderer->GetRegistry().Get<TransformComponent>(e);
        if (!tc) { DBERROR(GDX_SRC_LOC, "Engine::RotateEntity: kein TransformComponent"); return; }
        tc->SetEulerDeg(pitchDeg, yawDeg, rollDeg);
        TransformSystem::MarkDirty(_::renderer->GetRegistry(), e);
    }

    // TurnEntity  dreht relativ zur aktuellen Rotation (akkumuliert).
    inline void TurnEntity(LPENTITY e, float pitchDeg, float yawDeg, float rollDeg)
    {
        if (!_::renderer) return;
        auto* tc = _::renderer->GetRegistry().Get<TransformComponent>(e);
        if (!tc) { DBERROR(GDX_SRC_LOC, "Engine::TurnEntity: kein TransformComponent"); return; }

        const Float4 delta = GDX::QuaternionFromEulerDeg(pitchDeg, yawDeg, rollDeg);
        tc->localRotation = GDX::QuaternionMultiply(tc->localRotation, delta);
        TransformSystem::MarkDirty(_::renderer->GetRegistry(), e);
    }

    inline void ScaleEntity(LPENTITY e, float x, float y, float z)
    {
        if (!_::renderer) return;
        auto* tc = _::renderer->GetRegistry().Get<TransformComponent>(e);
        if (!tc) { DBERROR(GDX_SRC_LOC, "Engine::ScaleEntity: kein TransformComponent"); return; }
        tc->localScale = { x, y, z };
        TransformSystem::MarkDirty(_::renderer->GetRegistry(), e);
    }

    // LookAt  dreht die Entity so dass sie auf ein Ziel zeigt.
    inline void LookAt(LPENTITY e, float tx, float ty, float tz)
    {
        if (!_::renderer) return;
        auto* tc = _::renderer->GetRegistry().Get<TransformComponent>(e);
        if (!tc) { DBERROR(GDX_SRC_LOC, "Engine::LookAt: kein TransformComponent"); return; }

        const Float3 pos = tc->localPosition;
        const Float3 target = { tx, ty, tz };

        Float3 forward = GDX::Normalize3(
            GDX::Subtract(target, pos),
            { 0.0f, 0.0f, 1.0f });

        Float3 up = { 0.0f, 1.0f, 0.0f };
        if (std::abs(GDX::Dot3(forward, up)) > 0.999f)
            up = { 0.0f, 0.0f, 1.0f };

        const Float3 right = GDX::Normalize3(
            GDX::Cross(up, forward),
            { 1.0f, 0.0f, 0.0f });
        const Float3 newUp = GDX::Normalize3(
            GDX::Cross(forward, right),
            { 0.0f, 1.0f, 0.0f });

        tc->localRotation = GDX::QuaternionFromBasis(right, newUp, forward);
        TransformSystem::MarkDirty(_::renderer->GetRegistry(), e);
    }

    // ---------------------------------------------------------------------------
    // Position lesen
    // ---------------------------------------------------------------------------
    inline float EntityX(LPENTITY e)
    {
        if (!_::renderer) return 0.0f;
        auto* wt = _::renderer->GetRegistry().Get<WorldTransformComponent>(e);
        return wt ? wt->matrix._41 : 0.0f;
    }

    inline float EntityY(LPENTITY e)
    {
        if (!_::renderer) return 0.0f;
        auto* wt = _::renderer->GetRegistry().Get<WorldTransformComponent>(e);
        return wt ? wt->matrix._42 : 0.0f;
    }

    inline float EntityZ(LPENTITY e)
    {
        if (!_::renderer) return 0.0f;
        auto* wt = _::renderer->GetRegistry().Get<WorldTransformComponent>(e);
        return wt ? wt->matrix._43 : 0.0f;
    }


    // ===========================================================================
    // MESH & MATERIAL ZUWEISEN
    // ===========================================================================

    inline void AssignMesh(LPENTITY e, LPMESH mesh, uint32_t submeshIndex = 0u)
    {
        if (!_::renderer) return;
        Registry& reg = _::renderer->GetRegistry();
        auto* renderable = _::EnsureRenderable(reg, e);
        renderable->mesh = mesh;
        renderable->submeshIndex = submeshIndex;
        _::MarkRenderableDirty(renderable);
    }

    inline void AssignMaterial(LPENTITY e, LPMATERIAL mat)
    {
        if (!_::renderer) return;
        Registry& reg = _::renderer->GetRegistry();
        auto* renderable = _::EnsureRenderable(reg, e);
        renderable->material = mat;
        _::MarkRenderableDirty(renderable);
    }


    // ===========================================================================
    // MATERIAL ERSTELLEN & KONFIGURIEREN
    // ===========================================================================

    inline void CreateMaterial(LPMATERIAL* out,
        float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
    {
        if (!out) return;
        if (!_::renderer) { DBERROR(GDX_SRC_LOC, "Engine::CreateMaterial: Bind() fehlt"); return; }
        *out = _::renderer->CreateMaterial(MaterialResource::FlatColor(r, g, b, a));
    }

    inline void MaterialColor(LPMATERIAL mat, float r, float g, float b, float a = 1.0f)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) { DBERROR(GDX_SRC_LOC, "Engine::MaterialColor: ungltiger Handle"); return; }
        m->SetBaseColor(r, g, b, a);
    }

    inline void MaterialMetallic(LPMATERIAL mat, float metallic)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->SetMetallic(metallic);
    }

    inline void MaterialRoughness(LPMATERIAL mat, float roughness)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->SetRoughness(roughness);
    }

    inline void MaterialPBR(LPMATERIAL mat, float metallic, float roughness)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->SetMetallic(metallic);
        m->SetRoughness(roughness);
        m->SetShadingModel(MaterialShadingModel::PBR);
    }

    inline void MaterialEmissive(LPMATERIAL mat, float r, float g, float b, float intensity = 1.0f)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->SetEmissiveColor(r * intensity, g * intensity, b * intensity);
    }

    inline void MaterialLegacyPhong(LPMATERIAL mat, float specR, float specG, float specB, float shininess)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->SetLegacyPhong(specR, specG, specB, shininess);
    }

    // Setzt den Transparenzzustand explizit.
    // Für alpha-basierte Transparenz MaterialOpacity() bevorzugen —
    // das synchronisiert den Zustand über die Opacity.
    inline void MaterialTransparent(LPMATERIAL mat, bool enabled)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->SetTransparent(enabled);
    }

    // Setzt nur opacity [0..1]. Transparenz wird separat ueber MaterialTransparent/BlendMode gesteuert.
    inline void MaterialOpacity(LPMATERIAL mat, float opacity)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->SetOpacity(opacity);
    }

    inline void MaterialNormalScale(LPMATERIAL mat, float scale)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->SetNormalScale(scale);
    }

    inline void MaterialReceiveShadows(LPMATERIAL mat, bool enabled)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->SetReceiveShadows(enabled);
    }

    inline void MaterialShadowCullBackfaces(LPMATERIAL mat, bool enabled)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->SetShadowCullMode(enabled ? MaterialShadowCullMode::Back
            : MaterialShadowCullMode::None);
    }

    inline void MaterialShadowCullAuto(LPMATERIAL mat)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->SetShadowCullMode(MaterialShadowCullMode::Auto);
    }


    // ===========================================================================
    // TEXTUREN
    // ===========================================================================

    inline void LoadTexture(LPTEXTURE* out, const wchar_t* path, bool isSRGB = true)
    {
        if (!out) return;
        if (!_::renderer) { DBERROR(GDX_SRC_LOC, "Engine::LoadTexture: Bind() fehlt"); return; }
        *out = _::renderer->LoadTexture(path, isSRGB);
    }

    inline void MaterialSetAlbedo(LPMATERIAL mat, LPTEXTURE tex)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->SetTexture(MaterialTextureSlot::Albedo, tex, MaterialTextureUVSet::UV0);
    }

    inline void MaterialSetNormal(LPMATERIAL mat, LPTEXTURE tex)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->SetTexture(MaterialTextureSlot::Normal, tex, MaterialTextureUVSet::UV0);
    }

    inline void MaterialSetORM(LPMATERIAL mat, LPTEXTURE tex)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->SetTexture(MaterialTextureSlot::ORM, tex, MaterialTextureUVSet::UV0);
    }

    inline void MaterialSetEmissiveTex(LPMATERIAL mat, LPTEXTURE tex)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->SetTexture(MaterialTextureSlot::Emissive, tex, MaterialTextureUVSet::UV0);
    }


    // ===========================================================================
    // LICHT
    // ===========================================================================

    inline void LightColor(LPENTITY e, float r, float g, float b)
    {
        if (!_::renderer) return;
        auto* lc = _::renderer->GetRegistry().Get<LightComponent>(e);
        if (!lc) { DBERROR(GDX_SRC_LOC, "Engine::LightColor: kein LightComponent"); return; }
        lc->diffuseColor = { r, g, b, 1.0f };
    }

    inline void LightIntensity(LPENTITY e, float intensity)
    {
        if (!_::renderer) return;
        auto* lc = _::renderer->GetRegistry().Get<LightComponent>(e);
        if (!lc) return;
        lc->intensity = intensity;
    }

    inline void LightRadius(LPENTITY e, float radius)
    {
        if (!_::renderer) return;
        auto* lc = _::renderer->GetRegistry().Get<LightComponent>(e);
        if (!lc) return;
        lc->radius = radius;
    }

    inline void LightCastShadows(LPENTITY e, bool enabled,
        float orthoSize = 50.0f, float nearZ = 0.1f, float farZ = 1000.0f)
    {
        if (!_::renderer) return;
        auto* lc = _::renderer->GetRegistry().Get<LightComponent>(e);
        if (!lc) return;
        lc->castShadows = enabled;
        lc->shadowOrthoSize = orthoSize;
        lc->shadowNear = nearZ;
        lc->shadowFar = farZ;
    }

    inline void LightSpotCone(LPENTITY e, float innerDeg, float outerDeg)
    {
        if (!_::renderer) return;
        auto* lc = _::renderer->GetRegistry().Get<LightComponent>(e);
        if (!lc) return;
        lc->innerConeAngle = innerDeg;
        lc->outerConeAngle = outerDeg;
    }


    // ===========================================================================
    // SICHTBARKEIT & FLAGS
    // ===========================================================================

    inline void ShowEntity(LPENTITY e, bool visible)
    {
        if (!_::renderer) return;
        Registry& reg = _::renderer->GetRegistry();
        auto* vc = reg.Get<VisibilityComponent>(e);
        if (!vc) return;
        vc->visible = visible;
        vc->dirty = true;
        ++vc->stateVersion;
    }

    inline void EntityActive(LPENTITY e, bool active)
    {
        if (!_::renderer) return;
        Registry& reg = _::renderer->GetRegistry();
        auto* vc = reg.Get<VisibilityComponent>(e);
        if (!vc) return;
        vc->active = active;
        vc->dirty = true;
        ++vc->stateVersion;
    }

    inline void EntityCastShadows(LPENTITY e, bool enabled)
    {
        if (!_::renderer) return;
        Registry& reg = _::renderer->GetRegistry();
        auto* vc = reg.Get<VisibilityComponent>(e);
        if (vc)
        {
            vc->castShadows = enabled;
            vc->dirty = true;
            ++vc->stateVersion;
        }

    }

    inline void EntityLayer(LPENTITY e, uint32_t layerMask)
    {
        if (!_::renderer) return;
        Registry& reg = _::renderer->GetRegistry();
        auto* vc = reg.Get<VisibilityComponent>(e);
        if (!vc) return;
        vc->layerMask = layerMask;
        vc->dirty = true;
        ++vc->stateVersion;
    }

    inline void EntityReceiveShadows(LPENTITY e, bool enabled)
    {
        if (!_::renderer) return;
        Registry& reg = _::renderer->GetRegistry();
        auto* vc = reg.Get<VisibilityComponent>(e);
        if (!vc) return;
        vc->receiveShadows = enabled;
        vc->dirty = true;
        ++vc->stateVersion;
    }


    // ===========================================================================
    // SZENE
    // ===========================================================================

    inline void SetAmbient(float r, float g, float b)
    {
        if (!_::renderer) return;
        _::renderer->SetSceneAmbient(r, g, b);
    }

    // Konfiguriert Engine-Default-Shader (VS/PS/Flags).
    // Muss vor Engine::Graphics() aufgerufen werden.
    inline void SetShaderConfig(const ShaderPathConfig& config)
    {
        if (!_::renderer) return;
        _::renderer->SetShaderConfig(config);
    }

    inline bool SupportsOcclusionCulling()
    {
        return _::renderer ? _::renderer->SupportsOcclusionCulling() : false;
    }

    inline void SetOcclusionCulling(bool enabled)
    {
        if (!_::renderer) return;
        _::renderer->SetOcclusionCulling(enabled);
    }

    inline bool SetFullscreen(bool fullscreen)
    {
        return _::renderer ? _::renderer->SetFullscreen(fullscreen) : false;
    }

    inline bool IsFullscreen()
    {
        return _::renderer ? _::renderer->IsFullscreen() : false;
    }

    inline void SetClearColor(float r, float g, float b, float a = 1.0f)
    {
        if (!_::renderer) return;
        _::renderer->SetClearColor(r, g, b, a);
    }

    inline void DestroyEntity(LPENTITY e)
    {
        if (!_::renderer) return;
        _::renderer->GetRegistry().DestroyEntity(e);
    }

} // namespace Engine
