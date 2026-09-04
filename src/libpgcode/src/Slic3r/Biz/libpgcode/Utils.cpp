#include "Slic3r/Biz/libpgcode/Utils.hpp"

#include <cstddef>
#include <assert.h>

namespace Slic3r::Biz::libpgcode {
using namespace Domain;

std::string_view producer_name(GCodeProducer producer)
{
    switch (producer)
    {
    case GCodeProducer::AnkerMakeStudio: { return "AnkerMake Studio"sv; }
    case GCodeProducer::BambuStudio:     { return "BambuStudio"sv; }
    case GCodeProducer::CraftWare:       { return "CraftWare"sv; }
    case GCodeProducer::Cura:            { return "Cura"sv; }
    case GCodeProducer::KISSlicer:       { return "KISSlicer"sv; }
    case GCodeProducer::ideaMaker:       { return "ideaMaker"sv; }
    case GCodeProducer::OrcaSlicer:      { return "OrcaSlicer"sv; }
    case GCodeProducer::PrusaSlicer:     { return "PrusaSlicer"sv; }
    case GCodeProducer::Simplify3D:      { return "Simplify3D"sv; }
    case GCodeProducer::Slic3r:          { return "Slic3r"sv; }
    case GCodeProducer::Slic3rPE:        { return "Slic3r Prusa Edition"sv; }
    case GCodeProducer::SuperSlicer:     { return "SuperSlicer"sv; }
    case GCodeProducer::XDesktop:        { return "XDesktop"sv; }
    default:                             { return "?"sv; }
    }
}

std::string_view to_string(TimeMode mode)
{
    switch (mode)
    {
    case TimeMode::Normal:  { return "normal"sv; }
    case TimeMode::Stealth: { return "silent"sv; }
    default:                { return "?"sv; }
    }
    return {};
}

// mapping from MoveType to OptionType
OptionType move_type_to_option(MoveType type)
{
    switch (type)
    {
    case MoveType::Travel:      { return OptionType::Travels; }
    case MoveType::Wipe:        { return OptionType::Wipes; }
    case MoveType::Retract:     { return OptionType::Retractions; }
    case MoveType::Unretract:   { return OptionType::Unretractions; }
    case MoveType::Seam:        { return OptionType::Seams; }
    case MoveType::ToolChange:  { return OptionType::ToolChanges; }
    case MoveType::ColorChange: { return OptionType::ColorChanges; }
    case MoveType::PausePrint:  { return OptionType::PausePrints; }
    case MoveType::CustomGCode: { return OptionType::CustomGCodes; }
    default:                    { return OptionType::COUNT; }
    }
}

float convert(float value, UnitsType value_units, UnitsType desired_units)
{
    assert(value_units < UnitsType::COUNT && desired_units < UnitsType::COUNT);
    const auto it = std::find_if(CONVERSIONS.begin(), CONVERSIONS.end(), 
      [value_units, desired_units](const Conversion& c) { return c.in == value_units && c.out == desired_units; });
    assert(value_units == desired_units || it != CONVERSIONS.end());
    return (it != CONVERSIONS.end()) ? it->conversion_function(value) : value;
}

bool supports_separate_travel_acceleration(GCodeFlavor flavor)
{
    return (
        flavor == GCodeFlavor::gcfRepetier
        || flavor == GCodeFlavor::gcfMarlinFirmware
        || flavor == GCodeFlavor::gcfPrusaFirmwareBuddy
        || flavor == GCodeFlavor::gcfRepRapFirmware
    );
}

std::string_view reserved_tag(Tags tag)
{
    assert(tag < Tags::COUNT);
    return RESERVED_TAGS[size_t(tag)];
}

std::string_view custom_tag(CustomTags tag)
{
    assert(tag < CustomTags::COUNT);
    return CUSTOM_TAGS[size_t(tag)];
}

bool is_hex_digit(const char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

bool is_whitespace(char c)
{
    return c == ' ' || c == '\t';
}

const char* skip_whitespaces(const char* c)
{
    for (; is_whitespace(*c); ++c)
        ; // silence -Wempty-body
    return c;
}

const char* skip_whitespaces(const char* begin, const char* end)
{
    for (; begin != end && is_whitespace(*begin); ++begin);
    return begin;
}

std::string skip_whitespaces(const std::string& str)
{
    return std::string(skip_whitespaces(std::string_view(str)));
}

std::string_view skip_whitespaces(const std::string_view str)
{
    size_t begin = 0;
    for (; begin < str.length() && is_whitespace(str[begin]); ++begin);
    return (begin == str.length()) ? str : str.substr(begin);
}

std::string skip_whitespaces_both_sides(const std::string& str)
{
    return std::string(skip_whitespaces_both_sides(std::string_view(str)));
}

std::string_view skip_whitespaces_both_sides(const std::string_view str)
{
    if (str.empty())
        return std::string_view();

    int begin = 0;
    for (; begin < int(str.length()) && is_whitespace(str[begin]); ++begin);
    if (begin == int(str.length()))
        return std::string_view();
    int end = int(str.length()) - 1;
    for (; begin < end && is_whitespace(str[end]); --end);
    return str.substr(begin, end - begin + 1);
}

bool is_valid_color(const std::string& color)
{
    if (color.length() != 7)
        return false;

    if (color.front() != '#')
        return false;

    for (int i = 1; i <= 6; ++i) {
        if (!is_hex_digit(color[i]))
            return false;
    }
    return true;
}

} // namespace Slic3r::Biz::libpgcode


