///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef DATASTORETRAITS_HPP
#define DATASTORETRAITS_HPP

#include <string_view>

#include "libslic3r/libslic3r.h"

namespace Slic3r { namespace arr2 {

// Some items can be containers of arbitrary data stored under string keys.
template<class ArrItem, class En = void> struct DataStoreTraits_
{
    static constexpr bool Implemented = false;

    template<class T> static const T *get(const ArrItem &, const std::string &key)
    {
        return nullptr;
    }

    // Same as above just not const.
    template<class T> static T *get(ArrItem &, const std::string &key)
    {
        return nullptr;
    }

    static bool has_key(const ArrItem &itm, const std::string &key)
    {
        return false;
    }
};

template<class ArrItem, class En = void> struct WritableDataStoreTraits_
{
    static constexpr bool Implemented = false;

    template<class T> static void set(ArrItem &, const std::string &key, T &&data)
    {
    }
};

template<class T> using DataStoreTraits = DataStoreTraits_<StripCVRef<T>>;
template<class T> constexpr bool IsDataStore = DataStoreTraits<StripCVRef<T>>::Implemented;
template<class T, class TT = T> using DataStoreOnly = std::enable_if_t<IsDataStore<T>, TT>;

template<class T, class ArrItem>
const T *get_data(const ArrItem &itm, const std::string &key)
{
    return DataStoreTraits<ArrItem>::template get<T>(itm, key);
}

template<class ArrItem>
bool has_key(const ArrItem &itm, const std::string &key)
{
    return DataStoreTraits<ArrItem>::has_key(itm, key);
}

template<class T, class ArrItem>
T *get_data(ArrItem &itm, const std::string &key)
{
    return DataStoreTraits<ArrItem>::template get<T>(itm, key);
}

template<class T> using WritableDataStoreTraits = WritableDataStoreTraits_<StripCVRef<T>>;
template<class T> constexpr bool IsWritableDataStore = WritableDataStoreTraits<StripCVRef<T>>::Implemented;
template<class T, class TT = T> using WritableDataStoreOnly = std::enable_if_t<IsWritableDataStore<T>, TT>;

template<class T, class ArrItem>
void set_data(ArrItem &itm, const std::string &key, T &&data)
{
    WritableDataStoreTraits<ArrItem>::template set(itm, key, std::forward<T>(data));
}

template<class T> constexpr bool IsReadWritableDataStore = IsDataStore<T> && IsWritableDataStore<T>;
template<class T, class TT = T> using ReadWritableDataStoreOnly = std::enable_if_t<IsReadWritableDataStore<T>, TT>;

}} // namespace Slic3r::arr2

#endif // DATASTORETRAITS_HPP
