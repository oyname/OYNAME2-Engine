#pragma once
#include <cstdint>
#include <limits>
#include <cassert>
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
//
// Known limitation — generation wrap-around:
//   Generation is 8 bits (values 1–255; 0 is reserved for NULL_ENTITY).
//   After 255 Destroy/Create cycles on the same index slot, the generation
//   wraps from 255 back to 1 (0 is skipped). From that point on, an ID
//   issued in a previous full cycle becomes indistinguishable from a current
//   one — use-after-destroy can go undetected for that slot.
//   For typical game workloads (entity counts << 10k, short lifetimes) this
//   is not a problem. If very high slot-reuse rates are expected, widen
//   EntityGeneration to uint16_t and GEN_BITS to 16.
// ---------------------------------------------------------------------------
using EntityIndex = uint32_t;
using EntityGeneration = uint8_t;
struct EntityID
{
    static constexpr uint32_t INDEX_BITS = 24;
    static constexpr uint32_t GEN_BITS = 8;
    static constexpr uint32_t INDEX_MASK = (1u << INDEX_BITS) - 1u;  // 0x00FFFFFF
    static constexpr uint32_t GEN_MASK = (1u << GEN_BITS) - 1u;  // 0x000000FF
    uint32_t value = 0;
    // --- Construction -------------------------------------------------------
    EntityID() = default;
    constexpr explicit EntityID(uint32_t raw) : value(raw) {}
    static EntityID Make(EntityIndex index, EntityGeneration gen)
    {
        assert(gen != 0 && "EntityID::Make: generation 0 is reserved for NULL_ENTITY");
        return EntityID{ (index << GEN_BITS) | (gen & GEN_MASK) };
    }
    // --- Decomposition ------------------------------------------------------
    EntityIndex      Index()      const noexcept { return (value >> GEN_BITS) & INDEX_MASK; }
    EntityGeneration Generation() const noexcept { return static_cast<EntityGeneration>(value & GEN_MASK); }
    // --- Validity -----------------------------------------------------------
    // IsValid() prueft value != 0.
    // Das funktioniert weil Generation 0 als Null-Sentinel reserviert ist:
    //   NULL_ENTITY = EntityID{0} = Make(0, 0)  -> gen=0, niemals an echte Entity vergeben.
    //   Echte Entities starten immer mit gen >= 1.
    // Damit kann EntityID{0} (value==0) niemals einer lebenden Entity entsprechen.
    bool IsValid() const noexcept { return value != 0 && Generation() != 0; }
    explicit operator bool() const noexcept { return IsValid(); }
    // --- Comparison ---------------------------------------------------------
    bool operator==(const EntityID& o) const noexcept { return value == o.value; }
    bool operator!=(const EntityID& o) const noexcept { return value != o.value; }
    bool operator< (const EntityID& o) const noexcept { return value < o.value; }
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
// ---------------------------------------------------------------------------
// ComponentTypeID<T> — compile-time integer ID pro Komponententyp.
//
// Jeder Typ bekommt beim ersten Zugriff eine eindeutige uint32_t-ID,
// die fuer die gesamte Prozesslaufzeit stabil bleibt.
//
// Verwendung in Registry: Pool-Vektor direkt per ID indexieren statt
// unordered_map<type_index> -> O(1) ohne Hash-Overhead.
//
// Voraussetzung: C++17 (inline-Variable fuer die Template-Spezialisierung).
// ---------------------------------------------------------------------------
struct ComponentTypeIDCounter
{
    static inline uint32_t next = 0u;
};
template<typename T>
struct ComponentTypeID
{
    static inline const uint32_t value = ComponentTypeIDCounter::next++;
};
