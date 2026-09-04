#pragma once

#include "Slic3r/Biz/libpgcode/Types.hpp"
#include "Slic3r/Domain/GCodeFlavor.hpp"

namespace Slic3r::Biz::libpgcode {

std::string_view producer_name(GCodeProducer producer);

std::string_view to_string(TimeMode mode);

//
// Mapping from MoveType to OptionType
// Returns OptionType::COUNT if the given move type does not correspond
// to any option type.
//
OptionType move_type_to_option(MoveType type);

/** @brief Convert the given value from value_units to desired_units.
 *
 * @param value The value to convert.
 * @param value_units The initial units.
 * @param desired_units The final units.
 */
float convert(float value, UnitsType value_units, UnitsType desired_units);

bool supports_separate_travel_acceleration(Domain::GCodeFlavor flavor);

std::string_view reserved_tag(Tags tag);
std::string_view custom_tag(CustomTags tag);

bool is_hex_digit(const char c);
bool is_whitespace(char c);
const char* skip_whitespaces(const char* c);
const char* skip_whitespaces(const char* begin, const char* end);
std::string skip_whitespaces(const std::string& str);
std::string_view skip_whitespaces(const std::string_view str);
std::string skip_whitespaces_both_sides(const std::string& str);
std::string_view skip_whitespaces_both_sides(const std::string_view str);

/** @brief Checks if the given string contains a color in format #RRGGBB.
 *
 * @param color The string to check.
 * @return true If the string is a color in format #RRGGBB
 */
bool is_valid_color(const std::string& color);

} // namespace Slic3r::Biz::libpgcode

