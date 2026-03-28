#include "GDXDX11ShaderCompiler.h"
#include "Core/Debug.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <numeric>
#include <string>
#include <system_error>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

// ---------------------------------------------------------------------------
// Cache ABI version — bump wenn sich Compiler-Flags, Profile-Konventionen
// oder Cache-Key-Format ändern. Verhindert stale Bytecode-Hits.
// ---------------------------------------------------------------------------
static constexpr uint32_t kCacheABIVersion = 2u;

namespace
{
    // -----------------------------------------------------------------------
    // Pfad-Utilities
    // -----------------------------------------------------------------------
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
            exe / L"../" / L".." / L"shader" / file,
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

    std::filesystem::path GetCacheDir()
    {
        const auto dir = GetExeDir() / L"shadercache";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    // -----------------------------------------------------------------------
    // FNV-1a 64-bit — schnell, kein externes Hash-Lib nötig
    // -----------------------------------------------------------------------
    constexpr uint64_t FNV_OFFSET = 14695981039346656037ull;
    constexpr uint64_t FNV_PRIME  = 1099511628211ull;

    uint64_t FNV1a(const void* data, size_t bytes, uint64_t hash = FNV_OFFSET)
    {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < bytes; ++i)
            hash = (hash ^ p[i]) * FNV_PRIME;
        return hash;
    }

    uint64_t FNV1aStr(const std::string& s, uint64_t hash = FNV_OFFSET)
    {
        return FNV1a(s.data(), s.size(), hash);
    }

    // -----------------------------------------------------------------------
    // Cache-Key = hash über: resolvedPath + entry + target + sorted-defines
    //             + source-file-content + ABI-version
    // Sortierte Defines: gleiche Defines in beliebiger Reihenfolge → gleicher Key.
    // -----------------------------------------------------------------------
    uint64_t BuildCacheKey(const std::filesystem::path& resolvedPath,
                           const char* entry,
                           const char* target,
                           const std::vector<std::string>& defines)
    {
        uint64_t h = FNV_OFFSET;

        // ABI-Version
        h = FNV1a(&kCacheABIVersion, sizeof(kCacheABIVersion), h);

        // Shader-Pfad (normalisiert, UTF-8)
        const auto pathStr = resolvedPath.string();
        h = FNV1a(pathStr.data(), pathStr.size(), h);

        // Einstiegspunkt + Profil
        h = FNV1aStr(entry ? entry : "", h);
        h = FNV1aStr(target ? target : "", h);

        // Defines (sortiert)
        auto sortedDefines = defines;
        std::sort(sortedDefines.begin(), sortedDefines.end());
        for (const auto& d : sortedDefines)
            h = FNV1aStr(d, h);

        // Quell-Datei-Inhalt (inkl. Änderungen ohne #include-Transitiv)
        std::ifstream f(resolvedPath, std::ios::binary);
        if (f)
        {
            std::vector<char> buf(std::istreambuf_iterator<char>(f), {});
            h = FNV1a(buf.data(), buf.size(), h);
        }

        return h;
    }

    std::filesystem::path CachePath(uint64_t key)
    {
        char hex[17];
        snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)key);
        return GetCacheDir() / (std::string(hex) + ".dxbc");
    }

    // -----------------------------------------------------------------------
    // Blob aus Disk laden (gibt nullptr zurück bei Fehler)
    // -----------------------------------------------------------------------
    ID3DBlob* TryLoadCachedBlob(uint64_t key)
    {
        const auto path = CachePath(key);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return nullptr;

        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return nullptr;

        const auto size = static_cast<size_t>(f.tellg());
        if (size == 0) return nullptr;
        f.seekg(0);

        ID3DBlob* blob = nullptr;
        if (FAILED(D3DCreateBlob(size, &blob)))
            return nullptr;

        f.read(static_cast<char*>(blob->GetBufferPointer()), size);
        if (!f) { blob->Release(); return nullptr; }

        return blob;
    }

    // -----------------------------------------------------------------------
    // Blob auf Disk schreiben
    // -----------------------------------------------------------------------
    void SaveCachedBlob(uint64_t key, ID3DBlob* blob)
    {
        if (!blob) return;
        const auto path = CachePath(key);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f.write(static_cast<const char*>(blob->GetBufferPointer()),
                static_cast<std::streamsize>(blob->GetBufferSize()));
    }

    // -----------------------------------------------------------------------
    // Instrumentierung — Thread-sicher, wird am Ende via LogShaderStats dumped
    // -----------------------------------------------------------------------
    struct CompileStats
    {
        std::mutex           mtx;
        uint32_t             hits        = 0u;
        uint32_t             misses      = 0u;
        uint32_t             failures    = 0u;
        double               totalCompileMs = 0.0;

        struct Entry { std::string label; double ms; };
        std::vector<Entry>   slowest;

        void RecordHit()   { std::lock_guard g(mtx); ++hits; }
        void RecordMiss(const std::string& label, double ms)
        {
            std::lock_guard g(mtx);
            ++misses;
            totalCompileMs += ms;
            slowest.push_back({ label, ms });
        }
        void RecordFailure() { std::lock_guard g(mtx); ++failures; }
    };

    CompileStats g_stats;

} // namespace

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

