#pragma once

#include <array>
#include <cstdint>

// Gemeinsames semantisches Binding-Modell fuer ShaderLayout, PipelineLayout,
// DescriptorSet-Building und Recorded Binding Groups.
// Kein Backend-Registerdenken im Core-Modell.

enum class GDXShaderConstantBufferSlot : uint8_t
{
    Entity,
    Frame,
    Material,
    Skin,
    Light,
    Pass,
};

enum class GDXShaderTextureSemantic : uint8_t
{
    Albedo,
    Normal,
    ORM,
    Emissive,
    Detail,
    ShadowMap,
};

enum class GDXBindingGroup : uint8_t
{
    Pass = 0,
    Material = 1,
    Draw = 2,
};

enum class GDXBoundResourceClass : uint8_t
{
    ConstantBuffer = 0,
    Texture = 1,
    Sampler = 2,
    StructuredBuffer = 3,
    UnorderedAccess = 4,
};

enum class GDXShaderStageVisibility : uint8_t
{
    None = 0,
    Vertex = 1 << 0,
    Pixel = 1 << 1,
    AllGraphics = Vertex | Pixel,
};

inline constexpr GDXShaderStageVisibility operator|(GDXShaderStageVisibility a, GDXShaderStageVisibility b) noexcept
{
    return static_cast<GDXShaderStageVisibility>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline constexpr bool HasStageVisibility(GDXShaderStageVisibility value, GDXShaderStageVisibility bit) noexcept
{
    return (static_cast<uint8_t>(value) & static_cast<uint8_t>(bit)) != 0u;
}

struct GDXShaderConstantBufferBinding
{
    GDXShaderConstantBufferSlot slot = GDXShaderConstantBufferSlot::Entity;
    GDXBindingGroup bindingGroup = GDXBindingGroup::Draw;
    GDXBoundResourceClass resourceClass = GDXBoundResourceClass::ConstantBuffer;
    GDXShaderStageVisibility visibility = GDXShaderStageVisibility::AllGraphics;
    uint32_t layoutBindingIndex = 0u;
};

struct GDXShaderTextureBinding
{
    GDXShaderTextureSemantic semantic = GDXShaderTextureSemantic::Albedo;
    GDXBindingGroup bindingGroup = GDXBindingGroup::Material;
    GDXBoundResourceClass resourceClass = GDXBoundResourceClass::Texture;
    GDXShaderStageVisibility visibility = GDXShaderStageVisibility::Pixel;
    uint32_t layoutBindingIndex = 0u;
};

struct GDXBindingGroupLayoutDesc
{
    GDXBindingGroup group = GDXBindingGroup::Pass;
    std::array<uint32_t, 16> bindingIndices{};
    std::array<GDXBoundResourceClass, 16> resourceClasses{};
    std::array<GDXShaderStageVisibility, 16> visibilities{};
    std::array<uint8_t, 16> requiredFlags{};
    uint32_t bindingCount = 0u;

    void Reset(GDXBindingGroup inGroup) noexcept
    {
        group = inGroup;
        bindingCount = 0u;
        for (uint32_t i = 0; i < bindingIndices.size(); ++i)
        {
            bindingIndices[i] = 0u;
            resourceClasses[i] = GDXBoundResourceClass::Texture;
            visibilities[i] = GDXShaderStageVisibility::None;
            requiredFlags[i] = 1u;
        }
    }

    int32_t FindBindingSlot(uint32_t bindingIndex, GDXBoundResourceClass resourceClass) const noexcept
    {
        for (uint32_t i = 0; i < bindingCount; ++i)
        {
            if (bindingIndices[i] == bindingIndex && resourceClasses[i] == resourceClass)
                return static_cast<int32_t>(i);
        }
        return -1;
    }

    bool AddBinding(uint32_t bindingIndex,
                    GDXBoundResourceClass resourceClass,
                    GDXShaderStageVisibility visibility,
                    bool required = true) noexcept
    {
        const int32_t existing = FindBindingSlot(bindingIndex, resourceClass);
        if (existing >= 0)
        {
            const uint32_t slot = static_cast<uint32_t>(existing);
            visibilities[slot] = visibilities[slot] | visibility;
            requiredFlags[slot] = static_cast<uint8_t>((requiredFlags[slot] != 0u) || required);
            return true;
        }

        if (bindingCount >= bindingIndices.size())
            return false;

        bindingIndices[bindingCount] = bindingIndex;
        resourceClasses[bindingCount] = resourceClass;
        visibilities[bindingCount] = visibility;
        requiredFlags[bindingCount] = required ? 1u : 0u;
        ++bindingCount;
        return true;
    }

    bool HasBinding(uint32_t bindingIndex, GDXBoundResourceClass resourceClass) const noexcept
    {
        return FindBindingSlot(bindingIndex, resourceClass) >= 0;
    }

    bool IsBindingRequired(uint32_t bindingIndex, GDXBoundResourceClass resourceClass) const noexcept
    {
        const int32_t slot = FindBindingSlot(bindingIndex, resourceClass);
        return slot >= 0 ? (requiredFlags[static_cast<uint32_t>(slot)] != 0u) : false;
    }

    bool HasBinding(uint32_t bindingIndex) const noexcept
    {
        for (uint32_t i = 0; i < bindingCount; ++i)
        {
            if (bindingIndices[i] == bindingIndex)
                return true;
        }
        return false;
    }
};

struct GDXPipelineLayoutDesc
{
    static constexpr uint32_t MaxBindings = 16u;

    std::array<GDXShaderConstantBufferBinding, 8> constantBuffers{};
    std::array<GDXShaderTextureBinding, 16> textureBindings{};
    std::array<GDXBindingGroupLayoutDesc, 3> groupLayouts{};
    uint32_t constantBufferCount = 0u;
    uint32_t textureBindingCount = 0u;
    uint32_t groupLayoutCount = 0u;
    bool usesDescriptorSets = true;

    void Reset() noexcept
    {
        constantBufferCount = 0u;
        textureBindingCount = 0u;
        groupLayoutCount = 3u;
        groupLayouts[0].Reset(GDXBindingGroup::Pass);
        groupLayouts[1].Reset(GDXBindingGroup::Material);
        groupLayouts[2].Reset(GDXBindingGroup::Draw);
    }

    GDXBindingGroupLayoutDesc* FindGroupLayout(GDXBindingGroup group) noexcept
    {
        for (uint32_t i = 0; i < groupLayoutCount; ++i)
        {
            if (groupLayouts[i].group == group)
                return &groupLayouts[i];
        }
        return nullptr;
    }

    const GDXBindingGroupLayoutDesc* FindGroupLayout(GDXBindingGroup group) const noexcept
    {
        for (uint32_t i = 0; i < groupLayoutCount; ++i)
        {
            if (groupLayouts[i].group == group)
                return &groupLayouts[i];
        }
        return nullptr;
    }

    const GDXShaderTextureBinding* FindTextureBinding(GDXBindingGroup group, uint32_t bindingIndex) const noexcept
    {
        for (uint32_t i = 0; i < textureBindingCount; ++i)
        {
            const auto& binding = textureBindings[i];
            if (binding.bindingGroup == group && binding.layoutBindingIndex == bindingIndex)
                return &binding;
        }
        return nullptr;
    }

    const GDXShaderConstantBufferBinding* FindConstantBufferBinding(GDXBindingGroup group, uint32_t bindingIndex) const noexcept
    {
        for (uint32_t i = 0; i < constantBufferCount; ++i)
        {
            const auto& binding = constantBuffers[i];
            if (binding.bindingGroup == group && binding.layoutBindingIndex == bindingIndex)
                return &binding;
        }
        return nullptr;
    }

    uint64_t BuildStableHash() const noexcept
    {
        constexpr uint64_t kOffset = 1469598103934665603ull;
        constexpr uint64_t kPrime = 1099511628211ull;
        auto mix = [&](uint64_t value, uint64_t& hash) noexcept
        {
            hash ^= value;
            hash *= kPrime;
        };

        uint64_t hash = kOffset;
        mix(static_cast<uint64_t>(constantBufferCount), hash);
        mix(static_cast<uint64_t>(textureBindingCount), hash);
        mix(static_cast<uint64_t>(groupLayoutCount), hash);
        mix(static_cast<uint64_t>(usesDescriptorSets ? 1u : 0u), hash);

        for (uint32_t i = 0; i < constantBufferCount; ++i)
        {
            const auto& cb = constantBuffers[i];
            mix(static_cast<uint64_t>(cb.slot), hash);
            mix(static_cast<uint64_t>(cb.bindingGroup), hash);
            mix(static_cast<uint64_t>(cb.resourceClass), hash);
            mix(static_cast<uint64_t>(cb.visibility), hash);
            mix(static_cast<uint64_t>(cb.layoutBindingIndex), hash);
        }

        for (uint32_t i = 0; i < textureBindingCount; ++i)
        {
            const auto& tex = textureBindings[i];
            mix(static_cast<uint64_t>(tex.semantic), hash);
            mix(static_cast<uint64_t>(tex.bindingGroup), hash);
            mix(static_cast<uint64_t>(tex.resourceClass), hash);
            mix(static_cast<uint64_t>(tex.visibility), hash);
            mix(static_cast<uint64_t>(tex.layoutBindingIndex), hash);
        }

        for (uint32_t i = 0; i < groupLayoutCount; ++i)
        {
            const auto& group = groupLayouts[i];
            mix(static_cast<uint64_t>(group.group), hash);
            mix(static_cast<uint64_t>(group.bindingCount), hash);
            for (uint32_t j = 0; j < group.bindingCount; ++j)
            {
                mix(static_cast<uint64_t>(group.bindingIndices[j]), hash);
                mix(static_cast<uint64_t>(group.resourceClasses[j]), hash);
                mix(static_cast<uint64_t>(group.visibilities[j]), hash);
                mix(static_cast<uint64_t>(group.requiredFlags[j]), hash);
            }
        }

        return hash;
    }

    bool IsValid() const noexcept
    {
        for (uint32_t i = 0; i < groupLayoutCount; ++i)
        {
            const auto& group = groupLayouts[i];
            for (uint32_t a = 0; a < group.bindingCount; ++a)
            {
                for (uint32_t b = a + 1u; b < group.bindingCount; ++b)
                {
                    if (group.bindingIndices[a] == group.bindingIndices[b] &&
                        group.resourceClasses[a] == group.resourceClasses[b])
                        return false;
                }
            }
        }
        return true;
    }

    void AddConstantBufferBinding(const GDXShaderConstantBufferBinding& binding) noexcept
    {
        for (uint32_t i = 0; i < constantBufferCount; ++i)
        {
            auto& existing = constantBuffers[i];
            if (existing.bindingGroup == binding.bindingGroup &&
                existing.layoutBindingIndex == binding.layoutBindingIndex &&
                existing.resourceClass == binding.resourceClass)
            {
                existing.visibility = existing.visibility | binding.visibility;
                return;
            }
        }

        if (constantBufferCount >= constantBuffers.size())
            return;
        constantBuffers[constantBufferCount++] = binding;
        if (auto* group = FindGroupLayout(binding.bindingGroup))
            group->AddBinding(binding.layoutBindingIndex, binding.resourceClass, binding.visibility, true);
    }

    void AddTextureBinding(const GDXShaderTextureBinding& binding) noexcept
    {
        for (uint32_t i = 0; i < textureBindingCount; ++i)
        {
            auto& existing = textureBindings[i];
            if (existing.bindingGroup == binding.bindingGroup &&
                existing.layoutBindingIndex == binding.layoutBindingIndex &&
                existing.resourceClass == binding.resourceClass)
            {
                existing.visibility = existing.visibility | binding.visibility;
                return;
            }
        }

        if (textureBindingCount >= textureBindings.size())
            return;
        textureBindings[textureBindingCount++] = binding;
        if (auto* group = FindGroupLayout(binding.bindingGroup))
            group->AddBinding(binding.layoutBindingIndex, binding.resourceClass, binding.visibility, true);
    }
};
