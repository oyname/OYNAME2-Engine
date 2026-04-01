#pragma once

#include <chrono>
#include <algorithm>

class GDXFrameTimer
{
public:
    using Clock = std::chrono::steady_clock;
    using Seconds = std::chrono::duration<float>;

    void Reset()
    {
        m_last = Clock::now();
        m_deltaTime = 0.0f;
        m_totalTime = 0.0f;
        m_initialized = true;
    }

    float Tick()
    {
        const auto now = Clock::now();

        if (!m_initialized)
        {
            m_last = now;
            m_deltaTime = 0.0f;
            m_totalTime = 0.0f;
            m_initialized = true;
            return m_deltaTime;
        }

        m_deltaTime = std::chrono::duration_cast<Seconds>(now - m_last).count();
        m_last = now;

        // Schutz gegen kaputte Ausreißer nach Breakpoints / Alt-Tab / Hänger.
        m_deltaTime = (std::min)(m_deltaTime, 0.25f);
        if (m_deltaTime < 0.0f)
            m_deltaTime = 0.0f;

        m_totalTime += m_deltaTime;
        return m_deltaTime;
    }

    float GetDeltaTime() const { return m_deltaTime; }
    float GetTotalTime() const { return m_totalTime; }

private:
    Clock::time_point m_last{};
    bool  m_initialized = false;
    float m_deltaTime = 0.0f;
    float m_totalTime = 0.0f;
};
