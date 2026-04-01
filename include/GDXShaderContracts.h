#pragma once

#include "ShaderSourceType.h"
#include "GDXShaderLayout.h"
#include "GDXRenderBindingModel.h"
#include "GDXVertexFlags.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

enum class GDXShaderStage : uint8_t
{
    Vertex = 0,
    Pixel = 1,
    Compute = 2,
};

enum class GDXShaderLanguage : uint8_t
{
    Unknown = 0,
    HLSL = 1,
    GLSL = 2,
    SPIRV = 3,
};

enum class GDXShaderArtifactFormat : uint8_t
{
    Unknown = 0,
    DXBC = 1,
    DXIL = 2,
    SPIRV = 3,
    NativeBackendObject = 4,
};

struct GDXShaderStageSourceDesc
{
    GDXShaderStage       stage = GDXShaderStage::Vertex;
    ShaderSourceType     sourceType = ShaderSourceType::HlslFilePath;
    GDXShaderLanguage    sourceLanguage = GDXShaderLanguage::Unknown;
    std::wstring         entryPoint;
    std::wstring         backendProfile;
    std::wstring         debugName;
    std::vector<uint8_t> payload;
    std::vector<std::string> defines;

    static GDXShaderStageSourceDesc FromFilePath(
        GDXShaderStage stage,
        const std::wstring& filePath,
        GDXShaderLanguage language,
        const std::wstring& entryPoint = L"",
        const std::wstring& backendProfile = L"",
        const std::wstring& debugName = L"")
    {
        GDXShaderStageSourceDesc out{};
        out.stage = stage;
        out.sourceLanguage = language;
        out.entryPoint = entryPoint;
        out.backendProfile = backendProfile;
        out.debugName = debugName.empty() ? filePath : debugName;
        switch (language)
        {
        case GDXShaderLanguage::HLSL: out.sourceType = ShaderSourceType::HlslFilePath; break;
        case GDXShaderLanguage::GLSL: out.sourceType = ShaderSourceType::GlslSource; break;
        case GDXShaderLanguage::SPIRV: out.sourceType = ShaderSourceType::SpirvBinary; break;
        default: out.sourceType = ShaderSourceType::HlslFilePath; break;
        }
        out.payload.assign(
            reinterpret_cast<const uint8_t*>(filePath.data()),
            reinterpret_cast<const uint8_t*>(filePath.data() + filePath.size()));
        return out;
    }

    std::wstring AsWideText() const
    {
        return std::wstring(
            reinterpret_cast<const wchar_t*>(payload.data()),
            payload.size() / sizeof(wchar_t));
    }
};

struct GDXShaderArtifactDesc
{
    GDXShaderStage stage = GDXShaderStage::Vertex;
    GDXShaderArtifactFormat format = GDXShaderArtifactFormat::Unknown;
    std::vector<uint8_t> bytecode;
    std::wstring debugName;

    bool IsValid() const noexcept
    {
        return format != GDXShaderArtifactFormat::Unknown && !bytecode.empty();
    }
};

struct GDXShaderSourceAssetDesc
{
    std::array<GDXShaderStageSourceDesc, 2> stages{};
    uint32_t stageCount = 0u;
    std::wstring debugName;

    void Reset() noexcept
    {
        stageCount = 0u;
        debugName.clear();
    }

    void AddStage(const GDXShaderStageSourceDesc& stageDesc) noexcept
    {
        if (stageCount >= stages.size())
            return;
        stages[stageCount++] = stageDesc;
    }

    GDXShaderStageSourceDesc* FindStageMutable(GDXShaderStage stage) noexcept
    {
        for (uint32_t i = 0; i < stageCount; ++i)
        {
            if (stages[i].stage == stage)
                return &stages[i];
        }
        return nullptr;
    }

    const GDXShaderStageSourceDesc* FindStage(GDXShaderStage stage) const noexcept
    {
        for (uint32_t i = 0; i < stageCount; ++i)
        {
            if (stages[i].stage == stage)
                return &stages[i];
        }
        return nullptr;
    }

    static GDXShaderSourceAssetDesc GraphicsFromHlslFiles(
        const std::wstring& vsPath,
        const std::wstring& psPath,
        const std::wstring& debugName = L"")
    {
        GDXShaderSourceAssetDesc out{};
        out.debugName = debugName.empty() ? (vsPath + L" / " + psPath) : debugName;
        out.AddStage(GDXShaderStageSourceDesc::FromFilePath(GDXShaderStage::Vertex, vsPath, GDXShaderLanguage::HLSL, L"", L"", vsPath));
        out.AddStage(GDXShaderStageSourceDesc::FromFilePath(GDXShaderStage::Pixel, psPath, GDXShaderLanguage::HLSL, L"", L"", psPath));
        return out;
    }
};

struct GDXShaderInterfaceContract
{
    GDXShaderLayout        shaderLayout{};
    GDXPipelineLayoutDesc  pipelineLayout{};
    GDXVertexFormatDesc    vertexFormat{};

    void BuildFromShaderLayout(const GDXShaderLayout& layout) noexcept
    {
        shaderLayout = layout;
        pipelineLayout = BuildPipelineLayoutFromShaderLayout(layout);
        vertexFormat = layout.vertexFormat;
    }
};

struct ShaderSourceDesc
{
    GDXShaderSourceAssetDesc sourceAsset{};
    GDXShaderInterfaceContract interfaceContract{};
    uint32_t vertexFlags = GDX_VERTEX_DEFAULT;
    std::wstring debugName;

    static ShaderSourceDesc FromHlslFiles(
        const std::wstring& vsPath,
        const std::wstring& psPath,
        uint32_t vertexFlags,
        const GDXShaderLayout& layout,
        const std::wstring& debugName = L"")
    {
        ShaderSourceDesc d{};
        d.vertexFlags = vertexFlags;
        d.debugName = debugName.empty() ? (vsPath + L" / " + psPath) : debugName;
        d.sourceAsset = GDXShaderSourceAssetDesc::GraphicsFromHlslFiles(vsPath, psPath, d.debugName);
        d.interfaceContract.BuildFromShaderLayout(layout);
        return d;
    }

    GDXShaderStageSourceDesc* VertexStage() noexcept
    {
        return sourceAsset.FindStageMutable(GDXShaderStage::Vertex);
    }

    const GDXShaderStageSourceDesc* VertexStage() const noexcept
    {
        return sourceAsset.FindStage(GDXShaderStage::Vertex);
    }

    GDXShaderStageSourceDesc* PixelStage() noexcept
    {
        return sourceAsset.FindStageMutable(GDXShaderStage::Pixel);
    }

    const GDXShaderStageSourceDesc* PixelStage() const noexcept
    {
        return sourceAsset.FindStage(GDXShaderStage::Pixel);
    }

    std::wstring VertexFilePath() const
    {
        const auto* stage = VertexStage();
        return stage ? stage->AsWideText() : std::wstring();
    }

    std::wstring PixelFilePath() const
    {
        const auto* stage = PixelStage();
        return stage ? stage->AsWideText() : std::wstring();
    }

    const std::vector<std::string>& Defines() const noexcept
    {
        static const std::vector<std::string> kEmpty;
        const auto* stage = VertexStage();
        return stage ? stage->defines : kEmpty;
    }
};
