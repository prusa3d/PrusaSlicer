#pragma once

#include <string>
#include <vector>
#include <variant>
#include <unordered_map>

namespace Slic3r::App::Yoga {

struct DnDPayload
{
    std::string type;
    // If you need more types in the payload, feel free to add it here
    // I just put all the usual suspects.
    std::unordered_map<
        std::string,
        std::variant<
            bool,
            int,
            size_t,
            double,
            std::string,
            std::vector<int>,
            std::vector<std::string>>>
        data;

    template <class T>
    const T* get(const std::string& key) const
    {
        auto it = data.find(key);
        if (it == data.end()) {
            return nullptr;
        }

        return std::get_if<T>(&it->second);
    }

    template <class T>
    T value_or(const std::string& key, T fallback) const
    {
        if (const T* value = get<T>(key)) {
            return *value;
        }

        return fallback;
    }
};

} // namespace Slic3r::App::Yoga
