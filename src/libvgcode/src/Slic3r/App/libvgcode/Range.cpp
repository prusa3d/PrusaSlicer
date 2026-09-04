#include "Slic3r/App/libvgcode/Range.hpp"

#include <algorithm>

namespace Slic3r::App::libvgcode {

void Range::set(Interval::value_type min, Interval::value_type max)
{
    if (max < min)
        std::swap(min, max);
    m_range[0] = min;
    m_range[1] = max;
}

void Range::clamp(Range& other)
{
    other.m_range[0] = std::clamp(other.m_range[0], m_range[0], m_range[1]);
    other.m_range[1] = std::clamp(other.m_range[1], m_range[0], m_range[1]);
}

} // namespace Slic3r::App::libvgcode
