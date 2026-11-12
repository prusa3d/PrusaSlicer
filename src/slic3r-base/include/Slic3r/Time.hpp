#pragma once

#include <string>
#include <ctime>

namespace Slic3r::Utils {

// Should be thread safe.
time_t get_current_time_utc();

enum class TimeZone { local, utc };
enum class TimeFormat
{
    /** @brief Gcode human readable */
    gcode,
    /** @brief Short form of iso8601 in UTC in format: YYYYMMDDThhmmssZ */
    iso8601Z,
    /** @brief Extended form of iso8601 in UTC in format: YYYY-MM-DDThh:mm:ssZ */
    iso8601ZExtended
};

// time_t to string functions...

std::string time2str(const time_t &t, TimeZone zone, TimeFormat fmt);

inline std::string time2str(TimeZone zone, TimeFormat fmt)
{
    return time2str(get_current_time_utc(), zone, fmt);
}

inline std::string utc_timestamp(time_t t)
{
    return time2str(t, TimeZone::utc, TimeFormat::gcode);
}

inline std::string utc_timestamp()
{
    return utc_timestamp(get_current_time_utc());
}

// String to time_t function. Returns time_t(-1) if fails to parse the input.
time_t str2time(const std::string &str, TimeZone zone, TimeFormat fmt);


// /////////////////////////////////////////////////////////////////////////////
// Utilities to convert an UTC time_t to/from an ISO8601 time format,
// useful for putting timestamps into file and directory names.
// Returns (time_t)-1 on error.

// Use these functions to convert safely to and from the ISO8601 format on
// all platforms

inline std::string iso_utc_timestamp(time_t t)
{
    return time2str(t, TimeZone::utc, TimeFormat::iso8601Z);
}

inline std::string iso_utc_timestamp()
{
    return iso_utc_timestamp(get_current_time_utc());
}

inline std::string iso_ext_utc_timestamp(time_t t)
{
    return time2str(t, TimeZone::utc, TimeFormat::iso8601ZExtended);
}

inline std::string iso_ext_utc_timestamp()
{
    return iso_ext_utc_timestamp(get_current_time_utc());
}

inline time_t parse_iso_utc_timestamp(const std::string &str)
{
    return str2time(str, TimeZone::utc, TimeFormat::iso8601Z);
}

// /////////////////////////////////////////////////////////////////////////////

// Returns the given time is seconds in format DDd HHh MMm SSs
std::string get_time_dhms(float time_in_secs);


} // namespace Slic3r::Utils
