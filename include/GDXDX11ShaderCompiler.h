#pragma once

#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcommon.h>

std::wstring GDXDX11ResolveShaderPath(const std::wstring& file);
bool GDXDX11CompileShaderFromFile(const std::wstring& file,
                                  const char* entry,
                                  const char* target,
                                  ID3DBlob** outBlob);
