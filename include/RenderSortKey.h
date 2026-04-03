#pragma once

#include "RenderPassTypes.h"
#include <cstdint>
#include <cstring>
#include <string>

// Queue-specialized packed sort keys. The encoding is explicit and centralized
// so gather code does not invent queue-local ordering ad hoc.
enum class RenderQueueClass : uint8_t
{
    Depth = 0,
    Opaque = 1,
    Transparent = 2,
    Distortion = 3,
    MotionVectors = 4,
    ShadowDepth = 5,
};



struct RenderSortKeyDebugFields
{
    RenderQueueClass queueClass = RenderQueueClass::Opaque;
    uint8_t renderPriority = 0u;
    uint8_t transparencyClass = 0u;
    uint8_t transparencySortPriority = 0u;
    uint16_t shaderKey = 0u;
    uint16_t pipelineKey = 0u;
    uint16_t materialKey = 0u;
    uint16_t geometryKey = 0u;
    uint16_t depthKey = 0u;
    float normalizedDepth = 0.0f;
    bool backToFrontDepth = false;
};

struct RenderSortKeyParams
{
    RenderQueueClass queueClass = RenderQueueClass::Opaque;
    uint8_t renderPriority = 128u;
    uint8_t transparencyClass = 0u;
    uint8_t transparencySortPriority = 128u;
    uint16_t shaderKey = 0u;
    uint16_t pipelineKey = 0u;
    uint16_t materialKey = 0u;
    uint16_t geometryKey = 0u;
    float depth = 0.0f;
};

namespace RenderSortKey
{
    inline RenderQueueClass DefaultQueueClassForPass(RenderPass pass) noexcept;

    inline const char* QueueClassName(RenderQueueClass q) noexcept
    {
        switch (q)
        {
        case RenderQueueClass::Depth:         return "Depth";
        case RenderQueueClass::Opaque:        return "Opaque";
        case RenderQueueClass::Transparent:   return "Transparent";
        case RenderQueueClass::Distortion:    return "Distortion";
        case RenderQueueClass::MotionVectors: return "MotionVectors";
        case RenderQueueClass::ShadowDepth:   return "ShadowDepth";
        default:                              return "Unknown";
        }
    }

    inline uint32_t FloatBits(float v) noexcept
    {
        uint32_t bits = 0u;
        static_assert(sizeof(bits) == sizeof(v), "Unexpected float size.");
        std::memcpy(&bits, &v, sizeof(bits));
        return bits;
    }

    inline uint16_t QuantizeDepth01(float depth) noexcept
    {
        float d = depth;
        if (d < 0.0f) d = 0.0f;
        if (d > 1.0f) d = 1.0f;
        return static_cast<uint16_t>(d * 65535.0f + 0.5f);
    }

    inline uint16_t EncodeBackToFrontDepth(float depth) noexcept
    {
        return static_cast<uint16_t>(0xFFFFu - QuantizeDepth01(depth));
    }

    inline uint16_t EncodeFrontToBackDepth(float depth) noexcept
    {
        return QuantizeDepth01(depth);
    }

    inline uint64_t Pack(const RenderSortKeyParams& p) noexcept
    {
        const uint64_t queueClass = static_cast<uint64_t>(static_cast<uint8_t>(p.queueClass) & 0x07u);
        switch (p.queueClass)
        {
        case RenderQueueClass::Transparent:
        case RenderQueueClass::Distortion:
        {
            const uint64_t depthKey = static_cast<uint64_t>(EncodeBackToFrontDepth(p.depth));
            return (queueClass << 61)
                | (static_cast<uint64_t>(p.transparencyClass & 0x07u) << 58)
                | (static_cast<uint64_t>(p.transparencySortPriority) << 50)
                | (depthKey << 34)
                | (static_cast<uint64_t>(p.renderPriority) << 26)
                | (static_cast<uint64_t>(p.materialKey & 0x03FFu) << 16)
                | static_cast<uint64_t>(p.geometryKey);
        }

        case RenderQueueClass::ShadowDepth:
        {
            const uint64_t depthKey = static_cast<uint64_t>(EncodeFrontToBackDepth(p.depth));
            return (queueClass << 61)
                | (static_cast<uint64_t>(p.renderPriority) << 53)
                | (depthKey << 37)
                | (static_cast<uint64_t>(p.pipelineKey & 0x1FFFu) << 24)
                | (static_cast<uint64_t>(p.shaderKey & 0x0FFFu) << 12)
                | static_cast<uint64_t>(p.geometryKey & 0x0FFFu);
        }

        case RenderQueueClass::Depth:
        case RenderQueueClass::Opaque:
        case RenderQueueClass::MotionVectors:
        default:
        {
            const uint64_t depthKey = static_cast<uint64_t>(EncodeFrontToBackDepth(p.depth));
            return (queueClass << 61)
                | (static_cast<uint64_t>(p.renderPriority) << 53)
                | (static_cast<uint64_t>(p.pipelineKey & 0x1FFFu) << 40)
                | (static_cast<uint64_t>(p.shaderKey & 0x0FFFu) << 28)
                | (static_cast<uint64_t>(p.materialKey & 0x0FFFu) << 16)
                | depthKey;
        }
        }
    }

