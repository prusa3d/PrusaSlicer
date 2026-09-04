#include "Point.hpp"

#include <algorithm>
#include <cstring>

#include "libslic3r/libslic3r.h"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

namespace Slic3r {

namespace BB = Biz::Algorithms::BoundingBox;

std::vector<Vec3f> transform(const std::vector<Vec3f>& points, const Transform3f& t)
{
    unsigned int vertices_count = (unsigned int)points.size();
    if (vertices_count == 0)
        return std::vector<Vec3f>();

    unsigned int data_size = 3 * vertices_count * sizeof(float);

    Eigen::MatrixXf src(3, vertices_count);
    ::memcpy((void*)src.data(), (const void*)points.data(), data_size);

    Eigen::MatrixXf dst(3, vertices_count);
    dst = t * src.colwise().homogeneous();

    std::vector<Vec3f> ret_points(vertices_count, Vec3f::Zero());
    ::memcpy((void*)ret_points.data(), (const void*)dst.data(), data_size);
    return ret_points;
}

Pointf3s transform(const Pointf3s& points, const Transform3d& t)
{
    unsigned int vertices_count = (unsigned int)points.size();
    if (vertices_count == 0)
        return Pointf3s();

    unsigned int data_size = 3 * vertices_count * sizeof(double);

    Eigen::MatrixXd src(3, vertices_count);
    ::memcpy((void*)src.data(), (const void*)points.data(), data_size);

    Eigen::MatrixXd dst(3, vertices_count);
    dst = t * src.colwise().homogeneous();

    Pointf3s ret_points(vertices_count, Vec3d::Zero());
    ::memcpy((void*)ret_points.data(), (const void*)dst.data(), data_size);
    return ret_points;
}

bool has_duplicate_points(Points &&pts)
{
    std::sort(pts.begin(), pts.end());
    for (size_t i = 1; i < pts.size(); ++ i)
        if (pts[i - 1] == pts[i])
            return true;
    return false;
}


template<bool IncludeBoundary>
BoundingBox get_extents(const Points &pts)
{
    using Biz::Algorithms::BoundingBox::construct;
    const auto bbox{construct(pts.begin(), pts.end())};
    BoundingBox out{bbox.min, bbox.max};
    if constexpr (IncludeBoundary) {
        out.defined = true;
    }
    return out;
}
template BoundingBox get_extents<false>(const Points &pts);
template BoundingBox get_extents<true>(const Points &pts);

// if IncludeBoundary, then a bounding box is defined even for a single point.
// otherwise a bounding box is only defined if it has a positive area.
template<bool IncludeBoundary>
BoundingBox get_extents(const VecOfPoints &pts)
{
    BoundingBox bbox;
    for (const Points &p : pts)
        bbox = BB::merge(bbox, get_extents<IncludeBoundary>(p));
    return bbox;
}
template BoundingBox get_extents<false>(const VecOfPoints &pts);
template BoundingBox get_extents<true>(const VecOfPoints &pts);

BoundingBoxf get_extents(const std::vector<Vec2d> &pts)
{
    BoundingBoxf bbox;
    for (const Vec2d &p : pts)
        bbox = BB::merge(bbox, p);
    return bbox;
}

int nearest_point_index(const Points &points, const Point &pt)
{
    int64_t distance = std::numeric_limits<int64_t>::max();
    int     idx      = -1;

    for (const Point &pt2 : points) {
        // If the X distance of the candidate is > than the total distance of the
        // best previous candidate, we know we don't want it.
        int64_t d = sqr<int64_t>(pt2.x() - pt.x());
        if (d < distance) {
            // If the Y distance of the candidate is > than the total distance of the
            // best previous candidate, we know we don't want it.
            d += sqr<int64_t>(pt2.y() - pt.y());
            if (d < distance) {
                idx      = &pt2 - points.data();
                distance = d;
            }
        }
    }

    return idx;
}

std::ostream& operator<<(std::ostream &stm, const Vec2d &pointf)
{
    return stm << pointf(0) << "," << pointf(1);
}

} // namespace Slic3r
