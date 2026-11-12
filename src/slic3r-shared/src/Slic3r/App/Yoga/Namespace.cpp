#include "Slic3r/App/Yoga/Namespace.hpp"

namespace Slic3r::App::Yoga {

int SizeInfo::viewport_max() const
{
    return std::max(viewport_size_x, viewport_size_y);
}

int SizeInfo::viewport_min() const
{
    return std::min(viewport_size_x, viewport_size_y);
}

bool SizeInfo::operator==(const SizeInfo& other) const
{
    return dpi == other.dpi
        && Domain::fuzzy_compare(dpi_scale_factor, other.dpi_scale_factor)
        && Domain::fuzzy_compare(root_font_size, other.root_font_size)
        && Domain::fuzzy_compare(root_font_size_pt, other.root_font_size_pt)
        && viewport_size_x == other.viewport_size_x
        && viewport_size_y == other.viewport_size_y;
}

bool SizeInfo::operator!=(const SizeInfo& other) const
{
    return !(*this == other);
}

EvaluatedUnit::EvaluatedUnit(const Unit& source) : source(source), result(source.value) {}

void EvaluatedUnit::evaluate(const SizeInfo& size_info)
{
    switch (source.type) {
    case Unit::Type::Pixel:
        result = source.value;
        break;
    case Unit::Type::FigmaPixel:
        result = source.value * (size_info.root_font_size_pt / 11.f);
        break;
    // case Unit::Type::Milimeter:
    // result = source.value * static_cast<float>(size_info.dpi) / size_info.dpi_scale_factor / 25.4f;
    // break;
    case Unit::Type::Point:
        result = source.value * 1.3333f;
        // Unable to use full equation as MacOs is increasing scale factor AND dpi on retina
        // source.value * static_cast<float>(size_info.dpi) / size_info.dpi_scale_factor / 72.f;
        break;
    case Unit::Type::Rem:
        result = source.value * size_info.root_font_size;
        break;
    case Unit::Type::ViewportHeight:
        result = source.value * 0.01f * static_cast<float>(size_info.viewport_size_y);
        break;
    case Unit::Type::ViewportWidth:
        result = source.value * 0.01f * static_cast<float>(size_info.viewport_size_x);
        break;
    case Unit::Type::ViewportMax:
        result = source.value * 0.01f * static_cast<float>(size_info.viewport_max());
        break;
    case Unit::Type::ViewportMin:
        result = source.value * 0.01f * static_cast<float>(size_info.viewport_min());
        break;
    }

    result = std::round(result);
}

YGAlign Align::get_yoga_v_align() const
{
    switch (vertical) {
    case AlignV::Top:
        return YGAlignFlexStart;
    case AlignV::Center:
        return YGAlignCenter;
    case AlignV::Bottom:
        return YGAlignFlexEnd;
    }
    return {};
}

YGAlign Align::get_yoga_h_align() const
{
    switch (horizontal) {
    case AlignH::Left:
        return YGAlignFlexStart;
    case AlignH::Center:
        return YGAlignCenter;
    case AlignH::Right:
        return YGAlignFlexEnd;
    }
    return {};
}

bool Unit::operator==(const Unit& other) const
{
    return type == other.type && Domain::fuzzy_compare(value, other.value);
}

bool Unit::operator!=(const Unit& other) const
{
    return !(*this == other);
}

float EvaluatedSides::result_horizontal() const
{
    return left + right;
}

float EvaluatedSides::result_vertical() const
{
    return bottom + top;
}

void EvaluatedSides::evaluate(const SizeInfo& size_info)
{
    EvaluatedUnit l{source.left}, r{source.right}, t{source.top}, b{source.bottom};
    l.evaluate(size_info);
    r.evaluate(size_info);
    t.evaluate(size_info);
    b.evaluate(size_info);
    left   = l.result;
    right  = r.result;
    top    = t.result;
    bottom = b.result;
}

Unit operator*(Unit unit, float scalar)
{
    unit *= scalar;
    return unit;
}

Unit operator*(float scalar, Unit unit)
{
    unit *= scalar;
    return unit;
}

Unit operator/(Unit unit, float scalar)
{
    unit /= scalar;
    return unit;
}

Unit operator+(Unit lhs, Unit rhs)
{
    ASSERT(lhs.type == rhs.type);
    return {lhs.value + rhs.value, lhs.type};
}

Unit operator-(Unit lhs, Unit rhs)
{
    ASSERT(lhs.type == rhs.type);
    return {lhs.value - rhs.value, lhs.type};
}

Unit operator*(Unit lhs, Unit rhs)
{
    ASSERT(lhs.type == rhs.type);
    return {lhs.value * rhs.value, lhs.type};
}

Unit operator/(Unit lhs, Unit rhs)
{
    ASSERT(lhs.type == rhs.type);
    return {lhs.value / rhs.value, lhs.type};
}

} // namespace Slic3r::App::Yoga
