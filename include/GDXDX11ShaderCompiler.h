#pragma once

#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcommon.h>

#include <vector>
#include <string>

std::wstring GDXDX11ResolveShaderPath(const std::wstring& file);

bool GDXDX11CompileShaderFromFile(const std::wstring& file,
                                  const char* entry,
                                  const char* target,
                                  ID3DBlob** outBlob,
                                  const std::vector<std::string>& defines = {});

// Loggt Cache-Statistiken (Hits/Misses/Failures/Zeiten) via Debug::Log.
// Aufrufen am Ende von Shutdown oder nach dem ersten Frame.
void GDXDX11LogShaderCacheStats();
