///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef TRAFOONLYARRANGEITEM_HPP
#define TRAFOONLYARRANGEITEM_HPP

#include <arrange/ArrangeItemTraits.hpp>

#include "ArbitraryDataStore.hpp"
#include "MutableItemTraits.hpp"

namespace Slic3r { namespace arr2 {

class TrafoOnlyArrangeItem {
    int m_bed_idx = Unarranged;
    int m_priority = 0;
    Vec2crd m_translation = Vec2crd::Zero();
    double  m_rotation = 0.;
    std::optional<int> m_bed_constraint;

    ArbitraryDataStore m_datastore;

public:
    TrafoOnlyArrangeItem() = default;

    template<class ArrItm>
    explicit TrafoOnlyArrangeItem(const ArrItm &other)
        : m_bed_idx{arr2::get_bed_index(other)},
          m_priority{arr2::get_priority(other)},
          m_translation(arr2::get_translation(other)),
          m_rotation{arr2::get_rotation(other)},
          m_bed_constraint{arr2::get_bed_constraint(other)}
    {}

    const Vec2crd& get_translation() const noexcept { return m_translation; }
    double get_rotation() const noexcept { return m_rotation; }
    int get_bed_index() const noexcept { return m_bed_idx; }
    int get_priority() const noexcept { return m_priority; }
    std::optional<int> get_bed_constraint() const noexcept { return m_bed_constraint; }

    const ArbitraryDataStore &datastore() const noexcept { return m_datastore; }
    ArbitraryDataStore &datastore() { return m_datastore; }
};

template<> struct DataStoreTraits_<TrafoOnlyArrangeItem>
{
    static constexpr bool Implemented = true;

    template<class T>
    static const T *get(const TrafoOnlyArrangeItem &itm, const std::string &key)
    {
        return itm.datastore().get<T>(key);
    }

    template<class T>
    static T *get(TrafoOnlyArrangeItem &itm, const std::string &key)
    {
        return itm.datastore().get<T>(key);
    }

    static bool has_key(const TrafoOnlyArrangeItem &itm, const std::string &key)
    {
        return itm.datastore().has_key(key);
    }
};

template<> struct IsMutableItem_<TrafoOnlyArrangeItem>: public std::true_type {};

template<> struct WritableDataStoreTraits_<TrafoOnlyArrangeItem>
{
    static constexpr bool Implemented = true;

    template<class T>
    static void set(TrafoOnlyArrangeItem &itm,
                    const std::string &key,
                    T &&data)
    {
        set_data(itm.datastore(), key, std::forward<T>(data));
    }
};

} // namespace arr2
} // namespace Slic3r

#endif // TRAFOONLYARRANGEITEM_HPP
