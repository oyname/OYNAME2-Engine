#pragma once

#include <cstdint>
#include <limits>

// ---------------------------------------------------------------------------
// EntityID — the one and only identity of an entity.
//
// An EntityID is just a number.  It carries no data and no behaviour.
// Components are stored separately and looked up by this ID.
//
// Layout (32 bit):
//   bits 31-8  : index       (16 777 214 possible live entities)
//   bits  7-0  : generation  (detects use-after-destroy)
//
// Generation: every time a slot is recycled the generation byte is
// incremented.  A stale ID (old generation) is rejected by the Registry.
// ---------------------------------------------------------------------------

using EntityIndex      = uint32_t;
using EntityGeneration = uint8_t;

struct EntityID
{
    static constexpr uint32_t INDEX_BITS = 24;
    static constexpr uint32_t GEN_BITS   =  8;
    static constexpr uint32_t INDEX_MASK = (1u << INDEX_BITS) - 1u;  // 0x00FFFFFF
    static constexpr uint32_t GEN_MASK   = (1u << GEN_BITS)  - 1u;  // 0x000000FF

    uint32_t value = 0;

    // --- Construction -------------------------------------------------------
    EntityID() = default;
    constexpr explicit EntityID(uint32_t raw) : value(raw) {}

    static EntityID Make(EntityIndex index, EntityGeneration gen)
    {
        return EntityID{ (index << GEN_BITS) | (gen & GEN_MASK) };
    }

    // --- Decomposition ------------------------------------------------------
    EntityIndex      Index()      const noexcept { return (value >> GEN_BITS) & INDEX_MASK; }
    EntityGeneration Generation() const noexcept { return static_cast<EntityGeneration>(value & GEN_MASK); }

    // --- Validity -----------------------------------------------------------
    bool IsValid() const noexcept { return value != 0; }
    explicit operator bool() const noexcept { return IsValid(); }

    // --- Comparison ---------------------------------------------------------
    bool operator==(const EntityID& o) const noexcept { return value == o.value; }
    bool operator!=(const EntityID& o) const noexcept { return value != o.value; }
    bool operator< (const EntityID& o) const noexcept { return value <  o.value; }
};

// Sentinel: the "null" / invalid entity.
inline constexpr EntityID NULL_ENTITY{ 0u };

// Hash support so EntityID can be used as unordered_map key.
#include <functional>
namespace std
{
    template<> struct hash<EntityID>
    {
        size_t operator()(const EntityID& e) const noexcept
        {
            return std::hash<uint32_t>{}(e.value);
        }
    };
}
