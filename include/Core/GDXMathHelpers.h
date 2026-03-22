#pragma once

// ---------------------------------------------------------------------------
// GDXMathHelpers — DirectX-spezifische Math-Hilfsfunktionen (XMVECTOR/XMMATRIX).
// Hinweis: Dieser Header ist DX-abhängig (DirectXMath.h).
// Für ein backend-neutrales OpenGL- oder Vulkan-Ziel muss ein äquivalenter
// Header ohne DirectXMath.h bereitgestellt werden.
// ---------------------------------------------------------------------------

#include "Core/GDXMath.h"
#include <DirectXMath.h>

namespace GDXMathHelpers
{
    inline DirectX::XMVECTOR LoadFloat2(const GIDX::Float2& v) noexcept
    {
        const DirectX::XMFLOAT2 dx = GDXMathDX::ToDX(v);
        return DirectX::XMLoadFloat2(&dx);
    }

    inline DirectX::XMVECTOR LoadFloat3(const GIDX::Float3& v) noexcept
    {
        const DirectX::XMFLOAT3 dx = GDXMathDX::ToDX(v);
        return DirectX::XMLoadFloat3(&dx);
    }

    inline DirectX::XMVECTOR LoadFloat4(const GIDX::Float4& v) noexcept
    {
        const DirectX::XMFLOAT4 dx = GDXMathDX::ToDX(v);
        return DirectX::XMLoadFloat4(&dx);
    }

    inline DirectX::XMVECTOR LoadUInt4(const GIDX::UInt4& v) noexcept
    {
        const DirectX::XMUINT4 dx = GDXMathDX::ToDX(v);
        return DirectX::XMLoadUInt4(&dx);
    }

    inline DirectX::XMMATRIX LoadFloat4x4(const GIDX::Float4x4& m) noexcept
    {
        const DirectX::XMFLOAT4X4 dx = GDXMathDX::ToDX(m);
        return DirectX::XMLoadFloat4x4(&dx);
    }

    inline void StoreFloat2(GIDX::Float2& out, DirectX::FXMVECTOR v) noexcept
    {
        DirectX::XMFLOAT2 dx{};
        DirectX::XMStoreFloat2(&dx, v);
        out = GDXMathDX::FromDX(dx);
    }

    inline void StoreFloat3(GIDX::Float3& out, DirectX::FXMVECTOR v) noexcept
    {
        DirectX::XMFLOAT3 dx{};
        DirectX::XMStoreFloat3(&dx, v);
        out = GDXMathDX::FromDX(dx);
    }

    inline void StoreFloat4(GIDX::Float4& out, DirectX::FXMVECTOR v) noexcept
    {
        DirectX::XMFLOAT4 dx{};
        DirectX::XMStoreFloat4(&dx, v);
        out = GDXMathDX::FromDX(dx);
    }

    inline void StoreUInt4(GIDX::UInt4& out, DirectX::FXMVECTOR v) noexcept
    {
        DirectX::XMUINT4 dx{};
        DirectX::XMStoreUInt4(&dx, v);
        out = GDXMathDX::FromDX(dx);
    }

    inline void StoreFloat4x4(GIDX::Float4x4& out, DirectX::FXMMATRIX m) noexcept
    {
        DirectX::XMFLOAT4X4 dx{};
        DirectX::XMStoreFloat4x4(&dx, m);
        out = GDXMathDX::FromDX(dx);
    }
}
