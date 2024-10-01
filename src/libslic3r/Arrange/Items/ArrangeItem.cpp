///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "ArrangeItem.hpp"

#include <numeric>

#include "libslic3r/Arrange/Core/NFP/NFPConcave_Tesselate.hpp"
#include "libslic3r/Arrange/ArrangeImpl.hpp" // IWYU pragma: keep
#include "libslic3r/Arrange/Tasks/ArrangeTaskImpl.hpp" // IWYU pragma: keep
#include "libslic3r/Arrange/Tasks/FillBedTaskImpl.hpp" // IWYU pragma: keep
#include "libslic3r/Arrange/Tasks/MultiplySelectionTaskImpl.hpp" // IWYU pragma: keep
#include "libslic3r/Geometry/ConvexHull.hpp"

namespace Slic3r { namespace arr2 {

const Polygons &DecomposedShape::transformed_outline() const
{
    constexpr auto sc = scaled<double>(1.) * scaled<double>(1.);

    if (!m_transformed_outline_valid) {
        m_transformed_outline = contours();
        for (Polygon &poly : m_transformed_outline) {
            poly.rotate(rotation());
            poly.translate(translation());
        }

        m_area = std::accumulate(m_transformed_outline.begin(),
                                 m_transformed_outline.end(), 0.,
                                 [sc](double s, const auto &p) {
                                     return s + p.area() / sc;
                                 });

        m_convex_hull = Geometry::convex_hull(m_transformed_outline);
        m_bounding_box = get_extents(m_convex_hull);

        m_transformed_outline_valid = true;
    }

    return m_transformed_outline;
}

const Polygon &DecomposedShape::convex_hull() const
{
    if (!m_transformed_outline_valid)
        transformed_outline();

    return m_convex_hull;
}

const BoundingBox &DecomposedShape::bounding_box() const
{
    if (!m_transformed_outline_valid)
        transformed_outline();

    return m_bounding_box;
}

const Vec2crd &DecomposedShape::reference_vertex() const
{
    if (!m_reference_vertex_valid) {
        m_reference_vertex = Slic3r::reference_vertex(transformed_outline());
        m_refs.clear();
        m_mins.clear();
        m_refs.reserve(m_transformed_outline.size());
        m_mins.reserve(m_transformed_outline.size());
        for (auto &poly : m_transformed_outline) {
            m_refs.emplace_back(Slic3r::reference_vertex(poly));
            m_mins.emplace_back(Slic3r::min_vertex(poly));
        }
        m_reference_vertex_valid = true;
    }

    return m_reference_vertex;
}

const Vec2crd &DecomposedShape::reference_vertex(size_t i) const
{
    if (!m_reference_vertex_valid) {
        reference_vertex();
    }

    return m_refs[i];
}

const Vec2crd &DecomposedShape::min_vertex(size_t idx) const
{
    if (!m_reference_vertex_valid) {
        reference_vertex();
    }

    return m_mins[idx];
}

Vec2crd DecomposedShape::centroid() const
{
    constexpr double area_sc = scaled<double>(1.) * scaled(1.);

    if (!m_centroid_valid) {
        double total_area = 0.0;
        Vec2d cntr = Vec2d::Zero();

        for (const Polygon& poly : transformed_outline()) {
            double parea = poly.area() / area_sc;
            Vec2d pcntr = unscaled(poly.centroid());
            total_area += parea;
            cntr += pcntr * parea;
        }

        cntr /= total_area;
        m_centroid = scaled(cntr);
        m_centroid_valid = true;
    }

    return m_centroid;
}

DecomposedShape decompose(const ExPolygons &shape)
{
    return DecomposedShape{convex_decomposition_tess(shape)};
}

DecomposedShape decompose(const Polygon &shape)
{
    Polygons convex_shapes;

    bool is_convex = polygon_is_convex(shape);
    if (is_convex) {
        convex_shapes.emplace_back(shape);
    } else {
        convex_shapes = convex_decomposition_tess(shape);
    }

    return DecomposedShape{std::move(convex_shapes)};
}

ArrangeItem::ArrangeItem(const ExPolygons &shape)
    : m_shape{decompose(shape)}, m_envelope{&m_shape}
{}

ArrangeItem::ArrangeItem(Polygon shape)
    : m_shape{decompose(shape)}, m_envelope{&m_shape}
{}

ArrangeItem::ArrangeItem(const ArrangeItem &other)
{
    this->operator= (other);
}

ArrangeItem::ArrangeItem(ArrangeItem &&other) noexcept
{
    this->operator=(std::move(other));
}

ArrangeItem &ArrangeItem::operator=(const ArrangeItem &other)
{
    m_shape = other.m_shape;
    m_datastore = other.m_datastore;
    m_bed_idx = other.m_bed_idx;
    m_priority = other.m_priority;

    if (other.m_envelope.get() == &other.m_shape)
        m_envelope = &m_shape;
    else
        m_envelope = std::make_unique<DecomposedShape>(other.envelope());

    return *this;
}

void ArrangeItem::set_shape(DecomposedShape shape)
{
    m_shape = std::move(shape);
    m_envelope = &m_shape;
}

void ArrangeItem::set_envelope(DecomposedShape envelope)
{
    m_envelope = std::make_unique<DecomposedShape>(std::move(envelope));

    // Initial synch of transformations of envelope and shape.
    // They need to be in synch all the time
    m_envelope->translation(m_shape.translation());
    m_envelope->rotation(m_shape.rotation());
}

ArrangeItem &ArrangeItem::operator=(ArrangeItem &&other) noexcept
{
    m_shape = std::move(other.m_shape);
    m_datastore = std::move(other.m_datastore);
    m_bed_idx = other.m_bed_idx;
    m_priority = other.m_priority;

    if (other.m_envelope.get() == &other.m_shape)
        m_envelope = &m_shape;
    else
        m_envelope = std::move(other.m_envelope);

    return *this;
}

template struct ImbueableItemTraits_<ArrangeItem>;
template class  ArrangeableToItemConverter<ArrangeItem>;
template struct ArrangeTask<ArrangeItem>;
template struct FillBedTask<ArrangeItem>;
template struct MultiplySelectionTask<ArrangeItem>;
template class  Arranger<ArrangeItem>;

}} // namespace Slic3r::arr2
