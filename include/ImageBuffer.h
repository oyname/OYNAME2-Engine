#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

class ImageBuffer
{
public:
    ImageBuffer() = default;
    ImageBuffer(uint32_t width, uint32_t height)
    {
        Reset(width, height);
    }

    void Reset(uint32_t width, uint32_t height)
    {
        m_width = width;
        m_height = height;
        m_pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u);
    }

    bool IsValid() const noexcept { return m_width > 0u && m_height > 0u && m_pixels.size() == static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 4u; }
    uint32_t Width() const noexcept { return m_width; }
    uint32_t Height() const noexcept { return m_height; }
    const uint8_t* Data() const noexcept { return m_pixels.empty() ? nullptr : m_pixels.data(); }
    uint8_t* Data() noexcept { return m_pixels.empty() ? nullptr : m_pixels.data(); }
    size_t SizeBytes() const noexcept { return m_pixels.size(); }

    void Fill(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u)
    {
        for (uint32_t y = 0; y < m_height; ++y)
            for (uint32_t x = 0; x < m_width; ++x)
                SetPixel(x, y, r, g, b, a);
    }

    void FillCheckerboard(uint32_t tileSize,
                          uint8_t r0, uint8_t g0, uint8_t b0, uint8_t a0,
                          uint8_t r1, uint8_t g1, uint8_t b1, uint8_t a1)
    {
        if (tileSize == 0u || !IsValid()) return;
        for (uint32_t y = 0; y < m_height; ++y)
        {
            for (uint32_t x = 0; x < m_width; ++x)
            {
                const bool odd = (((x / tileSize) + (y / tileSize)) & 1u) != 0u;
                if (odd) SetPixel(x, y, r1, g1, b1, a1);
                else     SetPixel(x, y, r0, g0, b0, a0);
            }
        }
    }

    void FlipVertical()
    {
        if (!IsValid()) return;
        const size_t rowBytes = static_cast<size_t>(m_width) * 4u;
        std::vector<uint8_t> tmp(rowBytes);
        for (uint32_t y = 0; y < m_height / 2u; ++y)
        {
            uint8_t* a = m_pixels.data() + static_cast<size_t>(y) * rowBytes;
            uint8_t* b = m_pixels.data() + static_cast<size_t>(m_height - 1u - y) * rowBytes;
            std::copy(a, a + rowBytes, tmp.data());
            std::copy(b, b + rowBytes, a);
            std::copy(tmp.data(), tmp.data() + rowBytes, b);
        }
    }

    void PremultiplyAlpha()
    {
        if (!IsValid()) return;
        for (size_t i = 0; i + 3 < m_pixels.size(); i += 4)
        {
            const uint32_t a = m_pixels[i + 3];
            m_pixels[i + 0] = static_cast<uint8_t>((static_cast<uint32_t>(m_pixels[i + 0]) * a) / 255u);
            m_pixels[i + 1] = static_cast<uint8_t>((static_cast<uint32_t>(m_pixels[i + 1]) * a) / 255u);
            m_pixels[i + 2] = static_cast<uint8_t>((static_cast<uint32_t>(m_pixels[i + 2]) * a) / 255u);
        }
    }

    void SetPixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u)
    {
        if (x >= m_width || y >= m_height) return;
        const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(m_width) + x) * 4u;
        m_pixels[idx + 0] = r;
        m_pixels[idx + 1] = g;
        m_pixels[idx + 2] = b;
        m_pixels[idx + 3] = a;
    }

    static ImageBuffer Solid(uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u)
    {
        ImageBuffer img(width, height);
        img.Fill(r, g, b, a);
        return img;
    }

    static ImageBuffer Checkerboard(uint32_t width, uint32_t height, uint32_t tileSize,
                                    uint8_t r0, uint8_t g0, uint8_t b0, uint8_t a0,
                                    uint8_t r1, uint8_t g1, uint8_t b1, uint8_t a1)
    {
        ImageBuffer img(width, height);
        img.FillCheckerboard(tileSize, r0, g0, b0, a0, r1, g1, b1, a1);
        return img;
    }

private:
    uint32_t m_width = 0u;
    uint32_t m_height = 0u;
    std::vector<uint8_t> m_pixels;
};