    inline uint64_t BuildPackedKey(const RenderSortKeyParams& p) noexcept
    {
        return Pack(p);
    }

    inline uint64_t BuildLegacyFallbackKeyFromPass(RenderPass pass,
                                                   uint8_t renderPriority,
                                                   uint32_t shaderSortID,
                                                   uint32_t pipelineSortID,
                                                   uint32_t materialSortID,
                                                   float depth = 0.0f) noexcept
    {
        RenderSortKeyParams params{};
        params.queueClass = DefaultQueueClassForPass(pass);
        params.shaderKey = static_cast<uint16_t>(shaderSortID);
        params.pipelineKey = static_cast<uint16_t>(pipelineSortID);
        params.materialKey = static_cast<uint16_t>(materialSortID);
        params.depth = depth;
        params.renderPriority = renderPriority;
        return Pack(params);
    }

    inline RenderSortKeyDebugFields Decode(uint64_t key) noexcept
    {
        RenderSortKeyDebugFields out{};
        out.queueClass = static_cast<RenderQueueClass>((key >> 61) & 0x07u);
        switch (out.queueClass)
        {
        case RenderQueueClass::Transparent:
        case RenderQueueClass::Distortion:
            out.transparencyClass = static_cast<uint8_t>((key >> 58) & 0x07u);
            out.transparencySortPriority = static_cast<uint8_t>((key >> 50) & 0xFFu);
            out.depthKey = static_cast<uint16_t>((key >> 34) & 0xFFFFu);
            out.renderPriority = static_cast<uint8_t>((key >> 26) & 0xFFu);
            out.materialKey = static_cast<uint16_t>((key >> 16) & 0x03FFu);
            out.geometryKey = static_cast<uint16_t>(key & 0xFFFFu);
            out.backToFrontDepth = true;
            out.normalizedDepth = float(0xFFFFu - out.depthKey) / 65535.0f;
            break;

        case RenderQueueClass::ShadowDepth:
            out.renderPriority = static_cast<uint8_t>((key >> 53) & 0xFFu);
            out.depthKey = static_cast<uint16_t>((key >> 37) & 0xFFFFu);
            out.pipelineKey = static_cast<uint16_t>((key >> 24) & 0x1FFFu);
            out.shaderKey = static_cast<uint16_t>((key >> 12) & 0x0FFFu);
            out.geometryKey = static_cast<uint16_t>(key & 0x0FFFu);
            out.normalizedDepth = float(out.depthKey) / 65535.0f;
            break;

        case RenderQueueClass::Depth:
        case RenderQueueClass::Opaque:
        case RenderQueueClass::MotionVectors:
        default:
            out.renderPriority = static_cast<uint8_t>((key >> 53) & 0xFFu);
            out.pipelineKey = static_cast<uint16_t>((key >> 40) & 0x1FFFu);
            out.shaderKey = static_cast<uint16_t>((key >> 28) & 0x0FFFu);
            out.materialKey = static_cast<uint16_t>((key >> 16) & 0x0FFFu);
            out.depthKey = static_cast<uint16_t>(key & 0xFFFFu);
            out.normalizedDepth = float(out.depthKey) / 65535.0f;
            break;
        }
        return out;
    }

    inline std::string ToDebugString(uint64_t key)
    {
        const RenderSortKeyDebugFields f = Decode(key);
        std::string s = "queue=";
        s += QueueClassName(f.queueClass);
        s += " priority=" + std::to_string(f.renderPriority);
        if (f.queueClass == RenderQueueClass::Transparent || f.queueClass == RenderQueueClass::Distortion)
        {
            s += " transparencyClass=" + std::to_string(f.transparencyClass);
            s += " transparencyPriority=" + std::to_string(f.transparencySortPriority);
            s += " pipeline=<not-packed>";
            s += " shader=<not-packed>";
            s += " material=" + std::to_string(f.materialKey);
            s += " geometry=" + std::to_string(f.geometryKey);
        }
        else if (f.queueClass == RenderQueueClass::ShadowDepth)
        {
            s += " pipeline=" + std::to_string(f.pipelineKey);
            s += " shader=" + std::to_string(f.shaderKey);
            s += " geometry=" + std::to_string(f.geometryKey);
        }
        else
        {
            s += " pipeline=" + std::to_string(f.pipelineKey);
            s += " shader=" + std::to_string(f.shaderKey);
            s += " material=" + std::to_string(f.materialKey);
            s += " geometry=<not-packed>";
        }
        s += " depthKey=" + std::to_string(f.depthKey);
        s += " depthNdc=" + std::to_string(f.normalizedDepth);
        return s;
    }

    inline RenderQueueClass DefaultQueueClassForPass(RenderPass pass) noexcept
    {
        switch (pass)
        {
        case RenderPass::Shadow:      return RenderQueueClass::ShadowDepth;
        case RenderPass::Transparent: return RenderQueueClass::Transparent;
        case RenderPass::ParticlesTransparent:
            return RenderQueueClass::Transparent;
        case RenderPass::Distortion:    return RenderQueueClass::Distortion;
        case RenderPass::Depth:         return RenderQueueClass::Depth;
        case RenderPass::MotionVectors: return RenderQueueClass::MotionVectors;
        case RenderPass::Opaque:
        default:
            return RenderQueueClass::Opaque;
        }
    }
}

