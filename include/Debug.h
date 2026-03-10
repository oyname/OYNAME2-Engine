#pragma once

// ---------------------------------------------------------------------------
// GDX_SRC_LOC — embeds the base filename and line number into a log call
// automatically, without hard-coding the filename as a string literal.
//
// Usage:  Debug::Log(GDX_SRC_LOC, "renderer initialized");
//         DBERROR(GDX_SRC_LOC, "context is null");
//
// The constexpr helper strips the directory prefix at compile time so only
// the bare filename appears in the output (e.g. "GDXEngine.cpp:42").
// ---------------------------------------------------------------------------
#include <string_view>
namespace gdx::detail
{
    constexpr std::string_view BaseName(std::string_view path) noexcept
    {
        const auto pos = path.find_last_of("/\\");
        return pos == std::string_view::npos ? path : path.substr(pos + 1);
    }
}

// Internal: builds a "File.cpp:line: " prefix as a constexpr string_view for
// the filename part; line number is appended at runtime by the logger.
// Two separate args so Debug::Log can print them individually without an
// intermediate std::string allocation.
#define GDX_SRC_LOC  ::gdx::detail::BaseName(__FILE__), ":", __LINE__, ": "

#include <iostream>
#include <sstream>
#include <string>
#include <mutex>
#include <iomanip>
#include <chrono>
#include <ctime>

// ---------------------------------------------------------------------------
// OutputDebugStringA is a Win32-only convenience that forwards log lines to
// the Visual Studio Output window.  It has no equivalent on other platforms,
// so it is guarded by _WIN32.  No windows.h is pulled in here — we use the
// SDK-provided forward declaration instead.
// ---------------------------------------------------------------------------
#if defined(_WIN32)
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char*);
#endif

// ---------------------------------------------------------------------------
// Debug macros
// DBLOG / DBWARN are stripped in Release builds.
// DBERROR is always active.
// ---------------------------------------------------------------------------
#ifdef _DEBUG
#define DBLOG(...)   Debug::Log(__VA_ARGS__)
#define DBWARN(...)  Debug::LogWarning(__VA_ARGS__)
#define DBLOG_HR(hr) Debug::LogHResult("", (long)(hr))
#else
#define DBLOG(...)   ((void)0)
#define DBWARN(...)  ((void)0)
#define DBLOG_HR(hr) ((void)0)
#endif

#define DBERROR(...) Debug::LogError(__VA_ARGS__)

class Debug
{
public:
    // When true, every log line is also forwarded to the platform debugger
    // (OutputDebugStringA on Win32, no-op on other platforms).
    static inline bool s_outputDebugString = true;

