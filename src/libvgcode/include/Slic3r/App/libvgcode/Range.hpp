#pragma once

#include "Slic3r/App/libvgcode/Types.hpp"

namespace Slic3r::App::libvgcode {

class Range
{
public:
    const Interval& get() const { return m_range; }
    void set(const Range& other) { m_range = other.m_range; }
    void set(const Interval& range) { set(range[0], range[1]); }
    void set(Interval::value_type min, Interval::value_type max);
    
    Interval::value_type min() const { return m_range[0]; }
    void set_min(Interval::value_type min) { set(min, m_range[1]); }

    Interval::value_type max() const { return m_range[1]; }
    void set_max(Interval::value_type max) { set(m_range[0], max); }

    // clamp the given range to stay inside this range
    void clamp(Range& other);
    void reset() { m_range = { 0, 0 }; }

    bool operator == (const Range& other) const { return m_range == other.m_range; }
    bool operator != (const Range& other) const { return m_range != other.m_range; }

private:
    Interval m_range{ 0, 0 };
};

} // namespace Slic3r::App::libvgcode
