///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef SIMPLEARRANGEITEM_HPP
#define SIMPLEARRANGEITEM_HPP

#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <libslic3r/Polygon.hpp>
#include <libslic3r/Geometry/ConvexHull.hpp>
#include <libslic3r/BoundingBox.hpp>
#include <libslic3r/ClipperUtils.hpp>
#include <libslic3r/ObjectID.hpp>
#include <libslic3r/Point.hpp>

#include <arrange/ArrangeItemTraits.hpp>
#include <arrange/PackingContext.hpp>
#include <arrange/NFP/NFPArrangeItemTraits.hpp>
#include <arrange/NFP/NFP.hpp>

#include <arrange-wrapper/Arrange.hpp>
#include <arrange-wrapper/Tasks/FillBedTask.hpp>
#include <arrange-wrapper/Tasks/ArrangeTask.hpp>
#include <arrange-wrapper/Items/MutableItemTraits.hpp>


namespace Slic3r { namespace arr2 {
struct InfiniteBed;

class SimpleArrangeItem {
    Polygon m_shape;

    Vec2crd m_translation = Vec2crd::Zero();
    double  m_rotation = 0.;
    int     m_priority = 0;
    int     m_bed_idx = Unarranged;
    std::optional<int> m_bed_constraint;

    std::vector<double> m_allowed_rotations = {0.};
    ObjectID m_obj_id;

public:
    explicit SimpleArrangeItem(Polygon chull = {}): m_shape{std::move(chull)} {}

    void set_shape(Polygon chull) { m_shape = std::move(chull); }

    const Vec2crd& get_translation() const noexcept { return m_translation; }
    double get_rotation() const noexcept { return m_rotation; }
    int get_priority() const noexcept { return m_priority; }
    int get_bed_index() const noexcept { return m_bed_idx; }
    std::optional<int> get_bed_constraint() const noexcept {
        return m_bed_constraint;
    }

    void set_translation(const Vec2crd &v) { m_translation = v; }
    void set_rotation(double v) noexcept { m_rotation = v; }
    void set_priority(int v) noexcept { m_priority = v; }
    void set_bed_index(int v) noexcept { m_bed_idx = v; }
    void set_bed_constraint(std::optional<int> v) noexcept { m_bed_constraint = v; }

    const Polygon &shape() const { return m_shape; }
    Polygon outline() const;

    const auto &allowed_rotations() const noexcept
    {
        return m_allowed_rotations;
    }

    void set_allowed_rotations(std::vector<double> rots)
    {
        m_allowed_rotations = std::move(rots);
    }

    void set_object_id(const ObjectID &id) noexcept { m_obj_id = id; }
    const ObjectID & get_object_id() const noexcept { return m_obj_id; }
};

template<> struct NFPArrangeItemTraits_<SimpleArrangeItem>
{
    template<class Context, class Bed, class StopCond>
    static ExPolygons calculate_nfp(const SimpleArrangeItem &item,
                                    const Context &packing_context,
                                    const Bed &bed,
                                    StopCond &&stop_cond)
    {
        auto fixed_items = all_items_range(packing_context);
        auto nfps = reserve_polygons(fixed_items.size());
        for (const SimpleArrangeItem &fixed_part : fixed_items) {
            Polygon subnfp = nfp_convex_convex_legacy(fixed_part.outline(),
                                                      item.outline());
            nfps.emplace_back(subnfp);


            if (stop_cond()) {
                nfps.clear();
                break;
            }
        }

        ExPolygons nfp_ex;
        if (!stop_cond()) {
            if constexpr (!std::is_convertible_v<Bed, InfiniteBed>) {
                ExPolygons ifpbed = ifp_convex(bed, item.outline());
                nfp_ex = diff_ex(ifpbed, nfps);
            } else {
                nfp_ex = union_ex(nfps);
            }
        }

        return nfp_ex;
    }

    static Vec2crd reference_vertex(const SimpleArrangeItem &item)
    {
        return Slic3r::reference_vertex(item.outline());
    }

    static BoundingBox envelope_bounding_box(const SimpleArrangeItem &itm)
    {
        return get_extents(itm.outline());
    }

    static BoundingBox fixed_bounding_box(const SimpleArrangeItem &itm)
    {
        return get_extents(itm.outline());
    }

    static Polygons envelope_outline(const SimpleArrangeItem &itm)
    {
        return {itm.outline()};
    }

    static Polygons fixed_outline(const SimpleArrangeItem &itm)
    {
        return {itm.outline()};
    }

    static Polygon envelope_convex_hull(const SimpleArrangeItem &itm)
    {
        return Geometry::convex_hull(itm.outline());
    }

    static Polygon fixed_convex_hull(const SimpleArrangeItem &itm)
    {
        return Geometry::convex_hull(itm.outline());
    }

    static double envelope_area(const SimpleArrangeItem &itm)
    {
        return itm.shape().area();
    }

    static double fixed_area(const SimpleArrangeItem &itm)
    {
        return itm.shape().area();
    }

    static const auto& allowed_rotations(const SimpleArrangeItem &itm) noexcept
    {
        return itm.allowed_rotations();
    }

    static Vec2crd fixed_centroid(const SimpleArrangeItem &itm) noexcept
    {
        return itm.outline().centroid();
    }

    static Vec2crd envelope_centroid(const SimpleArrangeItem &itm) noexcept
    {
        return itm.outline().centroid();
    }
};

template<> struct IsMutableItem_<SimpleArrangeItem>: public std::true_type {};

template<>
struct MutableItemTraits_<SimpleArrangeItem> {

    static void set_priority(SimpleArrangeItem &itm, int p) { itm.set_priority(p); }
    static void set_convex_shape(SimpleArrangeItem &itm, const Polygon &shape)
    {
        itm.set_shape(shape);
    }
    static void set_shape(SimpleArrangeItem &itm, const ExPolygons &shape)
    {
        itm.set_shape(Geometry::convex_hull(shape));
    }
    static void set_convex_envelope(SimpleArrangeItem &itm, const Polygon &envelope)
    {
        itm.set_shape(envelope);
    }
    static void set_envelope(SimpleArrangeItem &itm, const ExPolygons &envelope)
    {
        itm.set_shape(Geometry::convex_hull(envelope));
    }

    template<class T>
    static void set_data(SimpleArrangeItem &itm, const std::string &key, T &&data)
    {}

    static void set_allowed_rotations(SimpleArrangeItem &itm, const std::vector<double> &rotations)
    {
        itm.set_allowed_rotations(rotations);
    }
};

template<> struct ImbueableItemTraits_<SimpleArrangeItem>
{
    static void imbue_id(SimpleArrangeItem &itm, const ObjectID &id)
    {
        itm.set_object_id(id);
    }

    static std::optional<ObjectID> retrieve_id(const SimpleArrangeItem &itm)
    {
        std::optional<ObjectID> ret;
        if (itm.get_object_id().valid())
            ret = itm.get_object_id();

        return ret;
    }
};

extern template class  ArrangeableToItemConverter<SimpleArrangeItem>;
extern template struct ArrangeTask<SimpleArrangeItem>;
extern template struct FillBedTask<SimpleArrangeItem>;
extern template struct MultiplySelectionTask<SimpleArrangeItem>;
extern template class  Arranger<SimpleArrangeItem>;

}} // namespace Slic3r::arr2

#endif // SIMPLEARRANGEITEM_HPP
