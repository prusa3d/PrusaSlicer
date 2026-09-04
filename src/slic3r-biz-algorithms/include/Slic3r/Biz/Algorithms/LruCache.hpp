#pragma once

#include <functional>
#include <unordered_map>
#include <list>

namespace Slic3r::Biz::Algorithms {

template <typename KeyType, typename ValueType>
class LruCache
{
public:
    explicit LruCache(size_t capacity) : m_capacity(capacity) {}

    using Factory = std::function<ValueType(const KeyType&)>;

    const ValueType& get(const KeyType& key, const Factory& factory)
    {
        auto it = m_lut.find(key);
        if (it == m_lut.end()) {
            return put(key, factory(key));
        }

        // move the item at the beginning
        m_cache.splice(m_cache.begin(), m_cache, it->second);

        return it->second->second;
    }

    const ValueType& put(const KeyType& key, const ValueType& value)
    {
        auto it = m_lut.find(key);
        if (it != m_lut.end()) {
            it->second->second = value;

            // move the item at the beginning
            m_cache.splice(m_cache.begin(), m_cache, it->second);
            return it->second->second;
        }
        if (m_cache.size() == m_capacity) {
            const auto& old_key = m_cache.back().first;
            m_lut.erase(old_key);
            m_cache.pop_back();
        }
        m_cache.emplace_front(key, value);
        auto ret = m_cache.begin();
        m_lut[key] = ret;
        return ret->second;
    }


private:
    using CacheList = std::list<std::pair<KeyType, ValueType>>;
    using LookupTable = std::unordered_map<KeyType, typename CacheList::iterator>;

    size_t m_capacity;
    CacheList m_cache;
    LookupTable m_lut;
};


} // namespace Slic3r::Biz::Algorithms
