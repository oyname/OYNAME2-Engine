#include "GDXWin32Window.h"
#include "Debug.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <mutex>  // std::once_flag, std::call_once

namespace
{
    constexpr const wchar_t* k_className = L"GDXWin32WindowClass";

    Key TranslateKey(WPARAM wp)
    {
        switch (wp)
        {
        case VK_ESCAPE: return Key::Escape;
        case VK_SPACE:  return Key::Space;
        case 'A':       return Key::A;
        case 'D':       return Key::D;
        case 'S':       return Key::S;
        case 'W':       return Key::W;
        case VK_LEFT:   return Key::Left;
        case VK_RIGHT:  return Key::Right;
        case VK_UP:     return Key::Up;
        case VK_DOWN:   return Key::Down;
        default:        return Key::Unknown;
        }
    }

} // anonymous namespace

// ---------------------------------------------------------------------------
// RegisterClassOnce
//
// Uses std::call_once so the registration attempt is made exactly once across
// all threads.  Returns false (and logs) if the OS call fails for a reason
// other than "class already exists" — the caller (Create) treats that as a
// fatal error and aborts window creation, leaving s_registered = false so a
// future attempt after a hypothetical fix could retry.
// ---------------------------------------------------------------------------
bool GDXWin32Window::RegisterClassOnce()
{
    static std::once_flag s_flag;
    static bool           s_registered = false;

    std::call_once(s_flag, []()
    {
        WNDCLASSEXW wc    = {};
        wc.cbSize         = sizeof(wc);
        wc.style          = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc    = reinterpret_cast<WNDPROC>(StaticWndProc);
        wc.hInstance      = GetModuleHandleW(nullptr);
        wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground  = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName  = k_className;

        if (!RegisterClassExW(&wc))
        {
            const DWORD err = GetLastError();
            if (err == ERROR_CLASS_ALREADY_EXISTS)
            {
                // Another module already registered the class — perfectly fine.
                s_registered = true;
                return;
            }
            // Real failure: log with context and leave s_registered = false.
            DBERROR(GDX_SRC_LOC, "RegisterClassExW failed (err=", err, ")");
            return;
        }

        s_registered = true;
    });

    return s_registered;
}

GDXWin32Window::GDXWin32Window(const WindowDesc& desc, GDXEventQueue& events)
    : m_desc(desc)
    , m_events(events)
    , m_width(desc.width)
    , m_height(desc.height)
{
}

GDXWin32Window::~GDXWin32Window()
{
    // DestroyWindow triggers WM_DESTROY which calls PostQuitMessage.
    // We only call it when we actually own a valid HWND.
    if (m_handles.hwnd)
    {
        DestroyWindow(reinterpret_cast<HWND>(m_handles.hwnd));
        m_handles.hwnd = nullptr;
    }
}

bool GDXWin32Window::Create()
{
    if (!RegisterClassOnce())
    {
        DBERROR(GDX_SRC_LOC, "window class registration failed — aborting Create");
        return false;
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!m_desc.resizable)
    {
        style &= ~WS_THICKFRAME;
        style &= ~WS_MAXIMIZEBOX;
    }

    RECT rc = { 0, 0, m_desc.width, m_desc.height };
    AdjustWindowRect(&rc, style, FALSE);

    const int ww = rc.right  - rc.left;
    const int wh = rc.bottom - rc.top;

    // ---------------------------------------------------------------------------
    // UTF-8 -> UTF-16 conversion.
    //
    // MultiByteToWideChar with cbMultiByte=-1 returns the required buffer size
    // INCLUDING the null terminator.  std::wstring manages its own null, so we
    // allocate (wLen - 1) characters — the string object's internal buffer still
    // has room for the terminating L'\0' that MultiByteToWideChar writes, but it
    // sits in the std::wstring's managed null slot and does not become part of
    // the string's logical length.
    // ---------------------------------------------------------------------------
    const int wLen = MultiByteToWideChar(
        CP_UTF8, 0, m_desc.title.c_str(), -1, nullptr, 0);
    if (wLen <= 0)
    {
        DBERROR(GDX_SRC_LOC, "title UTF-8→UTF-16 conversion failed");
        return false;
    }
    std::wstring wTitle(static_cast<size_t>(wLen - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, m_desc.title.c_str(), -1, wTitle.data(), wLen);

    HWND hwnd = CreateWindowExW(
        0, k_className, wTitle.c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT, ww, wh,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);

    if (!hwnd)
    {
        DBERROR(GDX_SRC_LOC, "CreateWindowExW failed (err=", GetLastError(), ")");
        return false;
    }

    m_handles.hwnd       = reinterpret_cast<HWND__*>(hwnd);
    m_handles.hinstance  = reinterpret_cast<HINSTANCE__*>(GetModuleHandleW(nullptr));

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    DBLOG(GDX_SRC_LOC, "window created (", m_desc.width, "x", m_desc.height, ")");
    return true;
}

void GDXWin32Window::PollEvents()
{
    MSG msg = {};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool        GDXWin32Window::ShouldClose() const { return m_shouldClose; }
int         GDXWin32Window::GetWidth()    const { return m_width; }
int         GDXWin32Window::GetHeight()   const { return m_height; }
const char* GDXWin32Window::GetTitle()    const { return m_desc.title.c_str(); }

bool GDXWin32Window::QueryNativeHandles(GDXWin32NativeHandles& outHandles) const
{
    if (!m_handles.hwnd)
    {
        DBERROR(GDX_SRC_LOC, "QueryNativeHandles called before Create()");
        return false;
    }
    outHandles = m_handles;
    return true;
}

long long __stdcall GDXWin32Window::StaticWndProc(
    HWND__*            hwnd,
    unsigned int       msg,
    unsigned long long wparam,
    long long          lparam)
{
    if (msg == WM_NCCREATE)
    {
        auto* cs  = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* win = static_cast<GDXWin32Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(
            reinterpret_cast<HWND>(hwnd),
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(win));
        return DefWindowProcW(
            reinterpret_cast<HWND>(hwnd), msg,
            static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam));
    }

    auto* win = reinterpret_cast<GDXWin32Window*>(
        GetWindowLongPtrW(reinterpret_cast<HWND>(hwnd), GWLP_USERDATA));

    if (win)
        return win->WndProc(msg, wparam, lparam);

    return DefWindowProcW(
        reinterpret_cast<HWND>(hwnd), msg,
        static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam));
}

long long GDXWin32Window::WndProc(
    unsigned int       msg,
    unsigned long long wparam,
    long long          lparam)
{
    switch (msg)
    {
    case WM_CLOSE:
        m_shouldClose = true;
        m_events.Push(QuitEvent{});
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_SIZE:
    {
        const int w = LOWORD(static_cast<DWORD>(lparam));
        const int h = HIWORD(static_cast<DWORD>(lparam));
        OnResize(w, h);
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        const bool repeat = (lparam & (1LL << 30)) != 0;
        m_events.Push(KeyPressedEvent{
            TranslateKey(static_cast<WPARAM>(wparam)), repeat });
        return 0;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP:
        m_events.Push(KeyReleasedEvent{
            TranslateKey(static_cast<WPARAM>(wparam)) });
        return 0;

    default:
        return DefWindowProcW(
            reinterpret_cast<HWND>(m_handles.hwnd), msg,
            static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam));
    }
}

void GDXWin32Window::OnResize(int w, int h)
{
    m_width  = w;
    m_height = h;
    m_events.Push(WindowResizedEvent{ w, h });
}
