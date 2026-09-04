#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Biz/Config/ConfigSerialize.hpp"

#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Config/ConfigJson.hpp" // IWYU pragma: keep
#include "boost/algorithm/string.hpp"

#include <algorithm>
#include <set>
#include <fmt/format.h>

static std::vector<std::string> to_strings(const std::vector<std::string_view>& strings)
{
    std::vector<std::string> out;
    out.insert(out.end(), strings.begin(), strings.end());
    return out;
}

namespace Slic3r::Domain {
[[maybe_unused]] void to_json(nlohmann::ordered_json& json_value, const ConfigItem& item) {
    item.visit([&](auto&& item_value) {
        using ValueType = std::remove_cvref_t<decltype(item_value)>;
        if constexpr (std::is_same_v<ValueType, EnumWrapper>) {
            json_value = item_value.get_string();
        } else if constexpr (std::is_same_v<ValueType, EnumVectorWrapper>) {
            json_value = to_strings(item_value.get_strings());
        } else {
            json_value = item.get<ValueType>();
        }
    });
}

[[maybe_unused]] void to_json(nlohmann::ordered_json& json_value, const Domain::ConfigBox& box)
{
    for (const ConfigItem& item : box.overrides.all_items()) {
        if (box.overrides.get(item.name())) {
            json_value[item.name()] = item;
        } else {
            // Unused overrides are not serialized.
        }
    }

    for (const ConfigItem& item : box.items.all_items()) {
        json_value[item.name()] = item;
    }
}

// Given list of boxes of the same type, serializes the content such that each key
// appears once and items from individual boxes end up as vector elements.
// Vector which belong to overrides and which are full of nulls are omitted.
[[maybe_unused]] void to_json(nlohmann::ordered_json& json_value, const BoxRefs& boxes)
{
    ASSERT(!boxes.empty());
    ASSERT(std::all_of(boxes.begin(), boxes.end(), [&boxes](const auto& box_ref) {
        return box_ref.get().location == boxes.front().get().location;
    }));

    // Create a JSON object from each box individually.
    std::vector<nlohmann::ordered_json> json_objects;
    for (const auto& box : boxes)
        json_objects.emplace_back(box.get());

    // Prepare list of keys used in this ConfigBox, either as a regular item or an override.
    std::vector<std::string> keys;
    for (const ConfigItem& item : boxes.front().get().overrides.all_items())
        keys.emplace_back(item.name());
    for (const ConfigItem& item : boxes.front().get().items.all_items())
        keys.emplace_back(item.name());
    std::ranges::sort(keys);

    // Vectorization of the individual json objects. Assumes that the keys are the same.
    json_value = nlohmann::ordered_json::object();
    for (const std::string& current_key : keys) {
        nlohmann::ordered_json value_array = nlohmann::ordered_json::array();
        for (const auto& obj : json_objects) {
            auto it = obj.find(current_key);
            value_array.push_back(it != obj.end() ? (*it) : nullptr);
        }
        json_value[current_key] = std::move(value_array);
    }

    // Remove all vectors which are full of nulls - but only for overrides.
    for (auto it = json_value.begin(); it != json_value.end(); ) {
        if (!boxes.front().get().overrides.find(it.key())) {
            ++it;
            continue; // Not an override, apparently an optional value.
        }
        ASSERT(it.value().is_array());
        if (const auto& arr = it.value();
            ! arr.empty() && std::all_of(arr.begin(), arr.end(), [](const auto& e) { return e.is_null(); }))
            it = json_value.erase(it);
        else
            ++it;
    }
}

void to_json(
    nlohmann::ordered_json& json_value, const BoxOrBoxesVector& box_or_boxes_vector
)
{
    for (const auto& box_or_boxes : box_or_boxes_vector) {
        std::visit([&](auto&& box_or_boxes){
            using ValueType = std::remove_cvref_t<decltype(box_or_boxes)>;
            std::string location_name;
            if constexpr(std::is_same_v<ValueType, BoxRef>) {
                location_name = get_location_name(box_or_boxes.get().location);
            } else {
                location_name = get_location_name(box_or_boxes.front().get().location);
            }
            json_value[location_name] = box_or_boxes;
        }, box_or_boxes);
    }
}
}

