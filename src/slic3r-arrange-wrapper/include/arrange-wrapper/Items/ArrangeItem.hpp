///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef ARRANGEITEM_HPP
#define ARRANGEITEM_HPP

#include <boost/variant.hpp>
#include <libslic3r/ClipperUtils.hpp>
#include <assert.h>
#include <stddef.h>
#include <optional>
#include <algorithm>
#include <initializer_list>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <cassert>
#include <cstddef>

#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/BoundingBox.hpp>
#include <libslic3r/AnyPtr.hpp>
#include <libslic3r/Point.hpp>
#include <libslic3r/Polygon.hpp>
#include <libslic3r/libslic3r.h>

#include <arrange/PackingContext.hpp>
#include <arrange/NFP/NFPArrangeItemTraits.hpp>
#include <arrange/NFP/NFP.hpp>
#include <arrange/ArrangeBase.hpp>
#include <arrange/ArrangeItemTraits.hpp>
#include <arrange/DataStoreTraits.hpp>


#include <arrange-wrapper/Items/MutableItemTraits.hpp>
#include <arrange-wrapper/Arrange.hpp>
#include <arrange-wrapper/Tasks/ArrangeTask.hpp>
#include <arrange-wrapper/Tasks/FillBedTask.hpp>
#include <arrange-wrapper/Tasks/MultiplySelectionTask.hpp>
#include <arrange-wrapper/Items/ArbitraryDataStore.hpp>

namespace Slic3r { namespace arr2 {
struct InfiniteBed;

inline bool check_polygons_are_convex(const Polygons &pp) {
    return std::all_of(pp.begin(), pp.end(), [](const Polygon &p) {
        return polygon_is_convex(p);
    });
}

// A class that stores a set of polygons that are garanteed to be all convex.
// They collectively represent a decomposition of a more complex shape into
// its convex part. Note that this class only stores the result of the decomp,
// does not do the job itself. In debug mode, an explicit check is done for
// each component to be convex.
//
// Additionally class stores a translation vector and a rotation angle for the
// stored polygon, plus additional privitives that are all cached cached after
// appying a the transformations. The caching is not thread safe!
class DecomposedShape
{
    Polygons m_shape;

    Vec2crd m_translation{0, 0}; // The translation of the poly
    double  m_rotation{0.0};     // The rotation of the poly in radians

    mutable Polygons m_transformed_outline;
    mutable bool     m_transformed_outline_valid = false;

    mutable Point              m_reference_vertex;
    mutable std::vector<Point> m_refs;
    mutable std::vector<Point> m_mins;
    mutable bool               m_reference_vertex_valid = false;

    mutable Point m_centroid;
    mutable bool  m_centroid_valid = false;

    mutable Polygon m_convex_hull;
    mutable BoundingBox m_bounding_box;
    mutable double  m_area = 0;

public:
    DecomposedShape() = default;

    explicit DecomposedShape(Polygon sh)
    {
        m_shape.emplace_back(std::move(sh));
        assert(check_polygons_are_convex(m_shape));
    }

    explicit DecomposedShape(std::initializer_list<Point> pts)
        : DecomposedShape(Polygon{pts})
    {}

    explicit DecomposedShape(Polygons sh) : m_shape{std::move(sh)}
    {
        assert(check_polygons_are_convex(m_shape));
    }

    const Polygons &contours() const { return m_shape; }

    const Vec2crd &translation() const { return m_translation; }
    double         rotation() const { return m_rotation; }

    void translation(const Vec2crd &v)
    {
        m_translation               = v;
        m_transformed_outline_valid = false;
        m_reference_vertex_valid    = false;
        m_centroid_valid            = false;
    }

    void rotation(double v)
    {
        m_rotation                  = v;
        m_transformed_outline_valid = false;
        m_reference_vertex_valid    = false;
        m_centroid_valid            = false;
    }

    const Polygons &transformed_outline() const;
    const Polygon  &convex_hull() const;
    const BoundingBox &bounding_box() const;

    // The cached reference vertex in the context of NFP creation. Always
    // refers to the leftmost upper vertex.
    const Vec2crd  &reference_vertex() const;
    const Vec2crd  &reference_vertex(size_t idx) const;

    // Also for NFP calculations, the rightmost lowest vertex of the shape.
    const Vec2crd  &min_vertex(size_t idx) const;

    double area_unscaled() const
    {
        // update cache
        transformed_outline();

        return m_area;
    }

    Vec2crd centroid() const;
};

DecomposedShape decompose(const ExPolygons &polys);
DecomposedShape decompose(const Polygon &p);

class ArrangeItem
{
private:
    DecomposedShape m_shape;            // Shape of item when it's not moving
    AnyPtr<DecomposedShape> m_envelope; // Possibly different shape when packed

    ArbitraryDataStore m_datastore;

    int m_bed_idx{Unarranged}; // To which logical bed does this item belong
    int m_priority{0};         // For sorting
    std::optional<int> m_bed_constraint;

public:
    ArrangeItem() = default;

    explicit ArrangeItem(DecomposedShape shape)
        : m_shape(std::move(shape)), m_envelope{&m_shape}
    {}

