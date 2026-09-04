#include <numeric>
#include <numbers>

#include "Slic3r/Biz/Arrange/ArrangeItem.hpp"
#include "Slic3r/Biz/Arrange/PackingContext.hpp"
#include "Slic3r/Biz/Arrange/NFP.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/ConvexHull.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/Arrange/Tesselate.hpp"
#include "Slic3r/Biz/Arrange/MinAreaBoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"

namespace Slic3r::Biz::Arrange {

using Algorithms::BoundingBox::sizes;
using Algorithms::ClipperUtils::diff_ex;
using Algorithms::ClipperUtils::offset_ex;
using Algorithms::ClipperUtils::union_ex;
using Algorithms::ExPolygon::simplify;
using Algorithms::Polygon::is_convex;
using Algorithms::Scaling::scaled;
using Domain::BoundingBox2crd;
using Domain::Polygon;
using Domain::Polygons;
using Domain::Vec2crd;
using Domain::Vec2d;

DecomposedShape::DecomposedShape(ConvexShape sh)
{
    ASSERT(is_convex(sh));
    m_shape.emplace_back(std::move(sh));
}

DecomposedShape::DecomposedShape(const ArbitraryShape& sh)
{
    ConvexShapes polygons{convex_decomposition_tess(sh)};
    m_shape.insert(
        m_shape.end(),
        std::make_move_iterator(polygons.begin()),
        std::make_move_iterator(polygons.end())
    );
}

const ConvexShapes& DecomposedShape::contours() const
{
    return m_shape;
}

const Domain::Vec2crd& DecomposedShape::get_translation() const
{
    return m_translation;
}

double DecomposedShape::get_rotation() const
{
    return m_rotation;
}

void DecomposedShape::set_translation(const Domain::Vec2crd& v)
{
    m_translation               = v;
    m_transformed_outline_valid = false;
    m_reference_vertex_valid    = false;
    m_centroid_valid            = false;
}

void DecomposedShape::set_rotation(double v)
{
    m_rotation                  = v;
    m_transformed_outline_valid = false;
    m_reference_vertex_valid    = false;
    m_centroid_valid            = false;
}

const ConvexShapes& DecomposedShape::transformed_outline() const
{
    constexpr auto sc = static_cast<double>(scaled(1.)) * static_cast<double>(scaled(1.));

    if (!m_transformed_outline_valid) {
        m_transformed_outline = contours();
        for (Polygon& poly : m_transformed_outline) {
            poly.rotate(get_rotation());
            poly.translate(get_translation());
        }

        m_area = std::accumulate(
            m_transformed_outline.begin(),
            m_transformed_outline.end(),
            0.,
            [sc](double s, const auto& p) { return s + p.area() / sc; }
        );

        m_convex_hull  = Algorithms::Geometry::convex_hull(m_transformed_outline);
        m_bounding_box = Algorithms::Polygon::get_extents(m_convex_hull);

        m_transformed_outline_valid = true;
    }

    return m_transformed_outline;
}

const ConvexShape& DecomposedShape::convex_hull() const
{
    if (!m_transformed_outline_valid)
        transformed_outline();

    return m_convex_hull;
}

const BoundingBox2crd& DecomposedShape::bounding_box() const
{
    if (!m_transformed_outline_valid)
        transformed_outline();

    return m_bounding_box;
}

const Vec2crd& DecomposedShape::reference_vertex() const
{
    if (!m_reference_vertex_valid) {
        m_reference_vertex = Arrange::reference_vertex(transformed_outline());
        m_refs.clear();
        m_mins.clear();
        m_refs.reserve(m_transformed_outline.size());
        m_mins.reserve(m_transformed_outline.size());
        for (auto& poly : m_transformed_outline) {
            m_refs.emplace_back(Arrange::reference_vertex(poly));
            m_mins.emplace_back(Arrange::min_vertex(poly));
        }
        m_reference_vertex_valid = true;
    }

    return m_reference_vertex;
}

const Vec2crd& DecomposedShape::reference_vertex(size_t i) const
{
    if (!m_reference_vertex_valid) {
        reference_vertex();
    }

    return m_refs[i];
}

const Vec2crd& DecomposedShape::min_vertex(size_t idx) const
{
    if (!m_reference_vertex_valid) {
        reference_vertex();
    }

    return m_mins[idx];
}

double DecomposedShape::area_unscaled() const
{
    // update cache
    transformed_outline();

    return m_area;
}

Vec2crd DecomposedShape::centroid() const
{
    constexpr double area_sc = static_cast<double>(Algorithms::Scaling::scaled(1.))
        * static_cast<double>(Algorithms::Scaling::scaled(1.));

    if (!m_centroid_valid) {
        double total_area = 0.0;
        Vec2d cntr        = Vec2d::Zero();

        for (const Polygon& poly : transformed_outline()) {
            double parea = poly.area() / area_sc;
            Vec2d pcntr  = Algorithms::Scaling::unscaled<double>(poly.centroid());
            total_area += parea;
            cntr += pcntr * parea;
        }

        cntr /= total_area;
        m_centroid       = Algorithms::Scaling::scaled(cntr);
        m_centroid_valid = true;
    }

    return m_centroid;
}

namespace {
double get_min_area_bounding_box_rotation(const ArrangeItem& item)
{
    const ConvexShape convex_hull{item.movable_shape().convex_hull()};
    return MinAreaBoundigBox{convex_hull, MinAreaBoundigBox::pcConvex}.angle_to_X();
}

std::optional<double> get_fit_box_rotation(const ArrangeItem& itm, const BoundingBox2crd& binbb)
{
    const Vec2crd bbsz{sizes(itm.movable_shape().bounding_box())};
    const Vec2crd binbbsz{sizes(binbb)};

    if (bbsz.x() >= binbbsz.x() || bbsz.y() >= binbbsz.y()) {
        return fit_into_box_rotation(itm.movable_shape().convex_hull(), binbb);
    }

    return std::nullopt;
}
} // namespace

ArrangeItem::ArrangeItem(const InputShape& shape, const Settings& settings)
{
    using GeometryHandling::Arbitrary;
    using GeometryHandling::Convex;

    ArbitraryShape offset_shape{
        settings.scaled_offset != 0.0 ? offset_ex(shape.shape, settings.scaled_offset) : shape.shape
    };

    offset_shape = simplify(offset_shape, settings.scaled_simplification_tolerance);

    if (settings.fixed_geometry == Convex && settings.movable_geometry == Convex) {
        ConvexShape convex_shape{Algorithms::Geometry::convex_hull(offset_shape)};
        m_fixed_shape   = std::make_shared<DecomposedShape>(std::move(convex_shape));
        m_movable_shape = m_fixed_shape;
    } else if (settings.fixed_geometry == Arbitrary && settings.movable_geometry == Arbitrary) {
        m_fixed_shape   = std::make_shared<DecomposedShape>(offset_shape);
        m_movable_shape = m_fixed_shape;
    } else if (settings.fixed_geometry == Arbitrary && settings.movable_geometry == Convex) {
        m_fixed_shape = std::make_shared<DecomposedShape>(offset_shape);
        ConvexShape convex_shape{Algorithms::Geometry::convex_hull(offset_shape)};
        m_movable_shape = std::make_shared<DecomposedShape>(std::move(convex_shape));
    } else {
        PANIC("Arbitrary movable shaped geometry with convex fixed shape makes no sense!");
    }

    m_element_ref = shape.element_ref;
}

void ArrangeItem::allow_rotations(const IBed& bed)
{
    // Use the minimum bounding box rotation as a starting point.
    const double minbbr = get_min_area_bounding_box_rotation(*this);
    m_allowed_rotations = {
        {minbbr,
         minbbr + std::numbers::pi / 4.,
         minbbr + std::numbers::pi / 2.,
         minbbr + std::numbers::pi,
         minbbr + 3 * std::numbers::pi / 4.}
    };

    // Add the original rotation of the item if minbbr
    // is not already the original rotation (zero)
    if (std::abs(minbbr) > 0.) {
        m_allowed_rotations.emplace_back(0.);
    }

    // Also try to find the rotation that fits the item
    // into a rectangular bed, given that it cannot fit,
    // and there exists a rotation which can fit.
    auto rectangle_bed{dynamic_cast<const RectangleBed*>(&bed)};
    if (rectangle_bed != nullptr) {
        if (const auto rotation{get_fit_box_rotation(*this, rectangle_bed->bounding_box())}) {
            m_allowed_rotations.emplace_back(*rotation);
        }
    }
}

const DecomposedShape& ArrangeItem::fixed_shape() const
{
    return *m_fixed_shape;
}

const DecomposedShape& ArrangeItem::movable_shape() const
{
    return *m_movable_shape;
}

const std::vector<double>& ArrangeItem::allowed_rotations() const
{
    return m_allowed_rotations;
}

const Domain::Vec2crd& ArrangeItem::get_translation() const
{
    return m_fixed_shape->get_translation();
}

double ArrangeItem::get_rotation() const
{
    return m_fixed_shape->get_rotation();
}

Domain::ElementRef ArrangeItem::get_element_ref() const
{
    return m_element_ref;
}

void ArrangeItem::set_translation(const Domain::Vec2crd& v)
{
    m_fixed_shape->set_translation(v);
    m_movable_shape->set_translation(v);
}

void ArrangeItem::set_rotation(double v)
{
    m_fixed_shape->set_rotation(v);
    m_movable_shape->set_rotation(v);
}

void ArrangeItem::update_caches() const
{
    m_fixed_shape->reference_vertex();
    m_movable_shape->reference_vertex();
    m_fixed_shape->centroid();
    m_movable_shape->centroid();
}

namespace {

Polygons calculate_nfp_unnormalized(
    const ArrangeItem& item,
    const std::vector<ArrangeItem>& fixed_items,
    StopCondition stop_cond
)
{
    size_t cap = 0;

    for (const ArrangeItem& fixitem : fixed_items) {
        const ConvexShapes& outlines = fixitem.fixed_shape().transformed_outline();
        cap += outlines.size();
    }

    const ConvexShapes& item_outlines = item.movable_shape().transformed_outline();

    Polygons nfps;
    nfps.reserve(cap * item_outlines.size());

    Vec2crd ref_whole = item.movable_shape().reference_vertex();
    Polygon subnfp;

    for (const ArrangeItem& fixed : fixed_items) {
        // fixed_polys should already be a set of strictly convex polygons,
        // as ArrangeItem stores convex-decomposed polygons
        const ConvexShapes& fixed_polys = fixed.fixed_shape().transformed_outline();

        for (const ConvexShape& fixed_poly : fixed_polys) {
            Domain::Point max_fixed = reference_vertex(fixed_poly);
            for (size_t mi = 0; mi < item_outlines.size(); ++mi) {
                const ConvexShape& movable  = item_outlines[mi];
                const Domain::Vec2crd& mref = item.movable_shape().reference_vertex(mi);
                subnfp                      = nfp_convex_convex_legacy(fixed_poly, movable);

                Domain::Vec2crd min_movable = item.movable_shape().min_vertex(mi);

                Domain::Vec2crd dtouch    = max_fixed - min_movable;
                Domain::Vec2crd top_other = mref + dtouch;
                Domain::Vec2crd max_nfp   = reference_vertex(subnfp);
                auto dnfp                 = top_other - max_nfp;

                auto d = ref_whole - mref + dnfp;
                subnfp.translate(d);
                nfps.emplace_back(subnfp);
            }

            if (stop_cond())
                break;

            nfps = Biz::Algorithms::ClipperUtils::union_(nfps);
        }

        if (stop_cond()) {
            nfps.clear();
            break;
        }
    }

    return nfps;
}
} // namespace

ArbitraryShape ArrangeItem::calculate_nfp(
    const PackingContext& packing_context,
    const IBed& bed,
    StopCondition stopcond
) const
{
    Domain::Polygons nfps = calculate_nfp_unnormalized(*this, packing_context.all_items(), stopcond);

    Domain::ExPolygons nfp_ex;

    if (!stopcond()) {
        if (dynamic_cast<const InfiniteBed*>(&bed) == nullptr) {
            Domain::ExPolygons ifpbed{bed.ifp_convex(movable_shape().convex_hull())};
            nfp_ex = diff_ex(ifpbed, nfps);
        } else {
            nfp_ex = union_ex(nfps);
        }
    }

    update_caches();

    return nfp_ex;
}

} // namespace Slic3r::Biz::Arrange
