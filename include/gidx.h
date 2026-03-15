#pragma once

// ---------------------------------------------------------------------------
// gidx.h  flacher API-Wrapper fr die OYNAME2 Engine.
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

#include "GDXECSRenderer.h"
#include "GDXEngine.h"
#include "GDXInput.h"
#include "GDXEventQueue.h"
#include "GDXWin32Window.h"
#include "GDXWin32DX11ContextFactory.h"
#include "GDXDX11RenderBackend.h"
#include "Components.h"
#include "SubmeshData.h"
#include "MaterialResource.h"
#include "MeshAssetResource.h"
#include "SubmeshBuilder.h"
#include "MeshUtilities.h"
#include "MeshProcessor.h"
#include "WindowDesc.h"
#include "Events.h"
#include "Debug.h"

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
        inline GDXEngine* engine = nullptr;
        inline bool            running = false;

        // Ownership  lebt so lange die App luft
        inline std::unique_ptr<GDXEventQueue>  eventQueue;
        inline std::unique_ptr<GDXEngine>      engineOwned;
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
        bool        resizable = true)
    {
        _::eventQueue = std::make_unique<GDXEventQueue>();

        WindowDesc desc;
        desc.width = width;
        desc.height = height;
        desc.title = title;
        desc.resizable = resizable;
        desc.borderless = false;

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

        _::engineOwned = std::make_unique<GDXEngine>(
            std::move(window), std::move(renderer), *_::eventQueue);

        if (!_::engineOwned->Initialize())
        {
            DBERROR(GDX_SRC_LOC, "Engine::Graphics: Engine-Initialisierung fehlgeschlagen");
            return false;
        }

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
    inline void Bind(GDXECSRenderer& r, GDXEngine& e)
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
            reg.Add<MeshRefComponent>(*out, mesh, 0u);

        reg.Add<VisibilityComponent>(*out);
        reg.Add<ShadowCasterTag>(*out);
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
        tc->dirty = true;
    }

    inline void RotateEntity(LPENTITY e, float pitchDeg, float yawDeg, float rollDeg)
    {
        if (!_::renderer) return;
        auto* tc = _::renderer->GetRegistry().Get<TransformComponent>(e);
        if (!tc) { DBERROR(GDX_SRC_LOC, "Engine::RotateEntity: kein TransformComponent"); return; }
        tc->SetEulerDeg(pitchDeg, yawDeg, rollDeg);
    }

    // TurnEntity  dreht relativ zur aktuellen Rotation (akkumuliert).
    inline void TurnEntity(LPENTITY e, float pitchDeg, float yawDeg, float rollDeg)
    {
        if (!_::renderer) return;
        auto* tc = _::renderer->GetRegistry().Get<TransformComponent>(e);
        if (!tc) { DBERROR(GDX_SRC_LOC, "Engine::TurnEntity: kein TransformComponent"); return; }

        const float toRad = DirectX::XM_PI / 180.0f;
        DirectX::XMVECTOR delta = DirectX::XMQuaternionRotationRollPitchYaw(
            pitchDeg * toRad, yawDeg * toRad, rollDeg * toRad);
        DirectX::XMVECTOR current = DirectX::XMLoadFloat4(&tc->localRotation);
        DirectX::XMStoreFloat4(&tc->localRotation,
            DirectX::XMQuaternionMultiply(current, delta));
        tc->dirty = true;
    }

    inline void ScaleEntity(LPENTITY e, float x, float y, float z)
    {
        if (!_::renderer) return;
        auto* tc = _::renderer->GetRegistry().Get<TransformComponent>(e);
        if (!tc) { DBERROR(GDX_SRC_LOC, "Engine::ScaleEntity: kein TransformComponent"); return; }
        tc->localScale = { x, y, z };
        tc->dirty = true;
    }

    // LookAt  dreht die Entity so dass sie auf ein Ziel zeigt.
    inline void LookAt(LPENTITY e, float tx, float ty, float tz)
    {
        if (!_::renderer) return;
        auto* tc = _::renderer->GetRegistry().Get<TransformComponent>(e);
        if (!tc) { DBERROR(GDX_SRC_LOC, "Engine::LookAt: kein TransformComponent"); return; }

        DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&tc->localPosition);
        DirectX::XMVECTOR target = DirectX::XMVectorSet(tx, ty, tz, 1.0f);
        DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

        DirectX::XMVECTOR forward = DirectX::XMVector3Normalize(
            DirectX::XMVectorSubtract(target, pos));
        DirectX::XMVECTOR right = DirectX::XMVector3Normalize(
            DirectX::XMVector3Cross(up, forward));
        DirectX::XMVECTOR newUp = DirectX::XMVector3Cross(forward, right);

        DirectX::XMMATRIX rot;
        rot.r[0] = right;
        rot.r[1] = newUp;
        rot.r[2] = forward;
        rot.r[3] = DirectX::XMVectorSet(0, 0, 0, 1);

        DirectX::XMStoreFloat4(&tc->localRotation,
            DirectX::XMQuaternionRotationMatrix(rot));
        tc->dirty = true;
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
        if (reg.Has<MeshRefComponent>(e))
            reg.Get<MeshRefComponent>(e)->mesh = mesh;
        else
            reg.Add<MeshRefComponent>(e, mesh, submeshIndex);
    }

    inline void AssignMaterial(LPENTITY e, LPMATERIAL mat)
    {
        if (!_::renderer) return;
        Registry& reg = _::renderer->GetRegistry();
        if (reg.Has<MaterialRefComponent>(e))
            reg.Get<MaterialRefComponent>(e)->material = mat;
        else
            reg.Add<MaterialRefComponent>(e, mat);
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
        m->data.baseColor = { r, g, b, a };
        m->cpuDirty = true;
    }

    inline void MaterialMetallic(LPMATERIAL mat, float metallic)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->data.metallic = metallic;
        m->cpuDirty = true;
    }

    inline void MaterialRoughness(LPMATERIAL mat, float roughness)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->data.roughness = roughness;
        m->cpuDirty = true;
    }

    inline void MaterialPBR(LPMATERIAL mat, float metallic, float roughness)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->data.metallic = metallic;
        m->data.roughness = roughness;
        m->SetFlag(MF_SHADING_PBR, true);
        m->cpuDirty = true;
    }

    inline void MaterialEmissive(LPMATERIAL mat, float r, float g, float b, float intensity = 1.0f)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->data.emissiveColor = { r * intensity, g * intensity, b * intensity, 1.0f };
        m->SetFlag(MF_USE_EMISSIVE, true);
        m->cpuDirty = true;
    }

    inline void MaterialTransparent(LPMATERIAL mat, bool enabled)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->SetFlag(MF_TRANSPARENT, enabled);
        m->cpuDirty = true;
    }

    inline void MaterialNormalScale(LPMATERIAL mat, float scale)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->data.normalScale = scale;
        m->cpuDirty = true;
    }

    inline void MaterialReceiveShadows(LPMATERIAL mat, bool enabled)
    {
        if (!_::renderer) return;
        auto* m = _::renderer->GetMatStore().Get(mat);
        if (!m) return;
        m->data.receiveShadows = enabled ? 1.0f : 0.0f;
        m->cpuDirty = true;
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
        auto* vc = _::renderer->GetRegistry().Get<VisibilityComponent>(e);
        if (!vc) return;
        vc->visible = visible;
    }

    inline void EntityActive(LPENTITY e, bool active)
    {
        if (!_::renderer) return;
        auto* vc = _::renderer->GetRegistry().Get<VisibilityComponent>(e);
        if (!vc) return;
        vc->active = active;
    }

    inline void EntityCastShadows(LPENTITY e, bool enabled)
    {
        if (!_::renderer) return;
        Registry& reg = _::renderer->GetRegistry();
        auto* vc = reg.Get<VisibilityComponent>(e);
        if (vc) vc->castShadows = enabled;

        if (enabled && !reg.Has<ShadowCasterTag>(e))
            reg.Add<ShadowCasterTag>(e);
        else if (!enabled && reg.Has<ShadowCasterTag>(e))
            reg.Remove<ShadowCasterTag>(e);
    }

    inline void EntityLayer(LPENTITY e, uint32_t layerMask)
    {
        if (!_::renderer) return;
        auto* vc = _::renderer->GetRegistry().Get<VisibilityComponent>(e);
        if (!vc) return;
        vc->layerMask = layerMask;
    }

    inline void EntityReceiveShadows(LPENTITY e, bool enabled)
    {
        if (!_::renderer) return;
        auto* vc = _::renderer->GetRegistry().Get<VisibilityComponent>(e);
        if (!vc) return;
        vc->receiveShadows = enabled;
    }


    // ===========================================================================
    // SZENE
    // ===========================================================================

    inline void SetAmbient(float r, float g, float b)
    {
        if (!_::renderer) return;
        _::renderer->SetSceneAmbient(r, g, b);
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
#include "GDXDX12RenderBackend.h"
