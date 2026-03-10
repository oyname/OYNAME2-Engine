// =============================================================================
// test_step1.cpp — ECS Step 1 Testanwendung
//
// Testet:
//   [1] EntityID: Erstellung, Dekomposition, Generationssicherheit
//   [2] Registry: CreateEntity / DestroyEntity / IsAlive
//   [3] Slot-Recycling mit Generation-Check (stale ID Erkennung)
//   [4] Add / Get / Has / Remove von Components
//   [5] View<T...>: Iteration über alle Entities mit bestimmten Components
//   [6] View mit mehreren Component-Typen (TransformComponent + MeshComponent)
//   [7] Praxis-Szenario: kleine Szene mit Kamera, Lichtern, Meshes
//
// Kompilieren (MSVC Beispiel — keine weiteren Abhängigkeiten nötig):
//   cl /std:c++17 /EHsc /I ecs/include test_step1.cpp
//
// Kompilieren (GCC/Clang):
//   g++ -std=c++17 -I ecs/include test_step1.cpp -o test_step1
// =============================================================================

#include "Registry.h"
#include "Components.h"
#include "ECSTypes.h"

#include <iostream>
#include <cassert>
#include <string>
#include <sstream>

// ---------------------------------------------------------------------------
// Hilfsfunktionen
// ---------------------------------------------------------------------------

static int  s_passed = 0;
static int  s_failed = 0;

void Check(bool cond, const std::string& label)
{
    if (cond) {
        std::cout << "  [PASS] " << label << "\n";
        ++s_passed;
    } else {
        std::cout << "  [FAIL] " << label << "\n";
        ++s_failed;
    }
}

void Section(const std::string& name)
{
    std::cout << "\n--- " << name << " ---\n";
}

// ---------------------------------------------------------------------------
// [1] EntityID: Erstellung und Dekomposition
// ---------------------------------------------------------------------------
void Test_EntityID()
{
    Section("1. EntityID");

    EntityID id = EntityID::Make(42, 7);
    Check(id.Index()      == 42, "Index() == 42");
    Check(id.Generation() == 7,  "Generation() == 7");
    Check(id.IsValid(),          "IsValid() true");

    Check(!NULL_ENTITY.IsValid(), "NULL_ENTITY.IsValid() == false");

    // Zwei unterschiedliche IDs mit gleichem Index aber anderer Generation
    EntityID old = EntityID::Make(5, 1);
    EntityID fresh = EntityID::Make(5, 2);
    Check(old != fresh, "Verschiedene Generationen != ");
    Check(old.Index() == fresh.Index(), "Gleicher Index");
}

// ---------------------------------------------------------------------------
// [2] Entity Lifecycle
// ---------------------------------------------------------------------------
void Test_Lifecycle()
{
    Section("2. Entity Lifecycle");

    Registry reg;

    EntityID a = reg.CreateEntity();
    EntityID b = reg.CreateEntity();
    EntityID c = reg.CreateEntity();

    Check(reg.IsAlive(a), "a ist alive nach CreateEntity");
    Check(reg.IsAlive(b), "b ist alive nach CreateEntity");
    Check(reg.IsAlive(c), "c ist alive nach CreateEntity");
    Check(reg.EntityCount() == 3, "EntityCount == 3");

    reg.DestroyEntity(b);
    Check(!reg.IsAlive(b),       "b ist tot nach DestroyEntity");
    Check(reg.IsAlive(a),        "a bleibt alive");
    Check(reg.EntityCount() == 2,"EntityCount == 2 nach Destroy");

    // Doppeltes Destroy ist kein Fehler
    reg.DestroyEntity(b);
    Check(!reg.IsAlive(b), "Doppeltes DestroyEntity kein Crash");
}

// ---------------------------------------------------------------------------
// [3] Slot-Recycling + Generationssicherheit
// ---------------------------------------------------------------------------
void Test_Generation()
{
    Section("3. Generationssicherheit");

    Registry reg;

    EntityID first = reg.CreateEntity();
    EntityIndex idx = first.Index();
    reg.DestroyEntity(first);

    // Neuer Entity recycelt denselben Slot — andere Generation
    EntityID second = reg.CreateEntity();
    Check(second.Index() == idx,           "Slot recycelt (gleicher Index)");
    Check(second.Generation() != first.Generation(), "Andere Generation");
    Check(!reg.IsAlive(first),             "Alter Handle ist ungültig");
    Check( reg.IsAlive(second),            "Neuer Handle ist gültig");
}