std::wstring GDXDX11ResolveShaderPath(const std::wstring& file)
{
    return FindShaderPath(file).wstring();
}

bool GDXDX11CompileShaderFromFile(const std::wstring& file,
                                  const char* entry,
                                  const char* target,
                                  ID3DBlob** outBlob,
                                  const std::vector<std::string>& defines)
{
    if (!outBlob) return false;
    *outBlob = nullptr;

    const std::filesystem::path resolvedPath = FindShaderPath(file);
    if (resolvedPath.empty())
    {
        Debug::LogError(GDX_SRC_LOC, L"HLSL shader not found: ", file.c_str());
        g_stats.RecordFailure();
        return false;
    }

    // --- Cache lookup ---
    const uint64_t cacheKey = BuildCacheKey(resolvedPath, entry, target, defines);
    if (ID3DBlob* cached = TryLoadCachedBlob(cacheKey))
    {
        *outBlob = cached;
        g_stats.RecordHit();
        Debug::Log(GDX_SRC_LOC,
                   L"Shader cache HIT  file=", file.c_str(),
                   L" [", entry, L"/", target, L"]");
        return true;
    }

    // --- Cache miss: compile ---
    const auto t0 = std::chrono::steady_clock::now();

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL1;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    std::vector<D3D_SHADER_MACRO> macros;
    macros.reserve(defines.size() + 1);
    for (const auto& d : defines)
        macros.push_back({ d.c_str(), "1" });
    macros.push_back({ nullptr, nullptr });

    ID3DBlob* errors = nullptr;
    const HRESULT hr = D3DCompileFromFile(
        resolvedPath.c_str(), macros.data(), D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry, target, flags, 0, outBlob, &errors);

    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    if (FAILED(hr))
    {
        if (errors)
        {
            const char* msg = static_cast<const char*>(errors->GetBufferPointer());
            Debug::LogError("HLSL compile FAILED: ", resolvedPath.string().c_str(),
                            " [", entry, "/", target, "] ", msg ? msg : "");
            errors->Release();
        }
        else
        {
            Debug::LogError("HLSL compile FAILED: ", resolvedPath.string().c_str(),
                            " [", entry, "/", target, "] (no message)");
        }
        g_stats.RecordFailure();
        return false;
    }

    if (errors) errors->Release();

    // --- Save to cache ---
    SaveCachedBlob(cacheKey, *outBlob);

    const std::string pathLabel = resolvedPath.filename().string()
                                + " [" + (entry ? entry : "") + "/" + (target ? target : "") + "]";
    g_stats.RecordMiss(pathLabel, ms);

    Debug::Log(GDX_SRC_LOC,
               L"Shader compile MISS file=", file.c_str(),
               L" [", entry, L"/", target, L"]  ", ms, L" ms  → cached");
    return true;
}

void GDXDX11LogShaderCacheStats()
{
    std::lock_guard g(g_stats.mtx);

    const uint32_t total = g_stats.hits + g_stats.misses + g_stats.failures;
    const float hitRate  = total > 0 ? 100.0f * g_stats.hits / (float)total : 0.0f;

    Debug::Log("=== Shader Cache Stats ===");
    Debug::Log("  Total lookups : ", total);
    Debug::Log("  Cache hits    : ", g_stats.hits,   "  (", hitRate, "%)");
    Debug::Log("  Compile misses: ", g_stats.misses);
    Debug::Log("  Failures      : ", g_stats.failures);
    Debug::Log("  Compile time  : ", g_stats.totalCompileMs, " ms total");

    if (!g_stats.slowest.empty())
    {
        // Descending sort, log top 10
        auto sorted = g_stats.slowest;
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b){ return a.ms > b.ms; });
        Debug::Log("  Top compiles:");
        for (size_t i = 0; i < (std::min)(sorted.size(), size_t(10)); ++i)
            Debug::Log("    ", sorted[i].ms, " ms  ", sorted[i].label.c_str());
    }
    Debug::Log("=========================");
}
