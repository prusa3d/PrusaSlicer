#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"

#include <admesh/stl.h>

namespace Slic3r::Domain {

enum class BedType
{
    // Not set yet or undefined.
    Invalid,
    // Rectangular print bed. Most common, cheap to work with.
    Rectangle,
    // Circular print bed. Common on detals, cheap to work with.
    Circle,
    // Convex print bed. Complex to process.
    Convex,
    // Some non convex shape.
    Custom
};

struct BedSegments
{
    std::size_t x_count{1};
    std::size_t y_count{1};
};

bool operator==(const BedSegments& a, const BedSegments& b);

struct BedCreationData
{
    BedType type{ BedType ::Invalid };
    Vec2ds contour;
    indexed_triangle_set contour_mesh;
    float max_print_height{ 0.0f };
    std::optional<BedSegments> segments;
    std::optional<Vec2d> auxiliary_travel_anchor;
    std::string model_filename;
    std::string texture_filename;
};

class Bed : public ObjectBase
{
public:
    [[nodiscard]] static Bed create(const BedCreationData& data);

    [[nodiscard]] BedType type() const
    {
        return m_type;
    }

    [[nodiscard]] const Vec2d& center() const
    {
        return m_center;
    }

    [[nodiscard]] const Vec2ds& contour() const
    {
        return m_contour;
    }

    [[nodiscard]] const BoundingBoxf& contour_aabb() const
    {
        return m_contour_aabb;
    }

    [[nodiscard]] Vec2d contour_aabb_extent() const
    {
        return m_contour_aabb.max - m_contour_aabb.min;
    }

    [[nodiscard]] float max_print_height() const
    {
        return m_max_print_height;
    }

    [[nodiscard]] std::optional<BedSegments> segments() const
    {
        return m_segments;
    }

    [[nodiscard]] std::optional<Vec2d> auxiliary_travel_anchor() const
    {
        return m_auxiliary_travel_anchor;
    }

    [[nodiscard]] const indexed_triangle_set& contour_mesh() const
    {
        return m_contour_mesh;
    }

    [[nodiscard]] const std::string& model_filename() const
    {
        return m_model_filename;
    }

    [[nodiscard]] const std::string& texture_filename() const
    {
        return m_texture_filename;
    }

    // Compare the full content of this Bed with the given one 
    bool operator==(const Bed& rhs) const;

    // Compare the content of this Bed with the given one, as they come from a call to Bed::create()
    bool matches(const Bed& rhs) const;

private:
    BedType m_type{BedType::Invalid};
    Vec2ds m_contour;
    indexed_triangle_set m_contour_mesh;
    float m_max_print_height{0.0f};
    std::optional<BedSegments> m_segments;
    std::optional<Vec2d> m_auxiliary_travel_anchor;
    std::string m_model_filename;
    std::string m_texture_filename;

    Vec2d m_center{ Vec2d::Zero() };
    BoundingBoxf m_contour_aabb;
};
using BedPtr = std::unique_ptr<Bed>;

} // namespace Slic3r::Domain
