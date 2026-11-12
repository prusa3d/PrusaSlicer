#include "Slic3r/Biz/Algorithms/Line.hpp"


#include "Slic3r/Math.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Domain/Line.hpp"

namespace Slic3r::Biz::Algorithms::Line {

namespace Impl {
template<typename L>
struct line_traits
{
    using line_type = L;
    using point_type = typename line_type::point_type;

    static point_type get_a(const line_type& line);
    static point_type get_b(const line_type& line);
};

/**
 * Returns the squared distance to the nearest point of line.
 */
template<class L>
double distance_to_squared(const L& line, const typename line_traits<L>::point_type& point, typename line_traits<L>::point_type& nearest_point_out)
{
    using ScalarType = typename line_traits<L>::scalar_type;
    using PointType  = typename line_traits<L>::point_type;
    using VecType    = decltype(std::declval<PointType>().template cast<double>().eval());

    const VecType v  = (line_traits<L>::get_b(line) - line_traits<L>::get_a(line)).template cast<double>();
    const VecType va = (point - line_traits<L>::get_a(line)).template cast<double>();
    const double  l2 = v.squaredNorm();
    if (l2 == 0.) {
        // a == b case
        nearest_point_out = line_traits<L>::get_a(line);
        return va.squaredNorm();
    }

    // Consider the line extending the segment, parameterized as a + t (b - a).
    // We find projection of this point onto the line.
    // It falls where t = [(this-a) . (b-a)] / |b-a|^2
    const double t = va.dot(v);
    if (t <= 0.) {
        // beyond the 'a' end of the segment
        nearest_point_out = line_traits<L>::get_a(line);
        return va.squaredNorm();
    } else if (t >= l2) {
        // beyond the 'b' end of the segment
        nearest_point_out = line_traits<L>::get_b(line);
        return (point - line_traits<L>::get_b(line)).template cast<double>().squaredNorm();
    }

    const VecType w = ((t / l2) * v).eval();
    nearest_point_out = (line_traits<L>::get_a(line).template cast<double>() + w).template cast<ScalarType>();
    return (w - va).squaredNorm();
}

/**
 * Returns the squared distance to the nearest point of line.
 */
template<class L>
double distance_to_squared(const L& line, const typename line_traits<L>::point_type& point)
{
    using PointType = typename line_traits<L>::point_type;

    PointType nearest_point;
    return distance_to_squared<L>(line, point, nearest_point);
}

/**
 * Returns the distance to the nearest point of line.
 */
template<class L>
double distance_to(const L& line, const typename line_traits<L>::point_type& point)
{
    return std::sqrt(distance_to_squared(line, point));
}

/**
 * Returns the squared distance to the nearest point on the infinite segment.
 *
 * @note The nearest point (and returned squared distance to this point) could be beyond the 'a' and 'b' ends of the segment.
 */
template<class L>
double distance_to_infinite_squared(const L& line, const typename line_traits<L>::point_type& point, typename line_traits<L>::point_type& nearest_point_out)
{
    using ScalarType = typename line_traits<L>::scalar_type;
    using PointType  = typename line_traits<L>::point_type;
    using VecType    = decltype(std::declval<PointType>().template cast<double>().eval());

    const VecType v  = (line_traits<L>::get_b(line) - line_traits<L>::get_a(line)).template cast<double>();
    const VecType va = (point - line_traits<L>::get_a(line)).template cast<double>();
    const double  l2 = v.squaredNorm(); // avoid a sqrt
    if (l2 == 0.) {
        // a == b case
        nearest_point_out = line_traits<L>::get_a(line);
        return va.squaredNorm();
    }

    // Consider the line extending the segment, parameterized as a + t (b - a).
    // We find projection of this point onto the line.
    // It falls where t = [(this-a) . (b-a)] / |b-a|^2
    const double t = va.dot(v) / l2;
    nearest_point_out = (line_traits<L>::get_a(line).template cast<double>() + t * v).template cast<ScalarType>();
    return (t * v - va).squaredNorm();
}

/**
 * Returns the squared distance to the nearest point on the infinite segment.
 *
 * @note The nearest point (and returned squared distance to this point) could be beyond the 'a' and 'b' ends of the segment.
 */
template<class L>
double distance_to_infinite_squared(const L& line, const typename line_traits<L>::point_type& point)
{
    using PointType = typename line_traits<L>::point_type;

    PointType nearest_point;
    return distance_to_infinite_squared<L>(line, point, nearest_point);
}

/**
 * Returns the distance to the nearest point on the infinite segment.
 *
 * @note The nearest point (and returned squared distance to this point) could be beyond the 'a' and 'b' ends of the segment.
 */
template<class L>
double distance_to_infinite(const L& line, const typename line_traits<L>::point_type& point, typename line_traits<L>::point_type& nearest_point_out)
{
    return std::sqrt(distance_to_infinite_squared(line, point, nearest_point_out));
}

/**
 * Returns the distance to the nearest point on the infinite segment.
 *
 * @note The nearest point (and returned squared distance to this point) could be beyond the 'a' and 'b' ends of the segment.
 */
template<class L>
double distance_to_infinite(const L& line, const typename line_traits<L>::point_type& point)
{
    return std::sqrt(distance_to_infinite_squared(line, point));
}

template<class L>
bool intersection(const L& line, const L& other_line, typename line_traits<L>::point_type& intersection_point_out)
{
    using ScalarType = typename line_traits<L>::scalar_type;
    using PointType  = typename line_traits<L>::point_type;
    using Floating   = typename std::conditional<std::is_floating_point<ScalarType>::value, ScalarType, double>::type;
    using VecType    = Domain::Advanced::Vec<Floating, PointType::RowsAtCompileTime>;

    const VecType  v1    = (line_traits<L>::get_b(line) - line_traits<L>::get_a(line)).template cast<Floating>();
    const VecType  v2    = (line_traits<L>::get_b(other_line) - line_traits<L>::get_a(other_line)).template cast<Floating>();
    const Floating denom = Slic3r::cross2(v1, v2);
    if (std::fabs(denom) < Slic3r::Domain::EPSILON)
        return false; // Lines are collinear.

    const VecType  v12    = (line_traits<L>::get_a(line) - line_traits<L>::get_a(other_line)).template cast<Floating>();
    const Floating nume_a = Slic3r::cross2(v2, v12);
    const Floating nume_b = Slic3r::cross2(v1, v12);
    const Floating t1     = nume_a / denom;
    const Floating t2     = nume_b / denom;
    if (t1 >= 0 && t1 <= 1.0f && t2 >= 0 && t2 <= 1.0f) {
        // Get the intersection point.
        intersection_point_out = (line_traits<L>::get_a(line).template cast<Floating>() + t1 * v1).template cast<ScalarType>();
        return true;
    }

    return false; // not intersecting
}

} // namespace Impl


// Explicit specialization of the Impl::line_traits for the Domain::Line type
template<>
struct Impl::line_traits<Domain::Line>
{
    using line_type = Domain::Line;
    using point_type = Domain::Point;
    using scalar_type = Domain::Point::Scalar;

