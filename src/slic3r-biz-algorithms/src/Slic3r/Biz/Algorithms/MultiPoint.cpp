#include "Slic3r/Biz/Algorithms/MultiPoint.hpp"

#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"

namespace Slic3r::Biz::Algorithms::MultiPoint {

using namespace Slic3r::Biz::Algorithms;

void reverse(Domain::MultiPoint& multi_point)
{
    std::reverse(multi_point.points.begin(), multi_point.points.end());
}

Domain::MultiPoint reversed(const Domain::MultiPoint& multi_point)
{
    Domain::MultiPoint multi_point_out = multi_point;
    reverse(multi_point_out);
    return multi_point_out;
}

int find_point(const Domain::MultiPoint& multi_point, const Domain::Point& query_pt, const double scaled_epsilon)
{
    if (scaled_epsilon == 0.)
        return multi_point.find_point(query_pt);

    double dist2_min = std::numeric_limits<double>::max();
    double eps2      = scaled_epsilon * scaled_epsilon;
    int    idx_min   = -1;
    for (const Domain::Point& pt : multi_point.points) {
        double d2 = (pt - query_pt).cast<double>().squaredNorm();
        if (d2 < dist2_min) {
            idx_min   = int(&pt - &multi_point.points.front());
            dist2_min = d2;
        }
    }

    return dist2_min < eps2 ? idx_min : -1;
}

bool has_consecutive_duplicate_points(const Domain::MultiPoint& multi_point)
{
    return Point::has_consecutive_duplicate_points(multi_point.points);
}

bool remove_consecutive_duplicate_points(Domain::MultiPoint& multi_point)
{
    return Point::remove_consecutive_duplicate_points(multi_point.points);
}

int closest_point_index(const Domain::MultiPoint& multi_point, const Domain::Point& query_pt)
{
    const Domain::Points& points = multi_point.points;

    int idx = -1;
    if (!points.empty()) {
        idx = 0;
        double dist_min = (query_pt - points.front()).cast<double>().norm();
        for (int i = 1; i < int(points.size()); ++i) {
            const double d = (points[i] - query_pt).cast<double>().norm();
            if (d < dist_min) {
                dist_min = d;
                idx      = i;
            }
        }
    }

    return idx;
}

Domain::BoundingBox2crd get_bounding_box(const Domain::MultiPoint& multi_point)
{
    return BoundingBox::construct(multi_point.points);
}

} // namespace Slic3r::Biz::Algorithms::MultiPoint
