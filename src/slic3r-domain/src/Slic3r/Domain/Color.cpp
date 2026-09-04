#include "Slic3r/Domain/Color.hpp"

#include <algorithm>

namespace Slic3r::Domain {

const constexpr float INV_255 = 1.f / 255.f;

ColorRGB::ColorRGB(float r, float g, float b):
    m_data({std::clamp(r, 0.0f, 1.0f), std::clamp(g, 0.0f, 1.0f), std::clamp(b, 0.0f, 1.0f)})
{}

ColorRGB::ColorRGB(unsigned char r, unsigned char g, unsigned char b):
    m_data(
        {std::clamp(r * INV_255, 0.0f, 1.0f),
         std::clamp(g * INV_255, 0.0f, 1.0f),
         std::clamp(b * INV_255, 0.0f, 1.0f)}
    )
{}

bool ColorRGB::operator==(const ColorRGB& other) const
{
    return m_data == other.m_data;
}

bool ColorRGB::operator!=(const ColorRGB& other) const
{
    return !operator==(other);
}

bool ColorRGB::operator<(const ColorRGB& other) const
{
    for (size_t i = 0; i < 3; ++i) {
        if (m_data[i] < other.m_data[i])
            return true;
        else if (m_data[i] > other.m_data[i])
            return false;
    }

    return false;
}

bool ColorRGB::operator>(const ColorRGB& other) const
{
    for (size_t i = 0; i < 3; ++i) {
        if (m_data[i] > other.m_data[i])
            return true;
        else if (m_data[i] < other.m_data[i])
            return false;
    }

    return false;
}

ColorRGB ColorRGB::operator+(const ColorRGB& other) const
{
    ColorRGB ret;
    for (size_t i = 0; i < 3; ++i) {
        ret.m_data[i] = std::clamp(m_data[i] + other.m_data[i], 0.0f, 1.0f);
    }

    return ret;
}

ColorRGB ColorRGB::operator*(float value) const
{
    assert(value >= 0.0f);
    ColorRGB ret;
    for (size_t i = 0; i < 3; ++i) {
        ret.m_data[i] = std::clamp(value * m_data[i], 0.0f, 1.0f);
    }

    return ret;
}

void ColorRGB::set(unsigned int comp, float value)
{
    assert(0 <= comp && comp <= 2);
    m_data[comp] = std::clamp(value, 0.0f, 1.0f);
}

ColorRGBA::ColorRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a):
    m_data(
        {std::clamp(r * INV_255, 0.0f, 1.0f),
         std::clamp(g * INV_255, 0.0f, 1.0f),
         std::clamp(b * INV_255, 0.0f, 1.0f),
         std::clamp(a * INV_255, 0.0f, 1.0f)}
    )
{}

bool ColorRGBA::operator==(const ColorRGBA& other) const
{
    return m_data == other.m_data;
}

bool ColorRGBA::operator!=(const ColorRGBA& other) const
{
    return !operator==(other);
}

bool ColorRGBA::operator<(const ColorRGBA& other) const
{
    for (size_t i = 0; i < 3; ++i) {
        if (m_data[i] < other.m_data[i])
            return true;
        else if (m_data[i] > other.m_data[i])
            return false;
    }

    return false;
}

bool ColorRGBA::operator>(const ColorRGBA& other) const
{
    for (size_t i = 0; i < 3; ++i) {
        if (m_data[i] > other.m_data[i])
            return true;
        else if (m_data[i] < other.m_data[i])
            return false;
    }

    return false;
}

ColorRGBA ColorRGBA::operator+(const ColorRGBA& other) const
{
    ColorRGBA ret;
    for (size_t i = 0; i < 3; ++i) {
        ret.m_data[i] = std::clamp(m_data[i] + other.m_data[i], 0.0f, 1.0f);
    }
    return ret;
}

ColorRGBA ColorRGBA::operator*(float value) const
{
    assert(value >= 0.0f);
    ColorRGBA ret;
    for (size_t i = 0; i < 3; ++i) {
        ret.m_data[i] = std::clamp(value * m_data[i], 0.0f, 1.0f);
    }
    ret.m_data[3] = m_data[3];
    return ret;
}

void ColorRGBA::set(unsigned int comp, float value)
{
    assert(0 <= comp && comp <= 3);
    m_data[comp] = std::clamp(value, 0.0f, 1.0f);
}

ColorRGB operator*(float value, const ColorRGB& other)
{
    return other * value;
}

ColorRGBA operator*(float value, const ColorRGBA& other)
{
    return other * value;
}

} // namespace Slic3r::Domain
