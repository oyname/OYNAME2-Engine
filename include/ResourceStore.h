#pragma once
#include "Handle.h"

#include <vector>
#include <cassert>
#include <utility>

// ---------------------------------------------------------------------------
// ResourceStore<T, Tag> — generischer Slot-Map für geteilte Ressourcen.
//
// Aufgaben:
//   - Ressourcen besitzen (unique_ptr-Semantik, aber als value_type gespeichert)
//   - Stabiles Handle-Modell: Get(handle) gibt nullptr zurück, wenn das
//     Handle veraltet ist (Slot wurde inzwischen recycelt)
//   - Slot-Recycling mit Generationscheck
//
// Verwendung:
//   ResourceStore<MeshAssetResource, MeshTag> meshStore;
//   MeshHandle h = meshStore.Add(std::move(myMesh));
//   MeshAssetResource* r = meshStore.Get(h);  // nullptr wenn ungültig
//   meshStore.Release(h);
//
// Thread-Safety: keine. Ressource-Stores sind nicht thread-safe.
// Zugriff aus mehreren Threads erfordert externe Synchronisation.
// ---------------------------------------------------------------------------

template<typename T, typename Tag>
class ResourceStore
{
public:
    using HandleType = Handle<Tag>;

    ResourceStore()  = default;
    ~ResourceStore() = default;

    // Non-copyable — Store besitzt seine Ressourcen.
    ResourceStore(const ResourceStore&)            = delete;
    ResourceStore& operator=(const ResourceStore&) = delete;

    // Moveable.
    ResourceStore(ResourceStore&&)            = default;
    ResourceStore& operator=(ResourceStore&&) = default;

    // -----------------------------------------------------------------------
    // Add — Ressource in den Store aufnehmen, Handle zurückgeben.
    // Bewegt die Ressource in den Store (kein Copy).
    // Handle{} (ungültig) bedeutet: Store-Fehler (sollte nicht passieren).
    // -----------------------------------------------------------------------
    HandleType Add(T resource)
    {
        if (!m_freeList.empty())
        {
            // Slot recyceln.
            const uint32_t idx = m_freeList.back();
            m_freeList.pop_back();

            Slot& slot = m_slots[idx];
            slot.data    = std::move(resource);
            slot.alive   = true;
            // Generation wurde bereits beim Release inkrementiert.
            return HandleType::Make(idx, slot.generation);
        }

        // Neuen Slot anlegen.
        const uint32_t idx = static_cast<uint32_t>(m_slots.size());
        // Index 0 ist für den Invalid-Handle reserviert → ersten echten Slot
        // bei idx==0 auf idx=1 verschieben durch einmaligen Dummy-Slot.
        if (idx == 0)
        {
            m_slots.push_back(Slot{});  // Slot 0 = reserviert/invalid
            return Add(std::move(resource));
        }

        Slot s;
        s.data       = std::move(resource);
        s.generation = 1u;   // Generation 0 ist ungültig → starten bei 1
        s.alive      = true;
        m_slots.push_back(std::move(s));

        return HandleType::Make(idx, m_slots[idx].generation);
    }

    // -----------------------------------------------------------------------
    // Get — Ressource per Handle abfragen.
    // Gibt nullptr zurück wenn:
    //   - Handle ungültig (value == 0)
    //   - Index out-of-range
    //   - Generation stimmt nicht (Slot wurde recycelt)
    //   - Slot ist nicht alive
    // -----------------------------------------------------------------------
    T* Get(HandleType h)
    {
        if (!h.IsValid()) return nullptr;
        const uint32_t idx = h.Index();
        if (idx >= m_slots.size()) return nullptr;
        Slot& slot = m_slots[idx];
        if (!slot.alive || slot.generation != h.Generation()) return nullptr;
        return &slot.data;
    }

    const T* Get(HandleType h) const
    {
        if (!h.IsValid()) return nullptr;
        const uint32_t idx = h.Index();
        if (idx >= m_slots.size()) return nullptr;
        const Slot& slot = m_slots[idx];
        if (!slot.alive || slot.generation != h.Generation()) return nullptr;
        return &slot.data;
    }

    // -----------------------------------------------------------------------
    // IsValid — prüft ob ein Handle noch gültig ist.
    // -----------------------------------------------------------------------
    bool IsValid(HandleType h) const
    {
        return Get(h) != nullptr;
    }

    // -----------------------------------------------------------------------
    // Release — Slot freigeben. Handle wird danach ungültig.
    // Nächstes Add() kann denselben Index mit inkrementierter Generation recyceln.
    // -----------------------------------------------------------------------
    bool Release(HandleType h)
    {
        T* r = Get(h);
        if (!r) return false;

        Slot& slot = m_slots[h.Index()];
        slot.data    = T{};         // Ressource zerstören / Default-konstruieren
        slot.alive   = false;
        ++slot.generation;          // Generation bumpen → alte Handles ungültig
        if (slot.generation == 0u) slot.generation = 1u; // Wrap-around verhindern

        m_freeList.push_back(h.Index());
        return true;
    }

    bool Remove(HandleType h)
    {
        return Release(h);
    }

    // -----------------------------------------------------------------------
    // Iteration — alle lebenden Slots durchlaufen.
    // func(HandleType, T&) wird für jeden lebenden Slot aufgerufen.
    // -----------------------------------------------------------------------
    template<typename Func>
    void ForEach(Func&& func)
    {
        for (uint32_t i = 1u; i < static_cast<uint32_t>(m_slots.size()); ++i)
        {
            Slot& s = m_slots[i];
            if (!s.alive) continue;
            func(HandleType::Make(i, s.generation), s.data);
        }
    }

    // -----------------------------------------------------------------------
    // Diagnostics
    // -----------------------------------------------------------------------
    size_t AliveCount() const
    {
        size_t n = 0;
        for (size_t i = 1; i < m_slots.size(); ++i)
            if (m_slots[i].alive) ++n;
        return n;
    }

    bool Empty() const { return AliveCount() == 0; }

private:
    struct Slot
    {
        T        data       = {};
        uint32_t generation = 0u;
        bool     alive      = false;
    };

    std::vector<Slot>     m_slots;
    std::vector<uint32_t> m_freeList;
};
