#pragma once
#include <cstdint>

// ---------------------------------------------------------------------------
// Handle<Tag> — typisierter, generationssicherer Ressourcen-Handle.
//
// Layout (32-Bit):
//   Bits 31-12 : index      (20 Bit → max. 1 048 575 Slots)
//   Bits 11- 0 : generation (12 Bit → 4096 Recyclings pro Slot detektierbar)
//
// Warum Tag-Template?
//   MeshHandle und MaterialHandle sind verschiedene Typen. Ein versehentlicher
//   Vergleich oder eine Zuweisung zwischen den beiden ist ein Compilerfehler,
//   kein stiller Bug.
//
// Warum kein raw-Pointer?
//   - Pointer sind 8 Byte. Ein Handle ist 4 Byte.
//   - Pointer invalidieren still, wenn der Store reallociert.
//     Handles erkennen Use-after-Release über die Generation.
//   - Handles sind trivially copyable → RenderCommand kann gecacht werden.
// ---------------------------------------------------------------------------

template<typename Tag>
struct Handle
{
    static constexpr uint32_t INDEX_BITS = 20u;
    static constexpr uint32_t GEN_BITS   = 12u;
    static constexpr uint32_t INDEX_MASK = (1u << INDEX_BITS) - 1u;  // 0x000FFFFF
    static constexpr uint32_t GEN_MASK   = (1u << GEN_BITS)  - 1u;  // 0x00000FFF

    uint32_t value = 0u;

    // --- Konstruktion ------------------------------------------------------
    Handle() = default;
    constexpr explicit Handle(uint32_t raw) : value(raw) {}

    static Handle Make(uint32_t index, uint32_t generation)
    {
        return Handle{ ((index & INDEX_MASK) << GEN_BITS) | (generation & GEN_MASK) };
    }

    // --- Dekomposition -----------------------------------------------------
    uint32_t Index()      const noexcept { return (value >> GEN_BITS) & INDEX_MASK; }
    uint32_t Generation() const noexcept { return value & GEN_MASK; }

    // --- Gültigkeit --------------------------------------------------------
    // Handle{0} == Invalid. Index 0, Generation 0 ist reserviert.
    bool IsValid() const noexcept { return value != 0u; }
    explicit operator bool() const noexcept { return IsValid(); }

    // --- Vergleich ---------------------------------------------------------
    bool operator==(const Handle& o) const noexcept { return value == o.value; }
    bool operator!=(const Handle& o) const noexcept { return value != o.value; }

    static Handle Invalid() { return Handle{}; }
};

// ---------------------------------------------------------------------------
// Vordefinierte Handle-Typen.
// Jeder Tag ist eine leere Struct — nur zur Typdifferenzierung.
// ---------------------------------------------------------------------------
using MeshHandle     = Handle<struct MeshTag>;
using MaterialHandle = Handle<struct MaterialTag>;
using ShaderHandle   = Handle<struct ShaderTag>;
using TextureHandle  = Handle<struct TextureTag>;
using RenderTargetHandle = Handle<struct RenderTargetTag>;
using PostProcessHandle = Handle<struct PostProcessTag>;
using GpuBufferHandle    = Handle<struct GpuBufferTag>;

// Hash-Support für unordered_map.
#include <functional>
namespace std
{
    template<typename Tag>
    struct hash<Handle<Tag>>
    {
        size_t operator()(const Handle<Tag>& h) const noexcept
        {
            return std::hash<uint32_t>{}(h.value);
        }
    };
}
