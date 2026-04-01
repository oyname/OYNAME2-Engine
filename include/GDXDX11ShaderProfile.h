#pragma once

#include "GDXShaderContracts.h"

#include <string>

inline const wchar_t* GDXDX11DefaultEntryPointForStage(GDXShaderStage stage) noexcept
{
    switch (stage)
    {
    case GDXShaderStage::Vertex:  return L"main";
    case GDXShaderStage::Pixel:   return L"main";
    case GDXShaderStage::Compute: return L"main";
    default:                      return L"main";
    }
}

inline const char* GDXDX11DefaultCompileTargetForStage(GDXShaderStage stage) noexcept
{
    switch (stage)
    {
    case GDXShaderStage::Vertex:  return "vs_5_0";
    case GDXShaderStage::Pixel:   return "ps_5_0";
    case GDXShaderStage::Compute: return "cs_5_0";
    default:                      return nullptr;
    }
}

inline std::wstring GDXDX11ResolveEntryPoint(const GDXShaderStageSourceDesc& stageDesc)
{
    return stageDesc.entryPoint.empty() ? std::wstring(GDXDX11DefaultEntryPointForStage(stageDesc.stage)) : stageDesc.entryPoint;
}

inline std::string GDXDX11NarrowAscii(const std::wstring& value)
{
    std::string out;
    out.reserve(value.size());
    for (wchar_t ch : value)
        out.push_back((ch >= 0 && ch <= 0x7F) ? static_cast<char>(ch) : '?');
    return out;
}

inline std::string GDXDX11ResolveCompileTarget(const GDXShaderStageSourceDesc& stageDesc)
{
    if (!stageDesc.backendProfile.empty())
        return GDXDX11NarrowAscii(stageDesc.backendProfile);
    const char* target = GDXDX11DefaultCompileTargetForStage(stageDesc.stage);
    return target ? std::string(target) : std::string();
}
