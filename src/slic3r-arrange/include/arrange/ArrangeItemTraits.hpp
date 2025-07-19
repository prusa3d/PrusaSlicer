///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef ARRANGE_ITEM_TRAITS_HPP
#define ARRANGE_ITEM_TRAITS_HPP

#include <libslic3r/Point.hpp>

namespace Slic3r { namespace arr2 {

// A logical bed representing an object not being arranged. Either the arrange
// has not yet successfully run on this ArrangePolygon or it could not fit the
// object due to overly large size or invalid geometry.
const constexpr int Unarranged = -1;

const constexpr int PhysicalBedId = 0;

// Basic interface of an arrange item. This struct can be specialized for any
// type that is arrangeable.
template<class ArrItem, class En = void> struct ArrangeItemTraits_ {
    static Vec2crd get_translation(const ArrItem &ap)
    {
        return ap.get_translation();
    }

    static double get_rotation(const ArrItem &ap)
    {
        return ap.get_rotation();
    }

    static int get_bed_index(const ArrItem &ap) { return ap.get_bed_index(); }

    static std::optional<int> get_bed_constraint(const ArrItem &ap)
    {
        return ap.get_bed_constraint();
    }

    static int get_priority(const ArrItem &ap) { return ap.get_priority(); }

    // Setters:

    static void set_translation(ArrItem &ap, const Vec2crd &v)
    {
        ap.set_translation(v);
    }

    static void set_rotation(ArrItem &ap, double v) { ap.set_rotation(v); }

    static void set_bed_index(ArrItem &ap, int v) { ap.set_bed_index(v); }

    static void set_bed_constraint(ArrItem &ap, std::optional<int> v)
    {
        ap.set_bed_constraint(v);
    }
};

template<class T> using ArrangeItemTraits = ArrangeItemTraits_<StripCVRef<T>>;

// Getters:

template<class T> Vec2crd get_translation(const T &itm)
{
    return ArrangeItemTraits<T>::get_translation(itm);
}

template<class T> double get_rotation(const T &itm)
{
    return ArrangeItemTraits<T>::get_rotation(itm);
}

template<class T> int get_bed_index(const T &itm)
{
    return ArrangeItemTraits<T>::get_bed_index(itm);
}

template<class T> int get_priority(const T &itm)
{
    return ArrangeItemTraits<T>::get_priority(itm);
}

template<class T> std::optional<int> get_bed_constraint(const T &itm)
{
    return ArrangeItemTraits<T>::get_bed_constraint(itm);
}

// Setters:

template<class T> void set_translation(T &itm, const Vec2crd &v)
{
    ArrangeItemTraits<T>::set_translation(itm, v);
}

template<class T> void set_rotation(T &itm, double v)
{
    ArrangeItemTraits<T>::set_rotation(itm, v);
}

template<class T> void set_bed_index(T &itm, int v)
{
    ArrangeItemTraits<T>::set_bed_index(itm, v);
}

template<class T> void set_bed_constraint(T &itm, std::optional<int> v)
{
    ArrangeItemTraits<T>::set_bed_constraint(itm, v);
}

// Helper functions for arrange items
template<class ArrItem> bool is_arranged(const ArrItem &ap)
{
    return get_bed_index(ap) > Unarranged;
}

template<class ArrItem> bool is_fixed(const ArrItem &ap)
{
    return get_bed_index(ap) >= PhysicalBedId;
}

template<class ArrItem> bool is_on_physical_bed(const ArrItem &ap)
{
    return get_bed_index(ap) == PhysicalBedId;
}

template<class ArrItem> void translate(ArrItem &ap, const Vec2crd &t)
{
    set_translation(ap, get_translation(ap) + t);
}

template<class ArrItem> void rotate(ArrItem &ap, double rads)
{
    set_rotation(ap, get_rotation(ap) + rads);
}

}} // namespace Slic3r::arr2

#endif // ARRANGE_ITEM_HPP
