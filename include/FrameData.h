#pragma once
#include "Core/GDXMath.h"
#include "Core/GDXMathOps.h"
#include <cstdint>
#include <array>

static constexpr uint32_t MAX_LIGHTS = 256u;
static constexpr uint32_t MAX_SHADOW_CASCADES = 4u;

struct LightEntry
{
    Float4 position = { 0.0f, 0.0f, 0.0f, 0.0f };
    Float4 direction = { 0.0f,-1.0f, 0.0f, 0.0f };
    Float4 diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
    float  radius = 0.0f;
    float  intensity = 1.0f;
    float  innerCosAngle = 0.9659f;
    float  outerCosAngle = 0.8660f;
};

struct FrameData
{
    Matrix4  viewMatrix = Matrix4::Identity();
    Matrix4  projMatrix = Matrix4::Identity();
    Matrix4  viewProjMatrix = Matrix4::Identity();
    Float3   cameraPos = { 0.0f, 0.0f, 0.0f };
    float    _padCameraPos = 0.0f;
    Float3   cameraForward = { 0.0f, 0.0f, 1.0f };
    float    cameraNearPlane = 0.1f;
    uint32_t cullMask = 0xFFFFFFFFu;
    float    cameraFarPlane = 1000.0f;
    uint32_t cameraProjectionFlags = 0u;

    std::array<LightEntry, MAX_LIGHTS> lights = {};
    uint32_t lightCount = 0u;

    Float3 sceneAmbient = { 0.08f, 0.08f, 0.12f };
    float  _padAmbient = 0.0f;

    Matrix4 shadowViewProjMatrix = Matrix4::Identity();
    Matrix4 shadowCascadeViewProj[MAX_SHADOW_CASCADES] = {};
    float   shadowCascadeSplits[MAX_SHADOW_CASCADES] = {};
    uint32_t shadowCascadeCount = 0u;
    bool     hasShadowPass = false;
    uint32_t shadowCasterMask = 0xFFFFFFFFu;
    uint32_t lightAffectMask = 0xFFFFFFFFu;

    float viewportWidth = 1280.0f;
    float viewportHeight = 720.0f;

    FrameData()
    {
        for (uint32_t i = 0; i < MAX_SHADOW_CASCADES; ++i)
        {
            shadowCascadeViewProj[i] = Matrix4::Identity();
        }
    }
};
