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
#include <set>
#include <vector>

#if !defined(KROM_SHIPPING)
#pragma comment(lib, "d3dcompiler.lib")
#endif

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

    std::string WideToUtf8(const std::wstring& value)
    {
        if (value.empty()) return {};

        const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                                              nullptr, 0, nullptr, nullptr);
        if (bytes <= 0) return {};

        std::string out(static_cast<size_t>(bytes), '\0');
        const int written = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                                                out.data(), bytes, nullptr, nullptr);
        if (written <= 0) return {};
        return out;
    }

    std::string PathToUtf8String(const std::filesystem::path& path)
    {
        return WideToUtf8(path.native());
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
    // Sammelt transitiv alle #include-Dateien eines Shaders (rekursiv, kein Loop-Schutz
    // nötig — HLSL-Includes sind azyklisch per Konvention und #pragma once / Guards).
    void CollectIncludes(const std::filesystem::path& file,
                         std::set<std::filesystem::path>& visited)
    {
        std::error_code ec;
        if (!std::filesystem::exists(file, ec)) return;
        if (!visited.insert(std::filesystem::weakly_canonical(file, ec)).second) return;

        std::ifstream f(file);
        if (!f) return;

        const std::filesystem::path dir = file.parent_path();
        std::string line;
        while (std::getline(f, line))
        {
            // Matcht: #include "..." (kein <system>-Include — die kommen nie vom Shader-Verzeichnis)
            auto q1 = line.find('"');
            if (line.find("#include") == std::string::npos || q1 == std::string::npos) continue;
            auto q2 = line.find('"', q1 + 1);
            if (q2 == std::string::npos) continue;
            const std::string rel = line.substr(q1 + 1, q2 - q1 - 1);
            const auto candidate = std::filesystem::weakly_canonical(dir / rel, ec);
            CollectIncludes(candidate, visited);
        }
    }

    uint64_t BuildCacheKey(const std::filesystem::path& resolvedPath,
                           const char* entry,
                           const char* target,
                           const std::vector<std::string>& defines)
    {
        uint64_t h = FNV_OFFSET;

        // ABI-Version
        h = FNV1a(&kCacheABIVersion, sizeof(kCacheABIVersion), h);

        // Einstiegspunkt + Profil
        h = FNV1aStr(entry ? entry : "", h);
        h = FNV1aStr(target ? target : "", h);

        // Defines (sortiert)
        auto sortedDefines = defines;
        std::sort(sortedDefines.begin(), sortedDefines.end());
        for (const auto& d : sortedDefines)
            h = FNV1aStr(d, h);

        // Transitive Includes sammeln, sortiert hashen (Reihenfolge deterministisch)
        std::set<std::filesystem::path> allFiles;
        CollectIncludes(resolvedPath, allFiles);

        for (const auto& p : allFiles)  // std::set ist sortiert
        {
            // Pfad in Hash (damit Umbenennen invalidiert)
            const auto ps = p.string();
            h = FNV1a(ps.data(), ps.size(), h);

            // Dateiinhalt in Hash
            std::ifstream f(p, std::ios::binary);
            if (f)
            {
                std::vector<char> buf(std::istreambuf_iterator<char>(f), {});
                h = FNV1a(buf.data(), buf.size(), h);
            }
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
        //Debug::Log(GDX_SRC_LOC,
        //           L"Shader cache HIT  file=", file.c_str(),
        //           L" [", entry, L"/", target, L"]");
        return true;
    }

    // --- Cache miss ---
#if defined(KROM_SHIPPING)
    const std::string shaderNameUtf8 = PathToUtf8String(resolvedPath.filename());
    Debug::LogError("Shader not in cache (shipping): ", shaderNameUtf8.c_str(),
                    " [", entry, "/", target, "]");
    g_stats.RecordFailure();
    return false;
#else
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
        if (errors && errors->GetBufferSize() > 0)
        {
            Debug::LogError(GDX_SRC_LOC,
                L"HLSL compile error in: ", file.c_str(), L"\n",
                static_cast<const char*>(errors->GetBufferPointer()));
            errors->Release();
        }
        else
        {
            Debug::LogError(GDX_SRC_LOC,
                L"HLSL compile failed (no error blob): ", file.c_str(),
                L"  HRESULT=0x", (unsigned long)hr);
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

    return true;
#endif // KROM_SHIPPING
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

#if !defined(KROM_SHIPPING)
uint32_t GDXDX11PrecompileAllShaders(const std::wstring& shaderDir)
{
    // Alle bekannten Shader-Einstiegspunkte — muss mit CreateVariant/PrewarmPostProcess übereinstimmen.
    struct ShaderEntry { std::wstring file; const char* entry; const char* target; std::vector<std::string> defines; };

    const std::vector<ShaderEntry> entries =
    {
        // Main VS-Varianten
        { L"VertexShader.hlsl",            "main", "vs_5_0", {} },
        { L"VertexShader.hlsl",            "main", "vs_5_0", {"HAS_VERTEX_COLOR"} },
        { L"VertexShader.hlsl",            "main", "vs_5_0", {"HAS_SKINNING"} },
        { L"VertexShader.hlsl",            "main", "vs_5_0", {"HAS_SKINNING", "HAS_VERTEX_COLOR"} },
        // Main PS
        { L"PixelShader.hlsl",             "main", "ps_5_0", {} },
        // Shadow
        { L"ShadowVertexShader.hlsl",      "main", "vs_5_0", {} },
        { L"ShadowVertexShader.hlsl",      "main", "vs_5_0", {"ALPHA_TEST"} },
        { L"ShadowVertexShader.hlsl",      "main", "vs_5_0", {"HAS_SKINNING"} },
        { L"ShadowVertexShader.hlsl",      "main", "vs_5_0", {"HAS_SKINNING", "ALPHA_TEST"} },
        { L"ShadowPixelShader.hlsl",       "main", "ps_5_0", {} },
        { L"ShadowPixelShader.hlsl",       "main", "ps_5_0", {"ALPHA_TEST"} },
        // Compute
        { L"TileLightCullCS.hlsl",         "main", "cs_5_0", {} },
        // Post-process
        { L"PostProcessFullscreenVS.hlsl", "main", "vs_5_0", {} },
        { L"PostProcessToneMappingPS.hlsl","main", "ps_5_0", {} },
        { L"PostProcessFXAAPS.hlsl",       "main", "ps_5_0", {} },
        { L"PostProcessBloomBrightPS.hlsl","main", "ps_5_0", {} },
        { L"PostProcessBloomBlurPS.hlsl",  "main", "ps_5_0", {} },
        { L"PostProcessBloomCompositePS.hlsl","main","ps_5_0",{} },
        { L"PostProcessGTAOPS.hlsl",       "main", "ps_5_0", {} },
        { L"PostProcessGTAOBlurPS.hlsl",   "main", "ps_5_0", {} },
        { L"PostProcessGTAOCompositePS.hlsl","main","ps_5_0",{} },
        { L"PostProcessDepthDebugPS.hlsl", "main", "ps_5_0", {} },
        { L"PostProcessNormalDebugPS.hlsl","main", "ps_5_0", {} },
        { L"PostProcessEdgeDebugPS.hlsl",  "main", "ps_5_0", {} },
        { L"PostProcessDepthFogPS.hlsl",   "main", "ps_5_0", {} },
        { L"PostProcessVolumetricFogPS.hlsl",   "main", "ps_5_0", {} },
    };

    uint32_t failures = 0u;
    for (const auto& e : entries)
    {
        const std::wstring path = shaderDir + L"/" + e.file;
        ID3DBlob* blob = nullptr;
        if (!GDXDX11CompileShaderFromFile(path, e.entry, e.target, &blob, e.defines))
            ++failures;
        if (blob) blob->Release();
    }

    Debug::Log("GDXDX11PrecompileAllShaders: ", (uint32_t)entries.size(), " shaders, ",
               failures, " failures.");
    GDXDX11LogShaderCacheStats();
    return failures;
}
#endif // KROM_SHIPPING