// ---------------------------------------------------------------------------
// [4] Add / Get / Has / Remove
// ---------------------------------------------------------------------------
void Test_Components()
{
    Section("4. Components Add/Get/Has/Remove");

    Registry reg;
    EntityID e = reg.CreateEntity();

    // Add
    reg.Add<TagComponent>(e, TagComponent{"Spieler"});
    Check(reg.Has<TagComponent>(e),   "Has<Tag> true nach Add");

    auto* tag = reg.Get<TagComponent>(e);
    Check(tag != nullptr,             "Get<Tag> != nullptr");
    Check(tag->name == "Spieler",     "Tag.name == 'Spieler'");

    // Überschreiben
    reg.Add<TagComponent>(e, TagComponent{"Player"});
    Check(reg.Get<TagComponent>(e)->name == "Player", "Tag ueberschrieben");

    // Transform
    TransformComponent t;
    t.position[0] = 1.0f; t.position[1] = 2.0f; t.position[2] = 3.0f;
    reg.Add<TransformComponent>(e, t);

    auto* tc = reg.Get<TransformComponent>(e);
    Check(tc != nullptr,          "Get<Transform> != nullptr");
    Check(tc->position[0] == 1.0f
       && tc->position[1] == 2.0f
       && tc->position[2] == 3.0f, "Position korrekt gesetzt");

    // Remove
    reg.Remove<TagComponent>(e);
    Check(!reg.Has<TagComponent>(e), "Has<Tag> false nach Remove");
    Check(reg.Get<TagComponent>(e) == nullptr, "Get<Tag> == nullptr nach Remove");

    // Transform bleibt unberührt
    Check(reg.Has<TransformComponent>(e), "Transform bleibt nach Tag-Remove");

    // Components einer toten Entity werden beim Destroy bereinigt
    EntityID f = reg.CreateEntity();
    reg.Add<TagComponent>(f, TagComponent{"Temp"});
    reg.DestroyEntity(f);
    Check(!reg.Has<TagComponent>(f), "Components bereinigt nach Destroy");
}

// ---------------------------------------------------------------------------
// [5] View<T> — Iteration über einen Component-Typ
// ---------------------------------------------------------------------------
void Test_ViewSingle()
{
    Section("5. View<T> — ein Component-Typ");

    Registry reg;

    // 5 Entities mit TransformComponent anlegen
    for (int i = 0; i < 5; ++i)
    {
        EntityID e = reg.CreateEntity();
        TransformComponent t;
        t.position[0] = static_cast<float>(i);
        reg.Add<TransformComponent>(e, t);
    }

    // Eines davon ist eine Kamera — ohne Transform
    EntityID cam = reg.CreateEntity();
    reg.Add<TagComponent>(cam, TagComponent{"Kamera"});

    int count = 0;
    float posSum = 0.0f;
    reg.View<TransformComponent>([&](EntityID, TransformComponent& tc)
    {
        posSum += tc.position[0];
        ++count;
    });

    Check(count == 5,        "View iteriert genau 5 Transforms");
    Check(posSum == 0+1+2+3+4.0f, "Positionssumme 0+1+2+3+4 = 10");
}

// ---------------------------------------------------------------------------
// [6] View<T1, T2> — mehrere Component-Typen
// ---------------------------------------------------------------------------
void Test_ViewMulti()
{
    Section("6. View<Transform, Mesh> — zwei Component-Typen");

    Registry reg;

    // 3 Entities haben Transform + Mesh
    for (int i = 0; i < 3; ++i)
    {
        EntityID e = reg.CreateEntity();
        reg.Add<TransformComponent>(e);
        reg.Add<MeshComponent>(e, MeshComponent{static_cast<uint32_t>(i + 1)});
    }

    // 2 Entities haben nur Transform (kein Mesh — z.B. Kamera, Licht)
    for (int i = 0; i < 2; ++i)
    {
        EntityID e = reg.CreateEntity();
        reg.Add<TransformComponent>(e);
    }

    int meshCount = 0;
    reg.View<TransformComponent, MeshComponent>(
        [&](EntityID, TransformComponent&, MeshComponent& mc)
        {
            Check(mc.meshAssetID > 0, "MeshAssetID > 0");
            ++meshCount;
        });

    Check(meshCount == 3, "View<Transform,Mesh> findet genau 3 Entities");
}

