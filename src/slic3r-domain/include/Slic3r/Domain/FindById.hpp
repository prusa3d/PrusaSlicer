#pragma once
#include <vector>
#include <algorithm>
#include <memory>

namespace Slic3r::Domain {

template <typename T>
T* find_by_id(std::vector<T*>& container, size_t id)
{
    auto it = std::find_if(container.begin(), container.end(), [id](const auto& e) {
        return e->id() == id;
    });
    return it == container.end() ? nullptr : *it;
}

template <typename T>
const T* find_by_id(const std::vector<T*>& container, size_t id)
{
    auto it = std::find_if(container.begin(), container.end(), [id](const auto& e) {
        return e->id() == id;
    });
    return it == container.end() ? nullptr : *it;
}

template <typename T>
T* find_by_id(std::vector<std::unique_ptr<T>>& container, size_t id)
{
    auto it = std::find_if(container.begin(), container.end(), [id](const auto& e) {
        return e->id() == id;
    });
    return it == container.end() ? nullptr : it->get();
}

template <typename T>
const T* find_by_id(const std::vector<std::unique_ptr<T>>& container, size_t id)
{
    auto it = std::find_if(container.begin(), container.end(), [id](const auto& e) {
        return e->id() == id;
    });
    return it == container.end() ? nullptr : it->get();
}

}
