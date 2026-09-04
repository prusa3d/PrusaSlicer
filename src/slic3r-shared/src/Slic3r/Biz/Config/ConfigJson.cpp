#include "Slic3r/Biz/Config/ConfigJson.hpp"

using nlohmann::ordered_json;

namespace Slic3r::Domain::Advanced {
void from_json(const nlohmann::ordered_json& json_value, Vec2d& vec2d)
{
    json_value.at(0).get_to(vec2d[0]);
    json_value.at(1).get_to(vec2d[1]);
}

void to_json(ordered_json& json_value, const Vec2d& vec2d)
{
    json_value = ordered_json::array({vec2d.x(), vec2d.y()});
}
} // namespace Slic3r::Domain::Advanced

namespace Slic3r::Domain {
void from_json(const ordered_json& json_value, Percentage& percentage)
{
    json_value["value"].get_to(percentage.value);
}

void to_json(ordered_json& json_value, const Percentage& percentage)
{
    json_value = {{"value", percentage.value}, {"is_percent", true}};
}

void from_json(const ordered_json& json_value, FloatOrPercentage& fop)
{
    const auto value{json_value["value"].get<double>()};
    const auto is_percentage{json_value["is_percent"].get<bool>()};

    if (is_percentage) {
        fop = FloatOrPercentage{Percentage{value}};
    } else {
        fop = FloatOrPercentage{value};
    }
}

void to_json(ordered_json& json_value, const FloatOrPercentage& fop)
{
    json_value = {
        {"value", fop.is_percentage() ? fop.percentage().value : fop.float_value()},
        {"is_percent", fop.is_percentage()}
    };
}
} // namespace Slic3r::Domain

namespace Slic3r::Biz::Config {

using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Percentage;
using Slic3r::Domain::FloatOrPercentage;

template<>
tl::expected<void, std::string> is_valid<int>(const ordered_json& json_value) {
    if (!json_value.is_number_integer()) {
        return tl::unexpected{"Not an integer!"};
    }
    return tl::expected<void, std::string>{};
}

template<>
tl::expected<void, std::string> is_valid<double>(const ordered_json& json_value) {
    if (!json_value.is_number()) {
        return tl::unexpected{"Not a number!"};
    }
    return tl::expected<void, std::string>{};
}

template<>
tl::expected<void, std::string> is_valid<std::string>(const ordered_json& json_value) {
    if (!json_value.is_string()) {
        return tl::unexpected{"Not a string!"};
    }
    return tl::expected<void, std::string>{};
}

template<>
tl::expected<void, std::string> is_valid<bool>(const ordered_json& json_value) {
    if (!json_value.is_boolean()) {
        return tl::unexpected{"Not a boolean!"};
    }
    return tl::expected<void, std::string>{};
}

template<>
tl::expected<void, std::string> is_valid<Vec2d>(const ordered_json& json_value)
{
    if (!json_value.is_array()) {
        return tl::unexpected{"Not an array!"};
    }

    if (json_value.size() != 2) {
        return tl::unexpected{"The array size is not 2!"};
    }

    if (!json_value.at(0).is_number()) {
        return tl::unexpected{"The first element is not a number"};
    }

    if (!json_value.at(1).is_number()) {
        return tl::unexpected{"The first element is not a number"};
    }
    return tl::expected<void, std::string>{};
}

template<>
tl::expected<void, std::string> is_valid<FloatOrPercentage>(const ordered_json& json_value)
{
    if (!json_value.is_object()) {
        return tl::unexpected{"Not an object!"};
    }
    if (!json_value.contains("value")) {
        return tl::unexpected{"Does not contain value!"};
    }
    if (!json_value.contains("is_percent")) {
        return tl::unexpected{"Does not contain is_percent!"};
    }
    if (!json_value["value"].is_number()) {
        return tl::unexpected{"value is not a number!"};
    }
    if (!json_value["is_percent"].is_boolean()) {
        return tl::unexpected{"is_percent is not a boolean!"};
    }
    return tl::expected<void, std::string>{};
}

template<>
tl::expected<void, std::string> is_valid<Percentage>(const ordered_json& json_value)
{
    const auto valid{is_valid<FloatOrPercentage>(json_value)};
    if (!valid) {
        return valid;
    }
    if (!json_value["is_percent"].get<bool>()) {
        return tl::unexpected{"is_percent must always be true for Percentage!"};
    }
    return tl::expected<void, std::string>{};
}

} // namespace Slic3r::Biz::Config
