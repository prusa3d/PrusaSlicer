#pragma once

#include <map>
#include <string>
#include <vector>
#include <variant>


namespace Slic3r::Domain {

struct JsonValue;

using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

using JsonVariant = std::variant<JsonObject, JsonArray, std::string, double, bool, std::nullptr_t>;

/**
 * @brief Minimally typed struct that can be serialized into JSON.
 * @note This struct / type is intended to carry data loaded from JSON and/or stored into JSON.
 * In most cases you want to store JSON serialized data into strongly type purposeful structs,
 * but sometimes you just need to load some data and store them somewhere else later. If you create
 * a strongly typed structs, you will need to keep these structs updated as the format changes.
 * Or you can use `JsonValue` as a generic representation, and you don't care about the format
 * change (if you just transport these data).
 */
struct JsonValue : JsonVariant
{
    using JsonVariant::JsonVariant;
};


}
