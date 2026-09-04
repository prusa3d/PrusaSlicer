#include "Slic3r/Biz/Units.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/Assert.hpp"

#include <Slic3r/Biz/libpgcode/Utils.hpp>

#include <vector>
#include <functional>
#include <cmath>

using namespace Slic3r::Biz::libpgcode;

namespace Slic3r::Biz {

struct UnitsStrings
{
    std::vector<std::string> data;
    void init() {
        // This data needs to be initialized after the application's
        // locale has been set
        data = {
            // TRN: Following strings are units.
            _u8L("bytes"),
            _u8L("KB"),
            _u8L("MB"),
            _u8L("GB"),
            _u8L("TB"),
            _u8L("°C"),
            _u8L("d"),
            _u8L("°F"),
            _u8L("ft"),
            _u8L("ft³"),
            _u8L("g"),
            _u8L("h"),
            _u8L("in"),
            _u8L("in/s"),
            _u8L("in³"),
            _u8L("in³/s"),
            _u8L("m"),
            _u8L("m³"),
            _u8L("mm"),
            _u8L("mm/s"),
            _u8L("mm/m"),
            _u8L("mm³"),
            _u8L("mm³/s"),
            _u8L("m"),
            _u8L("oz"),
            _u8L("s"),
            _u8L("y"),
        };
    }
};

static UnitsStrings g_units_strings;

std::string format_units(float value, UnitsType units, uint8_t decimals)
{
    DEBUG_ASSERT(units < UnitsType::COUNT);
    char buf[64];
    sprintf(buf, "%.*f %s", decimals, value, format_units(units).c_str());
    return std::string(buf);
}

std::string convert_and_format_units(float value, UnitsType value_units, UnitsType desired_units, uint8_t decimals,
    bool append_units)
{
    DEBUG_ASSERT(value_units < UnitsType::COUNT && desired_units < UnitsType::COUNT);
    const auto it = std::find_if(CONVERSIONS.begin(), CONVERSIONS.end(),
      [value_units, desired_units](const Conversion& c) { return c.in == value_units && c.out == desired_units; });

    if (value_units != desired_units && it != CONVERSIONS.end()) {
        // TODO -> log error message;
    }

    // if unable to perform the conversion, return the input value
    const float out_value = (it != CONVERSIONS.end()) ? convert(value, value_units, desired_units) : value;
    if (append_units)
        return format_units(out_value, (it != CONVERSIONS.end()) ? desired_units : value_units, decimals);
    else {
        char buf[64];
        sprintf(buf, "%.*f", decimals, out_value);
        return std::string(buf);
    }
}

std::string format_units(UnitsType units)
{
    DEBUG_ASSERT(units < UnitsType::COUNT);
    if (g_units_strings.data.empty())
        g_units_strings.init();
    return g_units_strings.data[size_t(units)];
}

struct TimeHDMS
{
    float days{ 0.0f };
    float hours{ 0.0f };
    float minutes{ 0.0f };
    float seconds{ 0.0f };

    std::string format() const {
        char buffer[64];
        if (days > 365.0f)
            return "> 1" + format_units(UnitsType::Years);
        else if (days > 0.0f)
            sprintf(buffer, d_h_m_s_mask().c_str(), int(days), int(hours), int(minutes), int(seconds));
        else if (hours > 0.0f)
            sprintf(buffer, h_m_s_mask().c_str(), int(hours), int(minutes), int(seconds));
        else if (minutes > 0.0f)
            sprintf(buffer, m_s_mask().c_str(), int(minutes), int(seconds));
        else if (seconds >= 1.0f)
            sprintf(buffer, s_mask().c_str(), int(std::round(seconds)));
        else
            return "< 1" + format_units(UnitsType::Seconds);
        return buffer;
    }

    std::string format_short() const {
        TimeHDMS copy = *this;
        if (copy.seconds >= 30.0f) {
            copy.minutes += 1.0f;
            if (copy.minutes == 60.0f) {
                copy.minutes = 0.0f;
                copy.hours += 1.0f;
                if (copy.hours == 24) {
                    copy.hours = 0.0f;
                    copy.days += 1.0f;
                }
            }
        }

        char buffer[64];
        if (copy.days > 365.0f)
            return "> 1" + format_units(UnitsType::Years);
        else if (copy.days > 0.0f)
            sprintf(buffer, d_h_m_mask().c_str(), int(copy.days), int(copy.hours), int(copy.minutes));
        else if (copy.hours > 0.0f)
            sprintf(buffer, h_m_mask().c_str(), int(copy.hours), int(copy.minutes));
        else if (copy.minutes > 0.0f)
            sprintf(buffer, m_mask().c_str(), int(copy.minutes));
        else if (copy.seconds >= 1.0f)
            sprintf(buffer, s_mask().c_str(), int(std::round(copy.seconds)));
        else
            return "< 1" + format_units(UnitsType::Seconds);
        return buffer;
    }