    explicit ArrangeItem(DecomposedShape shape, DecomposedShape envelope)
        : m_shape(std::move(shape))
        , m_envelope{std::make_unique<DecomposedShape>(std::move(envelope))}
    {}

    explicit ArrangeItem(const ExPolygons &shape);
    explicit ArrangeItem(Polygon shape);
    explicit ArrangeItem(std::initializer_list<Point> pts)
        : ArrangeItem(Polygon{pts})
    {}

    ArrangeItem(const ArrangeItem &);
    ArrangeItem(ArrangeItem &&) noexcept;
    ArrangeItem & operator=(const ArrangeItem &);
    ArrangeItem & operator=(ArrangeItem &&) noexcept;

    int bed_idx() const { return m_bed_idx; }
    int priority() const { return m_priority; }
    std::optional<int> bed_constraint() const { return m_bed_constraint; };

    void bed_idx(int v) { m_bed_idx = v; }
    void priority(int v) { m_priority = v; }
    void bed_constraint(std::optional<int> v) { m_bed_constraint = v; }

    const ArbitraryDataStore &datastore() const { return m_datastore; }
    ArbitraryDataStore &datastore() { return m_datastore; }

    const DecomposedShape & shape() const { return m_shape; }
    void set_shape(DecomposedShape shape);

    const DecomposedShape & envelope() const { return *m_envelope; }
    void set_envelope(DecomposedShape envelope);

    const Vec2crd &translation() const { return m_shape.translation(); }
    double         rotation() const { return m_shape.rotation(); }

    void translation(const Vec2crd &v)
    {
        m_shape.translation(v);
        m_envelope->translation(v);
    }

    void rotation(double v)
    {
        m_shape.rotation(v);
        m_envelope->rotation(v);
    }

