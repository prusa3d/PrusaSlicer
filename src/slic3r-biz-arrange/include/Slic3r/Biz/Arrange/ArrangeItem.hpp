#pragma once

#include <memory>
#include <optional>
#include "Slic3r/Biz/Arrange/Bed.hpp"
#include "Slic3r/Biz/Arrange/Settings.hpp"
#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"

namespace Slic3r::Biz::Arrange {

struct PackingContext;

using StopCondition = std::function<bool()>;

using ConvexShape    = Domain::Polygon;
using ConvexShapes   = Domain::Polygons;
using ArbitraryShape = Domain::ExPolygons;

class DecomposedShape
{
    ConvexShapes m_shape;

    Domain::Vec2crd m_translation{0, 0}; // The translation of the poly
    double m_rotation{0.0}; // The rotation of the poly in radians

    mutable ConvexShapes m_transformed_outline;
    mutable bool m_transformed_outline_valid = false;

    mutable Domain::Point m_reference_vertex;
    mutable std::vector<Domain::Point> m_refs;
    mutable std::vector<Domain::Point> m_mins;
    mutable bool m_reference_vertex_valid = false;

    mutable Domain::Point m_centroid;
    mutable bool m_centroid_valid = false;

    mutable ConvexShape m_convex_hull;
    mutable Domain::BoundingBox2crd m_bounding_box;
    mutable double m_area = 0;

public:
    explicit DecomposedShape(ConvexShape sh);
    explicit DecomposedShape(const ArbitraryShape& sh);

    const Domain::Polygons& contours() const;

    const Domain::Vec2crd& get_translation() const;

    double get_rotation() const;

    void set_translation(const Domain::Vec2crd& v);

    void set_rotation(double v);

    const ConvexShapes& transformed_outline() const;
    const ConvexShape& convex_hull() const;
    const Domain::BoundingBox2crd& bounding_box() const;

    // The cached reference vertex in the context of NFP creation. Always
    // refers to the leftmost upper vertex.
    const Domain::Vec2crd& reference_vertex() const;
    const Domain::Vec2crd& reference_vertex(size_t idx) const;

    // Also for NFP calculations, the rightmost lowest vertex of the shape.
    const Domain::Vec2crd& min_vertex(size_t idx) const;

    double area_unscaled() const;

    Domain::Vec2crd centroid() const;
};

struct InputShape
{
    Domain::ElementRef element_ref;
    ArbitraryShape shape;
};

class ArrangeItem
{
public:
    std::optional<Domain::Vec2d> gravity_sink;
    bool is_wipe_tower{false};

    ArrangeItem(const InputShape& shape, const Settings& settings);

    void allow_rotations(const IBed& bed);

    const DecomposedShape& fixed_shape() const;

    const DecomposedShape& movable_shape() const;

    const std::vector<double>& allowed_rotations() const;

    const Domain::Vec2crd& get_translation() const;

    double get_rotation() const;

    Domain::ElementRef get_element_ref() const;

    void set_translation(const Domain::Vec2crd& v);

    void set_rotation(double v);

    void update_caches() const;

    ArbitraryShape calculate_nfp(
        const PackingContext& packing_context,
        const IBed& bed,
        StopCondition stopcond
    ) const;

private:
    std::shared_ptr<DecomposedShape> m_fixed_shape;
    std::shared_ptr<DecomposedShape> m_movable_shape;
    std::vector<double> m_allowed_rotations{0.0};
    Domain::ElementRef m_element_ref;
};
} // namespace Slic3r::Biz::Arrange
