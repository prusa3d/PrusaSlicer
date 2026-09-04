#include "Slic3r/Domain/Preset/PresetTree.hpp"

#include <algorithm>
#include <ranges>

namespace  Slic3r::Domain::Preset {

std::optional<std::string_view> PresetNode::short_name() const
{
    if (!name.has_value())
        return std::nullopt;
    return Domain::Preset::short_name(name.value());
}

std::string derive_name(std::string_view name, std::string_view parent_name)
{
    if (name.empty()) {
        return std::string(parent_name);
    }

    // Extract the base name of the child (everything before the first '@')
    size_t first_at = name.find('@');
    std::string result{name.substr(0, first_at)};

    // Helper view: splits a string by '@' and skips the first element
    auto get_tags = [](std::string_view s) {
        return s | std::views::split('@') | std::views::drop(1);
    };

    auto parent_tags = get_tags(parent_name);

    // 1. Append child's tags ONLY if they don't exist in the parent
    for (auto n_tag : get_tags(name)) {
        auto is_duplicate = [&n_tag](auto p_tag) {
            return std::ranges::equal(n_tag, p_tag);
        };

        if (std::ranges::find_if(parent_tags, is_duplicate) == parent_tags.end()) {
            result += '@';
            for (char c : n_tag) result += c;
        }
    }

    // 2. Append all parent tags, keeping their original order intact
    for (auto p_tag : parent_tags) {
        result += '@';
        for (char c : p_tag) result += c;
    }

    return result;
}

std::string_view short_name(const std::string& name)
{
    size_t idx = name.find('@');
    if (idx == 0 || idx == std::string_view::npos)
        return name;
    while (idx > 0 && name[idx - 1] == ' ') idx--;
    return std::string_view{name.data(), idx};
}

bool is_public_name(const std::string& name)
{
    return !name.empty() && name[0] != '*';
}

}

