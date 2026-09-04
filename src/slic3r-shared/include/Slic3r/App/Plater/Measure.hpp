#pragma once

#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"

#include <optional>

struct indexed_triangle_set;

namespace Slic3r::App::Plater::Measure {

enum class SurfaceFeatureType : uint8_t
{
    Undefined,
    Point,
    Edge,
    Circle,
    Plane
};

class SurfaceFeature
{
public:
    SurfaceFeature(
        SurfaceFeatureType type,
        const Domain::Vec3d& pt1,
        const Domain::Vec3d& pt2,
        std::optional<Domain::Vec3d> pt3 = std::nullopt,
        double value                     = 0.0
    ) :
        m_type(type),
        m_pt1(pt1),
        m_pt2(pt2),
        m_pt3(pt3),
        m_value(value)
    {}

    explicit SurfaceFeature(const Domain::Vec3d& pt) : m_type{SurfaceFeatureType::Point}, m_pt1{pt}
    {}

    // Get type of this feature.
    SurfaceFeatureType type() const
    {
        return m_type;
    }

    // For points, return the point.
    Domain::Vec3d point() const
    {
        assert(m_type == SurfaceFeatureType::Point);
        return m_pt1;
    }

    // For edges, return start and end.
    std::pair<Domain::Vec3d, Domain::Vec3d> edge() const
    {
        assert(m_type == SurfaceFeatureType::Edge);
        return std::make_pair(m_pt1, m_pt2);
    }

    // For circles, return center, radius and normal.
    std::tuple<Domain::Vec3d, double, Domain::Vec3d> circle() const
    {
        assert(m_type == SurfaceFeatureType::Circle);
        return std::make_tuple(m_pt1, m_value, m_pt2);
    }

    // For planes, return index into vector provided by Measuring::get_plane_triangle_indices, normal and point.
    std::tuple<int, Domain::Vec3d, Domain::Vec3d> plane() const
    {
        assert(m_type == SurfaceFeatureType::Plane);
        return std::make_tuple(int(m_value), m_pt1, m_pt2);
    }

    // For anything, return an extra point that should also be considered a part of this.
    std::optional<Domain::Vec3d> extra_point() const
    {
        assert(m_type != SurfaceFeatureType::Undefined);
        return m_pt3;
    }

    bool operator==(const SurfaceFeature& other) const
    {
        if (this->m_type != other.m_type)
            return false;
        switch (this->m_type) {
        case SurfaceFeatureType::Undefined: {
            break;
        }
        case SurfaceFeatureType::Point: {
            return (this->m_pt1.isApprox(other.m_pt1));
        }
        case SurfaceFeatureType::Edge: {
            return (this->m_pt1.isApprox(other.m_pt1) && this->m_pt2.isApprox(other.m_pt2))
                || (this->m_pt1.isApprox(other.m_pt2) && this->m_pt2.isApprox(other.m_pt1));
        }
        case SurfaceFeatureType::Plane:
        case SurfaceFeatureType::Circle: {
            return (
                this->m_pt1.isApprox(other.m_pt1)
                && this->m_pt2.isApprox(other.m_pt2)
                && std::abs(this->m_value - other.m_value) < Domain::EPSILON
            );
        }
        }

        return false;
    }

    bool operator!=(const SurfaceFeature& other) const
    {
        return !operator==(other);
    }

private:
    SurfaceFeatureType m_type{SurfaceFeatureType::Undefined};
    Domain::Vec3d m_pt1{Domain::Vec3d::Zero()};
    Domain::Vec3d m_pt2{Domain::Vec3d::Zero()};
    std::optional<Domain::Vec3d> m_pt3;
    double m_value{0.0};
};

class MeasuringImpl;

class Measuring
{
public:
    // Construct the measurement object on a given its.
    explicit Measuring(const indexed_triangle_set& its);
    ~Measuring();

    // Given a face_idx where the mouse cursor points, return a feature that
    // should be highlighted (if any).
    std::optional<SurfaceFeature> feature(size_t face_idx, const Domain::Vec3d& point) const;

    // Return total number of planes.
    size_t num_of_planes() const;

    // Returns a list of triangle indices for given plane.
    const std::vector<int>& plane_triangle_indices(int idx) const;

    // Returns the surface features of the plane with the given index
    const std::vector<SurfaceFeature>& plane_features(unsigned int plane_id) const;

