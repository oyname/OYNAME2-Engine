#include "GDXDX11ShaderCompiler.h"
#include "Core/Debug.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>

#include <array>
#include <filesystem>
#include <system_error>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    std::filesystem::path GetExeDir()
    {
        std::array<wchar_t, 4096> buf{};
        const DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0 || len >= buf.size())
            return std::filesystem::current_path();
        return std::filesystem::path(buf.data()).parent_path();
    }

    std::filesystem::path FindShaderPath(const std::wstring& file)
    {
        const auto exe = GetExeDir();
        const std::vector<std::filesystem::path> candidates =
        {
            exe / L"shader" / file,
            exe / L".." / L"shader" / file,
            exe / L".." / L".." / L"shader" / file,
            exe / L".." / L".." / L".." / L"shader" / file,
            std::filesystem::current_path() / L"shader" / file,
            std::filesystem::current_path() / L".." / L"shader" / file,
        };

        for (const auto& c : candidates)
        {
            std::error_code ec;
            if (std::filesystem::exists(c, ec))
                return std::filesystem::weakly_canonical(c, ec);
        }
        return {};
    }
}

std::wstring GDXDX11ResolveShaderPath(const std::wstring& file)
{
    return FindShaderPath(file).wstring();
}

bool GDXDX11CompileShaderFromFile(const std::wstring& file,
                                  const char* entry,
                                  const char* target,
                                  ID3DBlob** outBlob)
{
    if (!outBlob)
        return false;

    *outBlob = nullptr;

    const std::wstring resolvedPath = GDXDX11ResolveShaderPath(file);
    if (resolvedPath.empty())
    {
        Debug::LogError("HLSL shader not found: ", file.c_str());
        return false;
    }

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ID3DBlob* errors = nullptr;
    const HRESULT hr = D3DCompileFromFile(
        resolvedPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry, target, flags, 0, outBlob, &errors);

    if (FAILED(hr))
    {
        if (errors)
        {
            const char* msg = static_cast<const char*>(errors->GetBufferPointer());
            Debug::LogError("HLSL compile failed: ", resolvedPath.c_str(),
                            " [", entry, " / ", target, "] ",
                            msg ? msg : "");
            errors->Release();
        }
        else
        {
            Debug::LogError("HLSL compile failed: ", resolvedPath.c_str(),
                            " [", entry, " / ", target, "] (no compiler message)");
        }
        return false;
    }

    if (errors)
        errors->Release();
    return true;
}
