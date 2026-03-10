#pragma once

#include <memory>

class IGDXOpenGLContext;
class IGDXWin32NativeAccess;

// GDXWin32OpenGLContextFactory builds a WGL OpenGL context for a Win32 window.
//
// It takes IGDXWin32NativeAccess& directly — not IGDXWindow& — because it is
// already Win32-specific code and must not pretend to be generic.  The
// caller in main.cpp holds a typed GDXWin32Window* and passes it as both
// IGDXWindow and IGDXWin32NativeAccess without any cast at runtime.
//
// This avoids the dynamic_cast anti-pattern: wrong-type errors become
// compile errors, not silent nullptr returns at runtime.

class GDXWin32OpenGLContextFactory
{
public:
    // Returns nullptr on failure (logs via Debug::LogError).
    std::unique_ptr<IGDXOpenGLContext> Create(IGDXWin32NativeAccess& nativeAccess) const;
};
