#include "Slic3r/App/libvgcode/ColorRange.hpp"
#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Domain/Color.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace Slic3r::App::libvgcode {

const ColorRange ColorRange::DUMMY_COLOR_RANGE = ColorRange();

static float get_step_size(const ColorRange& color_range)
{
    const std::array<float, 2>& range = color_range.range();
    const Palette& palette = color_range.palette();
    switch (color_range.type())
    {
    default:
    case ColorRangeType::Linear:
    {
        return (range[1] - range[0]) / (float(palette.size()) - 1.0f);
    }
    case ColorRangeType::Logarithmic:
    {
        return (range[0] != 0.0f) ? std::log(range[1] / range[0]) / (float(palette.size()) - 1.0f) : 0.0f;
    }
    }
}

ColorRange::ColorRange(ColorRangeType type)
: m_type(type)
, m_palette(DEFAULT_RANGES_COLORS)
{
}

ColorRangeType ColorRange::type() const
{
    return m_type;
}

const Palette& ColorRange::palette() const
{
    return m_palette;
}

void ColorRange::set_palette(const Palette& palette)
{
    if (palette.size() > 1)
        m_palette = palette;
}

Domain::ColorRGB ColorRange::color_at(float value) const
{
    // Input value scaled to the colors range
    float global_t = 0.0f;
    value = std::clamp(value, m_range[0], m_range[1]);
    float step = get_step_size(*this);
    if (step > 0.0f) {
        if (m_type == ColorRangeType::Logarithmic) {
            if (m_range[0] != 0.0f)
                global_t = std::log(value / m_range[0]) / step;
        }
        else
            global_t = (value - m_range[0]) / step;
    }

    size_t color_max_idx = m_palette.size() - 1;

    // Compute the two colors just below (low) and above (high) the input value
    size_t color_low_idx = std::clamp<size_t>(size_t(global_t), 0, color_max_idx);
    size_t color_high_idx = std::clamp<size_t>(color_low_idx + 1, 0, color_max_idx);

    // Interpolate between the low and high colors to find exactly which color the input value should get
    return Biz::Algorithms::Color::lerp(m_palette[color_low_idx], m_palette[color_high_idx], global_t - float(color_low_idx));
}

const std::array<float, 2>& ColorRange::range() const
{
    return m_range;
}

std::vector<float> ColorRange::values() const
{
    std::vector<float> ret;

    if (m_count == 1) {
        // single item use case
        ret.emplace_back(m_range[0]);
    }
    else if (m_count == 2) {
        // two items use case
        ret.emplace_back(m_range[0]);
        ret.emplace_back(m_range[1]);
    }
    else {
        float step_size = get_step_size(*this);
        for (size_t i = 0; i < m_palette.size(); ++i) {
            float value = 0.0f;
            switch (m_type)
            {
            default:
            case ColorRangeType::Linear:      { value = m_range[0] + float(i) * step_size; break; }
            case ColorRangeType::Logarithmic: { value = std::exp(std::log(m_range[0]) + float(i) * step_size);  break; }
            }
            ret.emplace_back(value);
        }
    }

    return ret;
}

void ColorRange::update(float value)
{
    if (value != m_range[0] && value != m_range[1])
        ++m_count;

    m_range[0] = std::min(m_range[0], value);
    m_range[1] = std::max(m_range[1], value);
}

void ColorRange::reset()
{
    m_range = { FLT_MAX, -FLT_MAX };
    m_count = 0;
}

} // namespace Slic3r::App::libvgcode