// ---------------------------------------------------------------------------
// [7] Praxis-Szenario: kleine Szene
// ---------------------------------------------------------------------------
void Test_Scene()
{
    Section("7. Praxis: kleine Szene");

    Registry world;

    // -- Kamera --
    EntityID cam = world.CreateEntity();
    world.Add<TagComponent>(cam, TagComponent{"MainCamera"});
    {
        TransformComponent t;
        t.position[0] = 0; t.position[1] = 2; t.position[2] = -5;
        world.Add<TransformComponent>(cam, t);
    }
    {
        CameraComponent c;
        c.fov = 60.0f; c.nearPlane = 0.1f; c.farPlane = 1000.0f;
        world.Add<CameraComponent>(cam, c);
    }

    // -- Directional Light --
    EntityID sun = world.CreateEntity();
    world.Add<TagComponent>(sun, TagComponent{"Sun"});
    {
        TransformComponent t;
        t.rotation[0] = 45.0f; t.rotation[1] = 30.0f;
        world.Add<TransformComponent>(sun, t);
    }
    {
        LightComponent l;
        l.kind = LightKind::Directional;
        l.diffuseColor[0] = 1.0f; l.diffuseColor[1] = 0.95f; l.diffuseColor[2] = 0.8f;
        l.castShadows = true;
        world.Add<LightComponent>(sun, l);
    }

    // -- 3 Mesh-Entities --
    const char* names[] = { "Dreieck", "Wuerfel", "Kugel" };
    for (int i = 0; i < 3; ++i)
    {
        EntityID e = world.CreateEntity();
        world.Add<TagComponent>(e, TagComponent{names[i]});
        {
            TransformComponent t;
            t.position[0] = static_cast<float>(i) * 2.5f;
            world.Add<TransformComponent>(e, t);
        }
        world.Add<MeshComponent>(e, MeshComponent{static_cast<uint32_t>(i + 1)});
        world.Add<MaterialComponent>(e,
            MaterialComponent::FlatColor(
                0.2f + i * 0.3f,
                0.5f,
                0.8f - i * 0.2f));
    }

    Check(world.EntityCount() == 5, "5 Entities in der Szene");

    // -- System: finde alle renderbareren Entities --
    std::cout << "\n  Renderable Entities (Transform + Mesh + Material):\n";
    int renderCount = 0;
    world.View<TransformComponent, MeshComponent, MaterialComponent>(
        [&](EntityID id, TransformComponent& t, MeshComponent& m, MaterialComponent& mat)
        {
            auto* tag = world.Get<TagComponent>(id);
            std::cout << "    Entity " << id.value
                      << " (" << (tag ? tag->name : "?") << ")"
                      << "  pos=(" << t.position[0] << ","
                                   << t.position[1] << ","
                                   << t.position[2] << ")"
                      << "  meshID=" << m.meshAssetID
                      << "  albedo=("
                      << mat.albedo[0] << "," << mat.albedo[1] << "," << mat.albedo[2] << ")"
                      << "\n";
            ++renderCount;
        });

    Check(renderCount == 3, "Genau 3 renderbare Entities gefunden");

    // -- System: finde alle Lichter --
    int lightCount = 0;
    world.View<LightComponent>([&](EntityID, LightComponent&) { ++lightCount; });
    Check(lightCount == 1, "Genau 1 Licht in der Szene");

    // -- System: finde alle Kameras --
    int camCount = 0;
    world.View<CameraComponent>([&](EntityID, CameraComponent&) { ++camCount; });
    Check(camCount == 1, "Genau 1 Kamera in der Szene");

    // -- Kamera hat kein Mesh --
    Check(!world.Has<MeshComponent>(cam), "Kamera hat kein MeshComponent");

    // -- Entity zerstören und prüfen --
    EntityID dreieck = NULL_ENTITY;
    world.View<TagComponent>([&](EntityID id, TagComponent& tag)
    {
        if (tag.name == "Dreieck") dreieck = id;
    });

    Check(dreieck.IsValid(), "Dreieck-Entity gefunden");
    world.DestroyEntity(dreieck);
    Check(!world.IsAlive(dreieck), "Dreieck zerstört");
    Check(world.EntityCount() == 4, "4 Entities übrig nach Destroy");

    int renderCountAfter = 0;
    world.View<TransformComponent, MeshComponent, MaterialComponent>(
        [&](EntityID, TransformComponent&, MeshComponent&, MaterialComponent&)
        { ++renderCountAfter; });
    Check(renderCountAfter == 2, "2 renderbare Entities nach Destroy des Dreiecks");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    std::cout << "=================================================\n";
    std::cout << " ECS Step 1 — Registry + Components — Test Suite\n";
    std::cout << "=================================================\n";

    Test_EntityID();
    Test_Lifecycle();
    Test_Generation();
    Test_Components();
    Test_ViewSingle();
    Test_ViewMulti();
    Test_Scene();

    std::cout << "\n=================================================\n";
    std::cout << " Ergebnis: " << s_passed << " bestanden, "
                               << s_failed << " fehlgeschlagen\n";
    std::cout << "=================================================\n";

    return s_failed > 0 ? 1 : 0;
}
