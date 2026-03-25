#pragma once

#include <cstdint>

struct Float2
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Float3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Float4
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct UInt4
{
    uint32_t x = 0u;
    uint32_t y = 0u;
    uint32_t z = 0u;
    uint32_t w = 0u;
};

struct Matrix4
{
    union
    {
        float m[4][4];
        struct
        {
            float _11, _12, _13, _14;
            float _21, _22, _23, _24;
            float _31, _32, _33, _34;
            float _41, _42, _43, _44;
        };
    };

    Matrix4() : m{} {}

    static Matrix4 Identity()
    {
        Matrix4 out{};
        out._11 = 1.0f;
        out._22 = 1.0f;
        out._33 = 1.0f;
        out._44 = 1.0f;
        return out;
    }
};
