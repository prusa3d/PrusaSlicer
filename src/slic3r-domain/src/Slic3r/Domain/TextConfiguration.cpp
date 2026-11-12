#include "Slic3r/Domain/TextConfiguration.hpp"

namespace Slic3r::Domain {

bool is_approx(const std::optional<float>& value, const std::optional<float>& test_value)
{
    return (!value.has_value() && !test_value.has_value())
        || (value.has_value() && test_value.has_value() && is_approx(*value, *test_value));
}

FontProp::FontProp(float line_height) : per_glyph(false), size_in_mm(line_height) {}

bool FontProp::operator==(const FontProp& other) const
{
    return char_gap == other.char_gap
        && line_gap == other.line_gap
        && per_glyph == other.per_glyph
        && align == other.align
        && is_approx(size_in_mm, other.size_in_mm)
        && is_approx(boldness, other.boldness)
        && is_approx(skew, other.skew);
}

bool FontProp::Align::operator==(const Align& other) const {
    return horizontal == other.horizontal
        && vertical == other.vertical;
}

bool FontDescriptor::operator==(const FontDescriptor& other) const
{
    return type == other.type && name == other.name && path == other.path;
}

bool EmbossStyle::operator==(const EmbossStyle& other) const
{
    return descriptor == other.descriptor && prop == other.prop;
}

} // namespace Slic3r::Domain
