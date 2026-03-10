#pragma once

// ---------------------------------------------------------------------------
// GDXOpenGLIncludes.h
//
// Single place that handles the platform-specific ceremony required before
// pulling in <GL/gl.h>.
//
// On Windows, <GL/gl.h> requires certain Windows SDK macros (WINGDIAPI,
// APIENTRY) to already be defined.  Rather than every .cpp that uses OpenGL
// having to remember to include <windows.h> first, this header encapsulates
// that dependency in one place.
//
// Rule: include THIS header instead of <GL/gl.h> directly.
// Rule: never include <windows.h> in renderer or context-interface code
//       just to satisfy GL — that's what this file is for.
// ---------------------------------------------------------------------------

#if defined(_WIN32)
    // Pull in only the minimum Windows SDK surface needed by <GL/gl.h>.
    // We deliberately do NOT expose the rest of windows.h to consumers of
    // this header — that remains a platform-module concern.
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#   pragma comment(lib, "opengl32.lib")
#endif

#include <GL/gl.h>
