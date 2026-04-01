#pragma once

#include "GDXWin32NativeHandles.h"

// IGDXWin32NativeAccess is a Win32-only secondary interface that sits
// alongside IGDXWindow on GDXWin32Window.
//
// Why a separate interface instead of putting handles on IGDXWindow?
//   IGDXWindow must remain platform-neutral so Linux/macOS backends can
//   implement it without touching any Win32 type.  Putting HWND on it would
//   force every non-Windows backend to include windows.h or stub the member.
//
// Why not dynamic_cast from IGDXWindow?
//   dynamic_cast compiles silently and fails at runtime.  Anything that
//   needs Win32 handles is already Win32-specific code, so it can safely
//   demand IGDXWin32NativeAccess& directly.  A wrong type becomes a
//   compile error, not a null pointer at runtime.
//
// Usage:
//   Win32-side factories and context creators take IGDXWin32NativeAccess&.
//   main.cpp keeps a typed GDXWin32Window* before moving ownership to
//   the engine, so it can pass the same object as both types.

class IGDXWin32NativeAccess
{
public:
    virtual ~IGDXWin32NativeAccess() = default;

    // Returns false and leaves outHandles unchanged if the window has not
    // been successfully created yet (HWND is null).
    virtual bool QueryNativeHandles(GDXWin32NativeHandles& outHandles) const = 0;
    virtual bool IsBorderless() const = 0;
    virtual bool IsFullscreen() const = 0;
};
