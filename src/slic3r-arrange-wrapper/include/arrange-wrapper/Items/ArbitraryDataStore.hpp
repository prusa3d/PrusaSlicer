///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef ARBITRARYDATASTORE_HPP
#define ARBITRARYDATASTORE_HPP

#include <string>
#include <map>
#include <any>

#include <arrange/DataStoreTraits.hpp>

namespace Slic3r { namespace arr2 {

// An associative container able to store and retrieve any data type.
// Based on std::any
class ArbitraryDataStore {
    std::map<std::string, std::any> m_data;

public:
    template<class T> void add(const std::string &key, T &&data)
    {
        m_data[key] = std::any{std::forward<T>(data)};
    }

    void add(const std::string &key, std::any &&data)
    {
        m_data[key] = std::move(data);
    }

    // Return nullptr if the key does not exist or the stored data has a
    // type other then T. Otherwise returns a pointer to the stored data.
    template<class T> const T *get(const std::string &key) const
    {
        auto it = m_data.find(key);
        return it != m_data.end() ? std::any_cast<T>(&(it->second)) :
                                    nullptr;
    }

    // Same as above just not const.
    template<class T> T *get(const std::string &key)
    {
        auto it = m_data.find(key);
        return it != m_data.end() ? std::any_cast<T>(&(it->second)) : nullptr;
    }

    bool has_key(const std::string &key) const
    {
        auto it = m_data.find(key);
        return it != m_data.end();
    }
};

// Some items can be containers of arbitrary data stored under string keys.
template<> struct DataStoreTraits_<ArbitraryDataStore>
{
    static constexpr bool Implemented = true;

    template<class T>
    static const T *get(const ArbitraryDataStore &s, const std::string &key)
    {
        return s.get<T>(key);
    }

    // Same as above just not const.
    template<class T>
    static T *get(ArbitraryDataStore &s, const std::string &key)
    {
        return s.get<T>(key);
    }

    template<class T>
    static bool has_key(ArbitraryDataStore &s, const std::string &key)
    {
        return s.has_key(key);
    }
};

template<> struct WritableDataStoreTraits_<ArbitraryDataStore>
{
    static constexpr bool Implemented = true;

    template<class T>
    static void set(ArbitraryDataStore &store,
                    const std::string &key,
                    T &&data)
    {
        store.add(key, std::forward<T>(data));
    }
};

}} // namespace Slic3r::arr2

#endif // ARBITRARYDATASTORE_HPP
