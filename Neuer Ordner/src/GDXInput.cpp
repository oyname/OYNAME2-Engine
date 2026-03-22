#include "GDXInput.h"

#include <type_traits>
#include <variant>

std::array<bool, GDXInput::KEY_COUNT> GDXInput::s_down{};
std::array<bool, GDXInput::KEY_COUNT> GDXInput::s_hit{};
std::array<bool, GDXInput::KEY_COUNT> GDXInput::s_released{};

std::size_t GDXInput::ToIndex(Key key)
{
    return static_cast<std::size_t>(key);
}

void GDXInput::BeginFrame()
{
    s_hit.fill(false);
    s_released.fill(false);
}

bool GDXInput::KeyDown(Key key)
{
    const std::size_t i = ToIndex(key);
    return (i < KEY_COUNT) ? s_down[i] : false;
}

bool GDXInput::KeyHit(Key key)
{
    const std::size_t i = ToIndex(key);
    return (i < KEY_COUNT) ? s_hit[i] : false;
}

bool GDXInput::KeyReleased(Key key)
{
    const std::size_t i = ToIndex(key);
    return (i < KEY_COUNT) ? s_released[i] : false;
}

void GDXInput::OnEvent(const Event& e)
{
    std::visit([](auto&& ev)
    {
        using T = std::decay_t<decltype(ev)>;

        if constexpr (std::is_same_v<T, KeyPressedEvent>)
        {
            const std::size_t i = GDXInput::ToIndex(ev.key);
            if (i >= GDXInput::KEY_COUNT) return;

            if (!GDXInput::s_down[i])
                GDXInput::s_hit[i] = true;

            GDXInput::s_down[i] = true;
        }
        else if constexpr (std::is_same_v<T, KeyReleasedEvent>)
        {
            const std::size_t i = GDXInput::ToIndex(ev.key);
            if (i >= GDXInput::KEY_COUNT) return;

            GDXInput::s_down[i] = false;
            GDXInput::s_released[i] = true;
        }
    }, e);
}
