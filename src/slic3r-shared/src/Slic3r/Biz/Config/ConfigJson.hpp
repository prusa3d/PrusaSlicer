#pragma once

#include <nlohmann/json.hpp>
#include <tl/expected.hpp>
#include "Slic3r/Domain/TemplateUtils.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/Percentage.hpp"

namespace Slic3r::Domain::Advanced {
[[maybe_unused]] void from_json(const nlohmann::ordered_json& json_value, Vec2d& vec2d);

[[maybe_unused]] void to_json(nlohmann::ordered_json& json_value, const Vec2d& vec2d);

} // namespace Slic3r::Domain::Advanced

namespace Slic3r::Domain {
[[maybe_unused]] void from_json(const nlohmann::ordered_json& json_value, Percentage& percentage);

[[maybe_unused]] void to_json(nlohmann::ordered_json& json_value, const Percentage& percentage);

[[maybe_unused]] void from_json(const nlohmann::ordered_json& json_value, FloatOrPercentage& fop);

[[maybe_unused]] void to_json(nlohmann::ordered_json& json_value, const FloatOrPercentage& fop);
} // namespace Slic3r::Domain

namespace Slic3r::Biz::Config {

template <typename T>
tl::expected<void, std::string> is_valid(const nlohmann::ordered_json& json_value) = delete;

template<>
tl::expected<void, std::string> is_valid<bool>(const nlohmann::ordered_json& json_value);

template<>
tl::expected<void, std::string> is_valid<int>(const nlohmann::ordered_json& json_value);

template<>
tl::expected<void, std::string> is_valid<double>(const nlohmann::ordered_json& json_value);

template<>
tl::expected<void, std::string> is_valid<std::string>(const nlohmann::ordered_json& json_value);

template<>
tl::expected<void, std::string> is_valid<Domain::Vec2d>(const nlohmann::ordered_json& json_value);

template<>
tl::expected<void, std::string> is_valid<Domain::FloatOrPercentage>(const nlohmann::ordered_json& json_value);

template<>
tl::expected<void, std::string> is_valid<Domain::Percentage>(const nlohmann::ordered_json& json_value);

template <typename T>
tl::expected<void, std::string> is_valid_optional(const nlohmann::ordered_json& json_value);

template <typename T>
tl::expected<void, std::string> is_valid_vec(const nlohmann::ordered_json& json_value) {
    if (!json_value.is_array()) {
        return tl::unexpected{"Not an array!"};
    }

    for (const auto& element : json_value) {
        if constexpr (Domain::is_std_optional_v<typename T::value_type>) {
            const auto valid{is_valid_optional<typename T::value_type>(element)};
            if (!valid) {
                return valid;
            }
        } else {
            const auto valid{is_valid<typename T::value_type>(element)};
            if (!valid) {
                return valid;
            }
        }
    }
    return tl::expected<void, std::string>{};
}

template <typename T>
tl::expected<void, std::string> is_valid_optional(const nlohmann::ordered_json& json_value) {
    if (json_value.is_null()) {
        return tl::expected<void, std::string>{};
    }

    if constexpr (Domain::is_std_vector_v<typename T::value_type>) {
        return is_valid_vec<typename T::value_type>(json_value);
    } else {
        return is_valid<typename T::value_type>(json_value);
    }
}

template<typename T>
tl::expected<void, std::string> is_valid_map(const nlohmann::ordered_json& json_value)
{
    if (!json_value.is_object() && !json_value.is_null()) {
        return tl::unexpected{"Not an object!"};
    }

    for (const auto& [_, value] : json_value.items()) {
        const auto valid{is_valid<typename T::mapped_type>(value)};
        if (!valid) {
            return valid;
        }
    }
    return tl::expected<void, std::string>{};
}

template<typename T>
tl::expected<T, std::string> parse(const nlohmann::ordered_json& json_value)
{
    if constexpr (Domain::is_std_vector_v<T>) {
        const auto valid{is_valid_vec<T>(json_value)};
        if (!valid) {
            return tl::unexpected{valid.error()};
        }
    } else if constexpr (Domain::is_std_optional_v<T>) {
        const auto valid{is_valid_optional<T>(json_value)};
        if (!valid) {
            return tl::unexpected{valid.error()};
        }
    } else if constexpr (Domain::is_std_map_v<T>) {
        const auto valid{is_valid_map<T>(json_value)};
        if (!valid) {
            return tl::unexpected{valid.error()};
        }
    } else {
        const auto valid{is_valid<T>(json_value)};
        if (!valid) {
            return tl::unexpected{valid.error()};
        }
    }

    return json_value.template get<T>();
}

} // namespace Slic3r::Biz::Config
