#pragma once

#include <set>
#include <vector>

namespace Slic3r::Biz::Utils {

template <typename T>
struct SetDiff
{
    std::vector<T> changed{};
    std::vector<T> removed{};
    std::vector<T> added{};
};

template <typename T>
static SetDiff<T> get_sets_diff(const std::set<T>& old_set, const std::set<T>& new_set)
{
    SetDiff<T> result;
    for (const auto& new_item : new_set) {
        if (old_set.contains(new_item)) {
            result.changed.push_back(new_item);
        } else {
            result.added.push_back(new_item);
        }
    }

    for (const auto& old_item : old_set) {
        if (!new_set.contains(old_item)) {
            result.removed.push_back(old_item);
        }
    }

    return result;
}
} // namespace Slic3r::Biz::Utils
