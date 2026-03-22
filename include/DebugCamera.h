#pragma once

// ---------------------------------------------------------------------------
// DebugCamera — unabhängige Fly-Cam für Scene-Inspektion.
//
// Kein ECS, kein DX11, keine Engine-Abhängigkeiten außer GDXMath.h + GDXInput.h.
//
// Wenn aktiviert: ersetzt viewMatrix/projMatrix/viewProjMatrix im FrameData
// des Main-Views. Gameplay, Physik, RTT, Shadow — alles läuft mit der
// echten Kamera weiter.
//
// Steuerung (solange enabled == true):
//   W/S         — vorwärts / rückwärts
//   A/D         — links / rechts
//   Q/E         — runter / hoch
//   Space       — 5× schneller
//   Up/Down     — Pitch
//   Left/Right  — Yaw
// ---------------------------------------------------------------------------

#include "GDXMath.h"
#include <cstdint>

struct FrameData;   // forward — vollständige Definition in FrameData.h

class DebugCamera
{
public:
    DebugCamera() = default;

    // -------------------------------------------------------------------
    // Aktivierung
    // -------------------------------------------------------------------
    void   SetEnabled(bool enabled) { m_enabled = enabled; }
    bool   IsEnabled()        const { return m_enabled; }
    void   Toggle()                 { m_enabled = !m_enabled; }

    // -------------------------------------------------------------------
    // Position + Orientierung
    // -------------------------------------------------------------------
    void   SetPosition(const GIDX::Float3& pos)         { m_position = pos; }
    void   SetYawPitch(float yawDeg, float pitchDeg);

    GIDX::Float3 GetPosition()   const { return m_position; }
    float         GetYaw()        const { return m_yawDeg;   }
    float         GetPitch()      const { return m_pitchDeg; }

    // -------------------------------------------------------------------
    // Projektion
    // -------------------------------------------------------------------
    void  SetFovDeg(float fov)         { m_fovDeg  = fov;  }
    void  SetNearFar(float n, float f) { m_near = n; m_far = f; }
    float GetFovDeg()  const { return m_fovDeg; }

    // -------------------------------------------------------------------
    // Navigation — einmal pro Frame aufrufen wenn aktiviert.
    // Liest GDXInput::KeyDown() — kein Maus-Delta (Maus-Support später
    // über MouseMovedEvent ergänzbar wenn Events.h erweitert wird).
    // -------------------------------------------------------------------
    void ProcessInput(float deltaTime);

    // -------------------------------------------------------------------
    // Matrizen
    // -------------------------------------------------------------------
    GIDX::Float4x4 ViewMatrix()              const;
    GIDX::Float4x4 ProjMatrix(float aspect)  const;

    // Überschreibt viewMatrix, projMatrix, viewProjMatrix, cameraPos,
    // cameraForward in outFrame.  Nur aufrufen wenn IsEnabled().
    void OverrideFrameData(FrameData& outFrame) const;

    // -------------------------------------------------------------------
    // Bewegungsgeschwindigkeit
    // -------------------------------------------------------------------
    float moveSpeed      = 8.0f;   // Einheiten/s
    float fastMultiplier = 5.0f;   // Shift-Faktor
    float turnSpeedDeg   = 90.0f;  // Grad/s für Tasten-Rotation

private:
    GIDX::Float3 ForwardVector() const;
    GIDX::Float3 RightVector()   const;
    GIDX::Float3 UpVector()      const;

    bool          m_enabled  = false;
    GIDX::Float3  m_position = { 0.f, 2.f, -5.f };
    float         m_yawDeg   = 0.f;    // Y-Achse
    float         m_pitchDeg = 0.f;    // X-Achse, ±89°

    float         m_fovDeg   = 60.f;
    float         m_near     = 0.1f;
    float         m_far      = 1000.f;
};