    static Domain::Point get_a(const Domain::Line& line) { return line.a; }

    static Domain::Point get_b(const Domain::Line& line) { return line.b; }
};


double distance_to_squared(const Domain::Line& line, const Domain::Point& point, Domain::Point& nearest_point_out)
{
    return Impl::distance_to_squared<Domain::Line>(line, point, nearest_point_out);
}

double distance_to_squared(const Domain::Line& line, const Domain::Point& point)
{
    return Impl::distance_to_squared<Domain::Line>(line, point);
}

double distance_to(const Domain::Line& line, const Domain::Point& point)
{
    return Impl::distance_to<Domain::Line>(line, point);
}

double distance_to_infinite_squared(const Domain::Line& line, const Domain::Point& point, Domain::Point& nearest_point_out)
{
    return Impl::distance_to_infinite_squared<Domain::Line>(line, point, nearest_point_out);
}

double distance_to_infinite_squared(const Domain::Line& line, const Domain::Point& point)
{
    return Impl::distance_to_infinite_squared<Domain::Line>(line, point);
}

double distance_to_infinite(const Domain::Line& line, const Domain::Point& point, Domain::Point& nearest_point_out)
{
    return Impl::distance_to_infinite<Domain::Line>(line, point, nearest_point_out);
}

double distance_to_infinite(const Domain::Line& line, const Domain::Point& point)
{
    return Impl::distance_to_infinite<Domain::Line>(line, point);
}

bool intersection(const Domain::Line& line, const Domain::Line& other_line, Domain::Point& intersection_point_out)
{
    return Impl::intersection<Domain::Line>(line, other_line, intersection_point_out);
}

bool intersection_infinite(const Domain::Line& line, const Domain::Line& other_line, Domain::Point& intersection_point_out)
{
    using namespace Slic3r::Domain;

    const Vec2d  a1    = line.a.cast<double>();
    const Vec2d  v12   = (other_line.a - line.a).cast<double>();
    const Vec2d  v1    = (line.b - line.a).cast<double>();
    const Vec2d  v2    = (other_line.b - other_line.a).cast<double>();
    const double denom = Slic3r::cross2(v1, v2);
    if (std::fabs(denom) < EPSILON)
        return false;

    const double t1     = Slic3r::cross2(v12, v2) / denom;
    const Vec2d  result = (a1 + t1 * v1);
    if (result.x() > std::numeric_limits<coord_t>::max() || result.x() < std::numeric_limits<coord_t>::lowest() ||
        result.y() > std::numeric_limits<coord_t>::max() || result.y() < std::numeric_limits<coord_t>::lowest()) {
        // Intersection has at least one of the coordinates much bigger (or smaller) than coord_t maximum value (or minimum).
        // So it can not be stored into the Point without integer overflows. That could mean that input lines are parallel or near parallel.
        return false;
    }

    intersection_point_out = result.cast<coord_t>();
    return true;
}

Domain::BoundingBox2crd get_extents(const Domain::Lines& lines)
{
    Domain::BoundingBox2crd bbox;
    for (const Domain::Line& line : lines) {
        bbox = BoundingBox::merge(bbox, line.a);
        bbox = BoundingBox::merge(bbox, line.b);
    }

    return bbox;
}

Domain::Vec3d intersect_plane(const Domain::Line3d& line, const double z)
{
    const Domain::Vec3d v = line.vector();
    const double        t = (z - line.a.z()) / v.z();
    return {line.a.x() + v.x() * t, line.a.y() + v.y() * t, z};
}

Domain::Line3d transformed(const Domain::Line3d& line, const Domain::Transform3d& t)
{
    using LineInMatrixForm = Eigen::Matrix<double, 3, 2>;

    LineInMatrixForm world_line;
    ::memcpy((void*) world_line.col(0).data(), (const void*) line.a.data(), 3 * sizeof(double));
    ::memcpy((void*) world_line.col(1).data(), (const void*) line.b.data(), 3 * sizeof(double));

    LineInMatrixForm local_line = t * world_line.colwise().homogeneous();

    return Domain::Line3d(
        Domain::Vec3d(local_line(0, 0), local_line(1, 0), local_line(2, 0)),
        Domain::Vec3d(local_line(0, 1), local_line(1, 1), local_line(2, 1))
    );
}

} // namespace Slic3r::Biz::Algorithms::Line
