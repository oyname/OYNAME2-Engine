#pragma once

// Forward declarations keep this header free of windows.h.
// Concrete Win32 code casts void* back to HWND / HINSTANCE where needed.
struct HWND__;
struct HINSTANCE__;

struct GDXWin32NativeHandles
{
    HWND__*      hwnd      = nullptr;
    HINSTANCE__* hinstance = nullptr;
};
