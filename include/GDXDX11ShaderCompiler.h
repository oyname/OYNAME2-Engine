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

// ---------------------------------------------------------------------------
// GDXDX11PrecompileAllShaders — kompiliert alle Shader und füllt den Disk-Cache.
//
// Wird im Dev-Build vor dem Shipping-Build aufgerufen (z.B. als Build-Step).
// Shipping-Build setzt KROM_SHIPPING → D3DCompileFromFile nie aufgerufen.
//
// shaderDir: Verzeichnis mit .hlsl-Dateien (z.B. L"F:/proj/shader")
// Gibt Anzahl fehlgeschlagener Compiles zurück (0 = alles ok).
// ---------------------------------------------------------------------------
#if !defined(KROM_SHIPPING)
uint32_t GDXDX11PrecompileAllShaders(const std::wstring& shaderDir);
#endif
