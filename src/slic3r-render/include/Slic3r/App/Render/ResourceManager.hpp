#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_map>
#include "Slic3r/Log.hpp"

namespace Slic3r::App::Render {

template <typename K, typename R>
class ResourceManager {
public:
    ResourceManager(std::string name) : m_name{std::move(name)} {}

    R* get_or_create(const K& name, const std::function<std::unique_ptr<R>()>& builder)
    {
        auto it = m_resources.find(name);
        if (it != m_resources.end())
            return it->second.get();
        auto ret = m_resources.emplace(name, std::move(builder()));
        report_memory();
        return ret.first->second.get();
    }

    const R* get(const K& name) const
    {
        auto it = m_resources.find(name);
        return it != m_resources.end() ? it->second.get() : nullptr;
    }

    R* get(const K& name)
    {
        auto it = m_resources.find(name);
        return it != m_resources.end() ? it->second.get() : nullptr;
    }

    void set(const K& name, R* resource)
    {
        m_resources[name] = std::make_unique<R>(resource);
        report_memory();
    }

    void set(const K& name, std::unique_ptr<R>&& resource)
    {
        m_resources[name] = std::move(resource);
        report_memory();
    }

    bool release(const K& name)
    {
        std::size_t erased{m_resources.erase(name)};
        report_memory();
        return erased > 0;
    }

    void release_all()
    {
        m_resources.clear();
        report_memory();
    }

    size_t release_if(std::function<bool(const K& name, const R& resource)> predicate)
    {
        size_t count = 0;
        std::erase_if(m_resources, [&](const auto& item) {
            bool res = predicate(item.first, *item.second);
            if (res)
                ++count;
            return res;
        });
        report_memory();
        return count;
    }

    bool is_empty() const { return m_resources.empty(); }

private:
    void report_memory()
    {
        if constexpr (requires(R x) { x.memsize(); }) {
            std::size_t memsize{};
            for (const std::unique_ptr<R>& resource : m_resources | std::views::values) {
                memsize += resource->memsize();
            }
            const std::size_t kb{1024};
            const std::size_t mb{1024 * kb};
            SPDLOG_TRACE("The resource manager: {} uses {} mb", m_name, memsize / mb);
        }
    }

    using ResourceMap = std::unordered_map<K, std::unique_ptr<R>>;

    ResourceMap m_resources;
    std::string m_name;
};

}