    template<typename... Args>
    static void Log(Args&&... args)
    {
        Write("[LOG]   ", std::cout, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void LogWarning(Args&&... args)
    {
        Write("[WARN]  ", std::cout, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void LogError(Args&&... args)
    {
        Write("[ERROR] ", std::cerr, std::forward<Args>(args)...);
    }

    static void LogHResult(const std::string& context, long hr)
    {
        if (hr >= 0) return;
        std::ostringstream oss;
        oss << "[HRESULT 0x" << std::hex << std::uppercase
            << static_cast<unsigned long>(hr) << "] " << context;
        const std::string msg = Timestamp() + " " + oss.str();
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            std::cerr << msg << "\n";
            std::cerr.flush();
        }
        SendToDebugger(msg);
    }

private:
    inline static std::mutex s_mutex;

    // -----------------------------------------------------------------------
    // Timestamp — portable, no Win32 required.
    // localtime_s (MSVC) and localtime_r (POSIX) are both guarded here.
    // -----------------------------------------------------------------------
    static std::string Timestamp()
    {
        using namespace std::chrono;
        const auto now = system_clock::now();
        const std::time_t t = system_clock::to_time_t(now);
        const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

        std::tm tm{};
#if defined(_MSC_VER)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif

        std::ostringstream oss;
        oss << std::setfill('0')
            << std::setw(2) << tm.tm_hour << ":"
            << std::setw(2) << tm.tm_min  << ":"
            << std::setw(2) << tm.tm_sec  << "."
            << std::setw(3) << ms.count();
        return oss.str();
    }

    // -----------------------------------------------------------------------
    // SendToDebugger — Win32 only, no windows.h required.
    // On other platforms this is a no-op compiled away entirely.
    // -----------------------------------------------------------------------
    static void SendToDebugger(const std::string& line)
    {
#if defined(_WIN32)
        if (s_outputDebugString)
            OutputDebugStringA((line + "\n").c_str());
#else
        (void)line;
#endif
    }

    // -----------------------------------------------------------------------
    // WideChar helpers — portable, no WideCharToMultiByte required.
    // wchar_t is UTF-16 on Windows and UTF-32 on Linux/macOS.
    // The conversion handles both by processing code points correctly.
    // -----------------------------------------------------------------------
    static void WideToUtf8(std::ostringstream& oss, const wchar_t* w)
    {
        if (!w) return;

        while (*w)
        {
            unsigned long cp = 0;

#if WCHAR_MAX > 0xFFFF
            // UTF-32 wchar_t (Linux, macOS)
            cp = static_cast<unsigned long>(*w++);
#else
            // UTF-16 wchar_t (Windows)
            const unsigned long u = static_cast<unsigned long>(*w++);
            if (u >= 0xD800 && u <= 0xDBFF && *w)
            {
                // High surrogate — pair with low surrogate.
                const unsigned long low = static_cast<unsigned long>(*w);
                if (low >= 0xDC00 && low <= 0xDFFF)
                {
                    cp = 0x10000 + ((u - 0xD800) << 10) + (low - 0xDC00);
                    ++w;
                }
                else
                {
                    cp = u;   // orphaned high surrogate — emit as-is
                }
            }
            else
            {
                cp = u;
            }
#endif
            // Encode code point as UTF-8.
            if (cp < 0x80)
            {
                oss << static_cast<char>(cp);
            }
            else if (cp < 0x800)
            {
                oss << static_cast<char>(0xC0 | (cp >> 6))
                    << static_cast<char>(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                oss << static_cast<char>(0xE0 | (cp >> 12))
                    << static_cast<char>(0x80 | ((cp >> 6) & 0x3F))
                    << static_cast<char>(0x80 | (cp & 0x3F));
            }
            else
            {
                oss << static_cast<char>(0xF0 | (cp >> 18))
                    << static_cast<char>(0x80 | ((cp >> 12) & 0x3F))
                    << static_cast<char>(0x80 | ((cp >> 6)  & 0x3F))
                    << static_cast<char>(0x80 | (cp & 0x3F));
            }
        }
    }

    // -----------------------------------------------------------------------
    // Append overloads — one per argument type.
    // -----------------------------------------------------------------------
    template<typename T>
    static void Append(std::ostringstream& oss, const T& v) { oss << v; }

    static void Append(std::ostringstream& oss, const wchar_t* w)
    {
        WideToUtf8(oss, w);
    }

    static void Append(std::ostringstream& oss, const std::wstring& w)
    {
        WideToUtf8(oss, w.c_str());
    }

    template<typename T, typename... Rest>
    static void AppendAll(std::ostringstream& oss, T&& first, Rest&&... rest)
    {
        Append(oss, std::forward<T>(first));
        if constexpr (sizeof...(rest) > 0)
            AppendAll(oss, std::forward<Rest>(rest)...);
    }

    template<typename... Args>
    static void Write(const char* prefix, std::ostream& stream, Args&&... args)
    {
        std::ostringstream oss;
        oss << Timestamp() << " " << prefix;
        AppendAll(oss, std::forward<Args>(args)...);
        const std::string line = oss.str();
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            stream << line << "\n";
            stream.flush();
        }
        SendToDebugger(line);
    }
};