namespace Slic3r::Biz {

using Domain::ConfigItem;
using Domain::Vec2d;
using Domain::BoxOrBoxesVector;
using Domain::BoxRefs;
using Domain::BoxRef;
using Domain::ConfigLocation;

static std::string trim_quotes(const std::string& json_str)
{
    std::string out(json_str);
    boost::trim_if(out, boost::is_any_of("\""));
    return out;
}

std::variant<std::string, std::vector<std::string>> value_as_string(const ConfigItem& item)
{
    nlohmann::ordered_json j;
    j[item.name()] = item;

    ASSERT(!j.empty());

    auto it           = j.begin(); // Get the first (and assumed to be only) key-value pair
    const auto& value = it.value();

    if (value.is_array()) {
        std::vector<std::string> serialized_elements;
        for (const auto& element : value) {
            serialized_elements.push_back(trim_quotes(element.dump(-1, ' ', false)));
        }
        return serialized_elements;
    } else if (value.is_object()) {
        if (value.find("value") != value.end() && value.find("is_percent") != value.end()) {
            const auto d_value{value["value"].get<double>()};
            const auto is_percentage{value["is_percent"].get<bool>()};
            return is_percentage ? fmt::format("{}%", d_value) : fmt::format("{}", d_value);
        }
    }
    return trim_quotes(value.dump(-1, ' ', false));
}

std::string beautify_json(const Domain::BoxOrBoxesVector& box_or_boxes_vector, int indent)
{
    return beautify_json(nlohmann::ordered_json(box_or_boxes_vector), indent);
}



// Recursive helper to traverse the JSON and collect keys.
static void find_keys_with_many_children_recursive(const nlohmann::ordered_json& j, int min_children, int level, std::vector<std::pair<std::string, int>>& result) {
    if (j.is_object()) {
        for (const auto& item : j.items()) {
            const std::string& key = item.key();
            const nlohmann::ordered_json& value = item.value();
            if (value.size() >= min_children)
                result.emplace_back(key, level);
            else
                find_keys_with_many_children_recursive(value, min_children, level + 1, result);
        }
    }
    if (j.is_array()) {
        for (const auto& el : j)
            find_keys_with_many_children_recursive(el, min_children, level + 1, result);
    }
}

std::string beautify_json(
    const nlohmann::ordered_json& complete_json,
    int indent, int squash_factor)
{
    std::vector<std::pair<std::string, int>> keys_with_many_children_and_level;
    find_keys_with_many_children_recursive(complete_json, squash_factor, 1, keys_with_many_children_and_level);

    std::string str = complete_json.dump(indent);
    auto count_indent = [](const std::string& s) -> int { int i=0; while (s[i] == ' ') ++i; return i; };
    std::vector<std::string> lines;
    boost::split(lines, str, boost::is_any_of("\n"));
    for (auto& line : lines)
        line += '\n';
    std::string start;

    for (size_t line_id=0; line_id<lines.size(); ++line_id) {
        start = lines[line_id];
        boost::trim_left(start);
        if (! start.empty() && start.front() == '\"')
            start.erase(start.begin());
        auto it = std::find_if(keys_with_many_children_and_level.begin(), keys_with_many_children_and_level.end(),
            [&start](const std::pair<std::string, int> p) {
                return boost::starts_with(start, p.first);
        });
        if (it == keys_with_many_children_and_level.end())
            continue;
        std::string line_start = "\"" + it->first;
        int box_indent_level = it->second;

        if (lines[line_id].find(line_start) == box_indent_level * indent) {
            int box_indent = box_indent_level * indent;
            size_t i = line_id + 1;
            while (true) {
                int ind = count_indent(lines[i]);
                if (ind <= box_indent) {
                    if (std::string s = boost::trim_left_copy(lines[i]); boost::starts_with(s, "]")) {
                        // Ensure that squashed arrays have terminating ] at the same line.
                        ASSERT(lines[i-1].back() == '\n');
                        lines[i-1].pop_back();
                        lines[i-1] += s;
                        lines.erase(lines.begin() + i);
                    }
                    line_id = i-1;
                    break;
                }
                if (ind > box_indent + indent || (ind == box_indent + indent && lines[i][ind] != '\"')) {
                    // Only removes newlines and leading spaces, so the meaning stays the same.
                    boost::trim_left(lines[i]);
                    ASSERT(lines[i-1].back() == '\n');
                    lines[i-1].pop_back();
                    if (!lines[i - 1].empty() && lines[i - 1].back() == ',')
                        lines[i - 1].push_back(' ');
                }
                ++i;
            }
        }
    }

    str = {};
    for (const std::string& line : lines) {
        str += line;
    }

    return str;
}

} // namespace Slic3r::Biz
