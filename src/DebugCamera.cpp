#include "DebugCamera.h"
#include "FrameData.h"
#include "GDXInput.h"

#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// Orientierung
// ---------------------------------------------------------------------------

void DebugCamera::SetYawPitch(float yawDeg, float pitchDeg)
{
    m_yawDeg   = yawDeg;
    m_pitchDeg = std::clamp(pitchDeg, -89.f, 89.f);
}

GIDX::Float3 DebugCamera::ForwardVector() const
{
    const float yR = m_yawDeg   * (3.14159265f / 180.f);
    const float pR = m_pitchDeg * (3.14159265f / 180.f);
    return GIDX::Normalize3({
        std::sin(yR) * std::cos(pR),
        -std::sin(pR),
        std::cos(yR) * std::cos(pR)
    });
}

GIDX::Float3 DebugCamera::RightVector() const
{
    const GIDX::Float3 fwd = ForwardVector();
    const GIDX::Float3 up  = { 0.f, 1.f, 0.f };
    return GIDX::Normalize3(GIDX::Cross(fwd, up));
}

GIDX::Float3 DebugCamera::UpVector() const
{
    return GIDX::Cross(RightVector(), ForwardVector());
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void DebugCamera::ProcessInput(float deltaTime)
{
    if (!m_enabled) return;

    const float speed = GDXInput::KeyDown(Key::Space)
        ? moveSpeed * fastMultiplier
        : moveSpeed;
    const float move  = speed * deltaTime;
    const float turn  = turnSpeedDeg * deltaTime;

    // Translation
    const GIDX::Float3 fwd   = ForwardVector();
    const GIDX::Float3 right = RightVector();

    if (GDXInput::KeyDown(Key::W))
        m_position = GIDX::Add(m_position, GIDX::Scale3(fwd,   move));
    if (GDXInput::KeyDown(Key::S))
        m_position = GIDX::Add(m_position, GIDX::Scale3(fwd,  -move));
    if (GDXInput::KeyDown(Key::D))
        m_position = GIDX::Add(m_position, GIDX::Scale3(right,  move));
    if (GDXInput::KeyDown(Key::A))
        m_position = GIDX::Add(m_position, GIDX::Scale3(right, -move));
    if (GDXInput::KeyDown(Key::E))
        m_position.y += move;
    if (GDXInput::KeyDown(Key::Q))
        m_position.y -= move;

    // Rotation via Pfeiltasten
    if (GDXInput::KeyDown(Key::Left))  m_yawDeg   -= turn;
    if (GDXInput::KeyDown(Key::Right)) m_yawDeg   += turn;
    if (GDXInput::KeyDown(Key::Up))    m_pitchDeg -= turn;
    if (GDXInput::KeyDown(Key::Down))  m_pitchDeg += turn;

    m_pitchDeg = std::clamp(m_pitchDeg, -89.f, 89.f);
}

// ---------------------------------------------------------------------------
// Matrizen
// ---------------------------------------------------------------------------

GIDX::Float4x4 DebugCamera::ViewMatrix() const
{
    const GIDX::Float3 target = GIDX::Add(m_position, ForwardVector());
    return GIDX::LookAtLH(m_position, target, { 0.f, 1.f, 0.f });
}

GIDX::Float4x4 DebugCamera::ProjMatrix(float aspect) const
{
    return GIDX::PerspectiveFovLH(
        m_fovDeg * (3.14159265f / 180.f),
        aspect,
        m_near,
        m_far);
}

// ---------------------------------------------------------------------------
// FrameData überschreiben
// ---------------------------------------------------------------------------

void DebugCamera::OverrideFrameData(FrameData& f) const
{
    const float aspect = (f.viewportHeight > 0)
        ? static_cast<float>(f.viewportWidth) / static_cast<float>(f.viewportHeight)
        : 16.f / 9.f;

    f.viewMatrix     = ViewMatrix();
    f.projMatrix     = ProjMatrix(aspect);
    f.viewProjMatrix = GIDX::Multiply(f.viewMatrix, f.projMatrix);
    f.cameraPos      = m_position;
    f.cameraForward  = ForwardVector();
}
