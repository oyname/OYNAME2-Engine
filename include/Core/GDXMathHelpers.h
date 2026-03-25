#pragma once

#include "Core/GDXMath.h"
#include <DirectXMath.h>

namespace GDXMathHelpers
{
    inline DirectX::XMFLOAT2 ToDX(const Float2& v) noexcept { return { v.x, v.y }; }
    inline DirectX::XMFLOAT3 ToDX(const Float3& v) noexcept { return { v.x, v.y, v.z }; }
    inline DirectX::XMFLOAT4 ToDX(const Float4& v) noexcept { return { v.x, v.y, v.z, v.w }; }
    inline DirectX::XMUINT4 ToDX(const UInt4& v) noexcept { return { v.x, v.y, v.z, v.w }; }
    inline DirectX::XMFLOAT4X4 ToDX(const Matrix4& m) noexcept
    {
        DirectX::XMFLOAT4X4 out{};
        out._11=m._11; out._12=m._12; out._13=m._13; out._14=m._14;
        out._21=m._21; out._22=m._22; out._23=m._23; out._24=m._24;
        out._31=m._31; out._32=m._32; out._33=m._33; out._34=m._34;
        out._41=m._41; out._42=m._42; out._43=m._43; out._44=m._44;
        return out;
    }
    inline Float2 FromDX(const DirectX::XMFLOAT2& v) noexcept { return { v.x, v.y }; }
    inline Float3 FromDX(const DirectX::XMFLOAT3& v) noexcept { return { v.x, v.y, v.z }; }
    inline Float4 FromDX(const DirectX::XMFLOAT4& v) noexcept { return { v.x, v.y, v.z, v.w }; }
    inline UInt4 FromDX(const DirectX::XMUINT4& v) noexcept { return { v.x, v.y, v.z, v.w }; }
    inline Matrix4 FromDX(const DirectX::XMFLOAT4X4& v) noexcept
    {
        Matrix4 out{};
        out._11=v._11; out._12=v._12; out._13=v._13; out._14=v._14;
        out._21=v._21; out._22=v._22; out._23=v._23; out._24=v._24;
        out._31=v._31; out._32=v._32; out._33=v._33; out._34=v._34;
        out._41=v._41; out._42=v._42; out._43=v._43; out._44=v._44;
        return out;
    }

    inline DirectX::XMVECTOR LoadFloat2(const Float2& v) noexcept { const auto dx = ToDX(v); return DirectX::XMLoadFloat2(&dx); }
    inline DirectX::XMVECTOR LoadFloat3(const Float3& v) noexcept { const auto dx = ToDX(v); return DirectX::XMLoadFloat3(&dx); }
    inline DirectX::XMVECTOR LoadFloat4(const Float4& v) noexcept { const auto dx = ToDX(v); return DirectX::XMLoadFloat4(&dx); }
    inline DirectX::XMVECTOR LoadUInt4(const UInt4& v) noexcept { const auto dx = ToDX(v); return DirectX::XMLoadUInt4(&dx); }
    inline DirectX::XMMATRIX LoadFloat4x4(const Matrix4& m) noexcept { const auto dx = ToDX(m); return DirectX::XMLoadFloat4x4(&dx); }
    inline DirectX::XMMATRIX LoadMatrix4(const Matrix4& m) noexcept { return LoadFloat4x4(m); }
    inline void StoreFloat2(Float2& out, DirectX::FXMVECTOR v) noexcept { DirectX::XMFLOAT2 dx{}; DirectX::XMStoreFloat2(&dx, v); out = FromDX(dx); }
    inline void StoreFloat3(Float3& out, DirectX::FXMVECTOR v) noexcept { DirectX::XMFLOAT3 dx{}; DirectX::XMStoreFloat3(&dx, v); out = FromDX(dx); }
    inline void StoreFloat4(Float4& out, DirectX::FXMVECTOR v) noexcept { DirectX::XMFLOAT4 dx{}; DirectX::XMStoreFloat4(&dx, v); out = FromDX(dx); }
    inline void StoreUInt4(UInt4& out, DirectX::FXMVECTOR v) noexcept { DirectX::XMUINT4 dx{}; DirectX::XMStoreUInt4(&dx, v); out = FromDX(dx); }
    inline void StoreFloat4x4(Matrix4& out, DirectX::FXMMATRIX m) noexcept { DirectX::XMFLOAT4X4 dx{}; DirectX::XMStoreFloat4x4(&dx, m); out = FromDX(dx); }
    inline void StoreMatrix4(Matrix4& out, DirectX::FXMMATRIX m) noexcept { StoreFloat4x4(out, m); }
}