    std::string format_short_and_splitted() const {
        char buffer[64];
        if (days > 365.0f)
            return "> 1" + format_units(UnitsType::Years);
        else if (days > 0.0f)
            sprintf(buffer, dh__m_mask().c_str(), int(days), int(hours), int(minutes));
        else if (hours > 0.0f) {
            if (hours < 10.0f && minutes < 10.0f && seconds < 10.0f)
                sprintf(buffer, hms_mask().c_str(), int(hours), int(minutes), int(seconds));
            else if (hours > 10.0f && minutes > 10.0f && seconds > 10.0f)
                sprintf(buffer, h__m__s_mask().c_str(), int(hours), int(minutes), int(seconds));
            else if ((minutes < 10.0f && seconds > 10.0f) || (minutes > 10.0f && seconds < 10.0f))
                sprintf(buffer, h__ms_mask().c_str(), int(hours), int(minutes), int(seconds));
            else
                sprintf(buffer, hm__s_mask().c_str(), int(hours), int(minutes), int(seconds));
        }
        else if (minutes > 0.0f) {
            if (minutes > 10.0f && seconds > 10.0f)
                sprintf(buffer, m__s_mask().c_str(), int(minutes), int(seconds));
            else
                sprintf(buffer, ms_mask().c_str(), int(minutes), int(seconds));
        }
        else if (seconds >= 1.0f)
            sprintf(buffer, s_mask().c_str(), int(std::round(seconds)));
        else
            return "< 1" + format_units(UnitsType::Seconds);
        return buffer;
    }

    static std::string days_mask()    { return "%d" + format_units(UnitsType::Days); }
    static std::string hours_mask()   { return "%d" + format_units(UnitsType::Hours); }
    static std::string minutes_mask() { return "%d" + format_units(UnitsType::Minutes); }
    static std::string seconds_mask() { return "%d" + format_units(UnitsType::Seconds); }

    static std::string d_h_m_s_mask() { return days_mask() + " " + hours_mask() + " " + minutes_mask() + " " + seconds_mask(); }
    static std::string d_h_m_mask()   { return days_mask() + " " + hours_mask() + " " + minutes_mask(); }
    static std::string h_m_s_mask()   { return hours_mask() + " " + minutes_mask() + " " + seconds_mask(); }
    static std::string h_m_mask()     { return hours_mask() + " " + minutes_mask(); }
    static std::string m_s_mask()     { return minutes_mask() + " " + seconds_mask(); }
    static std::string m_mask()       { return minutes_mask(); }
    static std::string s_mask()       { return seconds_mask(); }
    static std::string hms_mask()     { return hours_mask() + minutes_mask() + seconds_mask(); }
    static std::string ms_mask()      { return minutes_mask() + seconds_mask(); }
    static std::string h__m__s_mask() { return hours_mask() + "\n" + minutes_mask() + "\n" + seconds_mask(); }
    static std::string dh__m_mask()   { return days_mask() + hours_mask() + "\n" + minutes_mask(); }
    static std::string hm__s_mask()   { return hours_mask() + minutes_mask() + "\n" + seconds_mask(); }
    static std::string h__ms_mask()   { return hours_mask() + "\n" + minutes_mask() + seconds_mask(); }
    static std::string m__s_mask()    { return minutes_mask() + "\n" + seconds_mask(); }
};

static TimeHDMS time_dhms(float time_in_secs)
{
    TimeHDMS ret;
    ret.days = float(int(time_in_secs / 86400.0f));
    time_in_secs -= ret.days * 86400.0f;
    ret.hours = float(int(time_in_secs / 3600.0f));
    time_in_secs -= ret.hours * 3600.0f;
    ret.minutes = float(int(time_in_secs / 60.0f));
    ret.seconds = time_in_secs - ret.minutes * 60.0f;
    return ret;
}

std::string format_time_dhms(float time_in_secs)
{
    return time_dhms(time_in_secs).format();
}

std::string format_time_dhms_short(float time_in_secs)
{
    return time_dhms(time_in_secs).format_short();
}

std::string format_time_dhms_short_and_splitted(float time_in_secs)
{
    return time_dhms(time_in_secs).format_short_and_splitted();
}

} // namespace Slic3r::Biz
