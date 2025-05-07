///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef MutableItemTraits_HPP
#define MutableItemTraits_HPP

#include <libslic3r/ExPolygon.hpp>

#include <arrange/ArrangeItemTraits.hpp>
#include <arrange/DataStoreTraits.hpp>

namespace Slic3r { namespace arr2 {

template<class Itm> struct IsMutableItem_ : public std::false_type
{};

// Using this interface to set up any arrange item. Provides default
// implementation but it needs to be explicitly switched on with
// IsMutableItem_ or completely reimplement a specialization.
template<class Itm, class En = void> struct MutableItemTraits_
{
    static_assert(IsMutableItem_<Itm>::value, "Not a Writable item type!");

    static void set_priority(Itm &itm, int p) { itm.set_priority(p); }

    static void set_convex_shape(Itm &itm, const Polygon &shape)
    {
        itm.set_convex_shape(shape);
    }

    static void set_shape(Itm &itm, const ExPolygons &shape)
    {
        itm.set_shape(shape);
    }

    static void set_convex_envelope(Itm &itm, const Polygon &envelope)
    {
        itm.set_convex_envelope(envelope);
    }

    static void set_envelope(Itm &itm, const ExPolygons &envelope)
    {
        itm.set_envelope(envelope);
    }

    template<class T>
    static void set_arbitrary_data(Itm &itm, const std::string &key, T &&data)
    {
        if constexpr (IsWritableDataStore<Itm>)
            set_data(itm, key, std::forward<T>(data));
    }

    static void set_allowed_rotations(Itm                       &itm,
                                      const std::vector<double> &rotations)
    {
        itm.set_allowed_rotations(rotations);
    }
};

template<class T>
using MutableItemTraits = MutableItemTraits_<StripCVRef<T>>;

template<class T> constexpr bool IsMutableItem = IsMutableItem_<T>::value;
template<class T, class TT = T>
using MutableItemOnly = std::enable_if_t<IsMutableItem<T>, TT>;

template<class Itm> void set_priority(Itm &itm, int p)
{
    MutableItemTraits<Itm>::set_priority(itm, p);
}

template<class Itm> void set_convex_shape(Itm &itm, const Polygon &shape)
{
    MutableItemTraits<Itm>::set_convex_shape(itm, shape);
}

template<class Itm> void set_shape(Itm &itm, const ExPolygons &shape)
{
    MutableItemTraits<Itm>::set_shape(itm, shape);
}

template<class Itm>
void set_convex_envelope(Itm &itm, const Polygon &envelope)
{
    MutableItemTraits<Itm>::set_convex_envelope(itm, envelope);
}

template<class Itm> void set_envelope(Itm &itm, const ExPolygons &envelope)
{
    MutableItemTraits<Itm>::set_envelope(itm, envelope);
}

template<class T, class Itm>
void set_arbitrary_data(Itm &itm, const std::string &key, T &&data)
{
    MutableItemTraits<Itm>::set_arbitrary_data(itm, key, std::forward<T>(data));
}

template<class Itm>
void set_allowed_rotations(Itm &itm, const std::vector<double> &rotations)
{
    MutableItemTraits<Itm>::set_allowed_rotations(itm, rotations);
}

template<class ArrItem> int raise_priority(ArrItem &itm)
{
    int ret = get_priority(itm) + 1;
    set_priority(itm, ret);

    return ret;
}

template<class ArrItem> int reduce_priority(ArrItem &itm)
{
    int ret = get_priority(itm) - 1;
    set_priority(itm, ret);

    return ret;
}

template<class It> int lowest_priority(const Range<It> &item_range)
{
    auto minp_it = std::min_element(item_range.begin(),
                                    item_range.end(),
                                    [](auto &itm1, auto &itm2) {
                                        return get_priority(itm1) <
                                               get_priority(itm2);
                                    });

    int min_priority = 0;
    if (minp_it != item_range.end())
        min_priority = get_priority(*minp_it);

    return min_priority;
}

}} // namespace Slic3r::arr2

#endif // MutableItemTraits_HPP
