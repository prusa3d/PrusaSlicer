#pragma once

#include <Slic3r/Biz/libpgcode/Types.hpp>

namespace Slic3r::Biz {

/** @brief Format the given value into a string with units.
 *
 * @param value The value to format.
 * @param units The units.
 * @param decimals The number of decimals to add after the point.
 */
std::string format_units(float value, Biz::libpgcode::UnitsType units, uint8_t decimals = 0);

/** @brief Convert the given value from value_units to desired_units.
 *         and format the result into a string with units.
 *
 * @param value The value to format.
 * @param value_units The initial units.
 * @param desired_units The final units.
 * @param decimals The number of decimals to add after the point.
 * @param append_units Whether or not to append the units string.
 */
std::string convert_and_format_units(float value, Biz::libpgcode::UnitsType value_units, Biz::libpgcode::UnitsType desired_units,
    uint8_t decimals = 0, bool append_units = true);

/** @brief Return the string associated with the given units.
 *
 * @param units The units.
 */
std::string format_units(Biz::libpgcode::UnitsType units);

/** @brief Format the given time in seconds into a string with format
 *         days, hours, minutes, seconds.
 *
 * @param time_in_secs The time to format, in seconds.
 */
std::string format_time_dhms(float time_in_secs);

/** @brief Format the given time in seconds into a string with format
 *         days, hours, minutes, seconds, rounding the result to
 *         get a shorter string.
 *
 * @param time_in_secs The time to format, in seconds.
 */
std::string format_time_dhms_short(float time_in_secs);

/** @brief Format the given time in seconds into a string with format
 *         days, hours, minutes, seconds, rounding the result to
 *         get a shorter string and splitting it in multiple lines, if needed.
 *
 * @param time_in_secs The time to format, in seconds.
 */
std::string format_time_dhms_short_and_splitted(float time_in_secs);

} // namespace Slic3r::Biz