    void update_caches() const
    {
        m_shape.reference_vertex();
        m_envelope->reference_vertex();
        m_shape.centroid();
        m_envelope->centroid();
    }
};

template<> struct ArrangeItemTraits_<ArrangeItem>
{
    static const Vec2crd &get_translation(const ArrangeItem &itm)
    {
        return itm.translation();
    }

    static double get_rotation(const ArrangeItem &itm)
    {
        return itm.rotation();
    }

    static int get_bed_index(const ArrangeItem &itm)
    {
        return itm.bed_idx();
    }

    static int get_priority(const ArrangeItem &itm)
    {
        return itm.priority();
    }

    static std::optional<int> get_bed_constraint(const ArrangeItem &itm)
    {
        return itm.bed_constraint();
    }

    // Setters:

    static void set_translation(ArrangeItem &itm, const Vec2crd &v)
    {
        itm.translation(v);
    }

    static void set_rotation(ArrangeItem &itm, double v)
    {
        itm.rotation(v);
    }

    static void set_bed_index(ArrangeItem &itm, int v)
    {
        itm.bed_idx(v);
    }

    static void set_bed_constraint(ArrangeItem &itm, std::optional<int> v)
    {
        itm.bed_constraint(v);
    }
};

// Some items can be containers of arbitrary data stored under string keys.
template<> struct DataStoreTraits_<ArrangeItem>
{
    static constexpr bool Implemented = true;

    template<class T>
    static const T *get(const ArrangeItem &itm, const std::string &key)
    {
        return itm.datastore().get<T>(key);
    }

    // Same as above just not const.
    template<class T>
    static T *get(ArrangeItem &itm, const std::string &key)
    {
        return itm.datastore().get<T>(key);
    }

    static bool has_key(const ArrangeItem &itm, const std::string &key)
    {
        return itm.datastore().has_key(key);
    }
};

template<> struct WritableDataStoreTraits_<ArrangeItem>
{
    static constexpr bool Implemented = true;

    template<class T>
    static void set(ArrangeItem &itm,
                    const std::string &key,
                    T &&data)
    {
        itm.datastore().add(key, std::forward<T>(data));
    }
};

template<class FixedIt, class StopCond = DefaultStopCondition>
static Polygons calculate_nfp_unnormalized(const ArrangeItem    &item,
                                           const Range<FixedIt> &fixed_items,
                                           StopCond &&stop_cond = {})
{
    size_t cap = 0;

    for (const ArrangeItem &fixitem : fixed_items) {
        const Polygons &outlines = fixitem.shape().transformed_outline();
        cap += outlines.size();
    }

    const Polygons &item_outlines = item.envelope().transformed_outline();

    auto nfps = reserve_polygons(cap * item_outlines.size());

    Vec2crd ref_whole = item.envelope().reference_vertex();
    Polygon subnfp;

    for (const ArrangeItem &fixed : fixed_items) {
        // fixed_polys should already be a set of strictly convex polygons,
        // as ArrangeItem stores convex-decomposed polygons
        const Polygons & fixed_polys = fixed.shape().transformed_outline();

        for (const Polygon &fixed_poly : fixed_polys) {
            Point max_fixed = Slic3r::reference_vertex(fixed_poly);
            for (size_t mi = 0; mi < item_outlines.size(); ++mi) {
                const Polygon &movable = item_outlines[mi];
                const Vec2crd &mref = item.envelope().reference_vertex(mi);
                subnfp = nfp_convex_convex_legacy(fixed_poly, movable);

                Vec2crd min_movable = item.envelope().min_vertex(mi);

                Vec2crd dtouch = max_fixed - min_movable;
                Vec2crd top_other = mref + dtouch;
                Vec2crd max_nfp = Slic3r::reference_vertex(subnfp);
                auto dnfp = top_other - max_nfp;

                auto d = ref_whole - mref + dnfp;
                subnfp.translate(d);
                nfps.emplace_back(subnfp);
            }

            if (stop_cond())
                break;

            nfps = union_(nfps);
        }

        if (stop_cond()) {
            nfps.clear();
            break;
        }
    }

    return nfps;
}

template<> struct NFPArrangeItemTraits_<ArrangeItem> {
    template<class Context, class Bed, class StopCond>
    static ExPolygons calculate_nfp(const ArrangeItem &item,
                                    const Context &packing_context,
                                    const Bed &bed,
                                    StopCond &&stopcond)
    {
        auto static_items = all_items_range(packing_context);
        Polygons nfps = arr2::calculate_nfp_unnormalized(item, static_items, stopcond);

        ExPolygons nfp_ex;

        if (!stopcond()) {
            if constexpr (!std::is_convertible_v<Bed, InfiniteBed>) {
                ExPolygons ifpbed = ifp_convex(bed, item.envelope().convex_hull());
                nfp_ex = diff_ex(ifpbed, nfps);
            } else {
                nfp_ex = union_ex(nfps);
            }
        }

        item.update_caches();

        return nfp_ex;
    }

    static const Vec2crd& reference_vertex(const ArrangeItem &item)
    {
        return item.envelope().reference_vertex();
    }

    static BoundingBox envelope_bounding_box(const ArrangeItem &itm)
    {
        return itm.envelope().bounding_box();
    }

    static BoundingBox fixed_bounding_box(const ArrangeItem &itm)
    {
        return itm.shape().bounding_box();
    }

    static double envelope_area(const ArrangeItem &itm)
    {
        return itm.envelope().area_unscaled() * scaled<double>(1.) *
               scaled<double>(1.);
    }

    static double fixed_area(const ArrangeItem &itm)
    {
        return itm.shape().area_unscaled() * scaled<double>(1.) *
               scaled<double>(1.);
    }

    static const Polygons & envelope_outline(const ArrangeItem &itm)
    {
        return itm.envelope().transformed_outline();
    }

    static const Polygons & fixed_outline(const ArrangeItem &itm)
    {
        return itm.shape().transformed_outline();
    }

    static const Polygon & envelope_convex_hull(const ArrangeItem &itm)
    {
        return itm.envelope().convex_hull();
    }

    static const Polygon & fixed_convex_hull(const ArrangeItem &itm)
    {
        return itm.shape().convex_hull();
    }

    static const std::vector<double>& allowed_rotations(const ArrangeItem &itm)
    {
        static const std::vector<double> ret_zero = {0.};

        const std::vector<double> * ret_ptr = &ret_zero;

        auto rots = get_data<std::vector<double>>(itm, "rotations");
        if (rots) {
            ret_ptr = rots;
        }

        return *ret_ptr;
    }

    static Vec2crd fixed_centroid(const ArrangeItem &itm)
    {
        return itm.shape().centroid();
    }

    static Vec2crd envelope_centroid(const ArrangeItem &itm)
    {
        return itm.envelope().centroid();
    }
};

template<> struct IsMutableItem_<ArrangeItem>: public std::true_type {};

template<>
struct MutableItemTraits_<ArrangeItem> {

    static void set_priority(ArrangeItem &itm, int p) { itm.priority(p); }
    static void set_convex_shape(ArrangeItem &itm, const Polygon &shape)
    {
        itm.set_shape(DecomposedShape{shape});
    }
    static void set_shape(ArrangeItem &itm, const ExPolygons &shape)
    {
        itm.set_shape(decompose(shape));
    }
    static void set_convex_envelope(ArrangeItem &itm, const Polygon &envelope)
    {
        itm.set_envelope(DecomposedShape{envelope});
    }
    static void set_envelope(ArrangeItem &itm, const ExPolygons &envelope)
    {
        itm.set_envelope(decompose(envelope));
    }

    template<class T>
    static void set_arbitrary_data(ArrangeItem &itm, const std::string &key, T &&data)
    {
        set_data(itm, key, std::forward<T>(data));
    }

    static void set_allowed_rotations(ArrangeItem &itm, const std::vector<double> &rotations)
    {
        set_data(itm, "rotations", rotations);
    }
};

extern template struct ImbueableItemTraits_<ArrangeItem>;
extern template class  ArrangeableToItemConverter<ArrangeItem>;
extern template struct ArrangeTask<ArrangeItem>;
extern template struct FillBedTask<ArrangeItem>;
extern template struct MultiplySelectionTask<ArrangeItem>;
extern template class  Arranger<ArrangeItem>;

}} // namespace Slic3r::arr2

#endif // ARRANGEITEM_HPP