    // Returns the mesh used for measuring
    const indexed_triangle_set& its() const;

private:
    std::unique_ptr<MeasuringImpl> priv;
};

struct DistAndPoints
{
    DistAndPoints(double dist_, Domain::Vec3d from_, Domain::Vec3d to_) :
        dist(dist_),
        from(from_),
        to(to_)
    {}

    double dist;
    Domain::Vec3d from;
    Domain::Vec3d to;
};

struct AngleAndEdges
{
    AngleAndEdges(
        double angle_,
        const Domain::Vec3d& center_,
        const std::pair<Domain::Vec3d, Domain::Vec3d>& e1_,
        const std::pair<Domain::Vec3d, Domain::Vec3d>& e2_,
        double radius_,
        bool coplanar_
    ) :
        angle(angle_),
        center(center_),
        e1(e1_),
        e2(e2_),
        radius(radius_),
        coplanar(coplanar_)
    {}

    double angle;
    Domain::Vec3d center;
    std::pair<Domain::Vec3d, Domain::Vec3d> e1;
    std::pair<Domain::Vec3d, Domain::Vec3d> e2;
    double radius;
    bool coplanar;

    static const AngleAndEdges Dummy;
};

struct MeasurementResult
{
    std::optional<AngleAndEdges> angle;
    std::optional<DistAndPoints> distance_infinite;
    std::optional<DistAndPoints> distance_strict;
    std::optional<Domain::Vec3d> distance_xyz;

    bool has_distance_data() const
    {
        return distance_infinite.has_value() || distance_strict.has_value();
    }

    bool has_any_data() const
    {
        return angle.has_value()
            || distance_infinite.has_value()
            || distance_strict.has_value()
            || distance_xyz.has_value();
    }
};

// Returns distance/angle between two SurfaceFeatures.
MeasurementResult measurement(
    const SurfaceFeature& a,
    const SurfaceFeature& b,
    const Measuring* measuring = nullptr
);

inline Domain::Vec3d edge_direction(const Domain::Vec3d& from, const Domain::Vec3d& to)
{
    return (to - from).normalized();
}

inline Domain::Vec3d edge_direction(const std::pair<Domain::Vec3d, Domain::Vec3d>& e)
{
    return edge_direction(e.first, e.second);
}

inline Domain::Vec3d edge_direction(const SurfaceFeature& edge)
{
    assert(edge.type() == SurfaceFeatureType::Edge);
    return edge_direction(edge.edge());
}

inline Domain::Vec3d plane_normal(const SurfaceFeature& plane)
{
    assert(plane.type() == SurfaceFeatureType::Plane);
    return std::get<1>(plane.plane());
}

inline bool are_parallel(const Domain::Vec3d& v1, const Domain::Vec3d& v2)
{
    return std::abs(std::abs(v1.dot(v2)) - 1.0) < Domain::EPSILON;
}

inline bool are_perpendicular(const Domain::Vec3d& v1, const Domain::Vec3d& v2)
{
    return std::abs(v1.dot(v2)) < Domain::EPSILON;
}

inline bool are_parallel(
    const std::pair<Domain::Vec3d, Domain::Vec3d>& e1,
    const std::pair<Domain::Vec3d, Domain::Vec3d>& e2
)
{
    return are_parallel(e1.second - e1.first, e2.second - e2.first);
}

inline bool are_parallel(const SurfaceFeature& f1, const SurfaceFeature& f2)
{
    if (f1.type() == SurfaceFeatureType::Edge && f2.type() == SurfaceFeatureType::Edge)
        return are_parallel(edge_direction(f1), edge_direction(f2));
    else if (f1.type() == SurfaceFeatureType::Edge && f2.type() == SurfaceFeatureType::Plane)
        return are_perpendicular(edge_direction(f1), plane_normal(f2));
    else
        return false;
}

inline bool are_perpendicular(const SurfaceFeature& f1, const SurfaceFeature& f2)
{
    if (f1.type() == SurfaceFeatureType::Edge && f2.type() == SurfaceFeatureType::Edge)
        return are_perpendicular(edge_direction(f1), edge_direction(f2));
    else if (f1.type() == SurfaceFeatureType::Edge && f2.type() == SurfaceFeatureType::Plane)
        return are_parallel(edge_direction(f1), plane_normal(f2));
    else
        return false;
}

} // namespace Slic3r::App::Plater::Measure
