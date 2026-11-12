#pragma once

#include "Slic3r/Domain/JsonValue.hpp"
#include "Slic3r/Assert.hpp"
#include <nlohmann/adl_serializer.hpp>


NLOHMANN_JSON_NAMESPACE_BEGIN

template <>
struct adl_serializer<Slic3r::Domain::JsonValue>
{
    template <typename BasicJsonType>
    static void to_json(BasicJsonType& j, const Slic3r::Domain::JsonValue& v)
    {
        std::visit([&j](auto&& arg) {
                j = arg;
            }, static_cast<const Slic3r::Domain::JsonVariant&>(v));
    }

    template <typename BasicJsonType>
    static void from_json(const BasicJsonType& j, Slic3r::Domain::JsonValue& v)
    {
        if (j.is_null()) {
            v = nullptr;
        } else if (j.is_boolean()) {
            v = j.template get<bool>();
        } else if (j.is_number()) {
            v = j.template get<double>();
        } else if (j.is_string()) {
            v = j.template get<std::string>();
        } else if (j.is_array()) {
            v = j.template get<Slic3r::Domain::JsonArray>();
        } else if (j.is_object()) {
            v = j.template get<Slic3r::Domain::JsonObject>();
        } else {
            PANIC("Invalid JsonValue type");
        }
    }
};
NLOHMANN_JSON_NAMESPACE_END

