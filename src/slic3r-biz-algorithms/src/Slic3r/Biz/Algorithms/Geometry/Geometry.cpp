#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/constants.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>
#include <vector>
#include <array>
#include <complex>

#include <LocalesUtils.hpp>

#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/DouglasPeucker.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/Geometry.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/MultiPoint.hpp"
#include "Slic3r/Domain/Polygon.hpp"

namespace boost {
namespace polygon {
template <typename T> class point_data;
}  // namespace polygon
}  // namespace boost

#if defined(_MSC_VER) && defined(__clang__)
#define BOOST_NO_CXX17_HDR_STRING_VIEW
#endif

using namespace Slic3r::Biz;

namespace Slic3r::Biz::Algorithms::Geometry {

using Domain::EPSILON;
using Domain::Point;
using Domain::Polygon;
using Domain::Polygons;
using Domain::ExPolygons;
using Domain::Points;
using Domain::Vec2d;
using Domain::Vec3d;
using Domain::Vec2ds;
using Domain::BoundingBox2d;
using Domain::Transform3d;
using Domain::SquareMatrix3d;
using Domain::Transformation;

Orientation orient(
    const Domain::Point& a, const Domain::Point& b, const Domain::Point& c
)
{
    static_assert(
        sizeof(Domain::coord_t) * 2 == sizeof(int64_t), "orient works with 32 bit coordinates"
    );
    int64_t u = int64_t(b.x()) * int64_t(c.y()) - int64_t(b.y()) * int64_t(c.x());
    int64_t v = int64_t(a.x()) * int64_t(c.y()) - int64_t(a.y()) * int64_t(c.x());
    int64_t w = int64_t(a.x()) * int64_t(b.y()) - int64_t(a.y()) * int64_t(b.x());
    int64_t d = u - v + w;
    return (d > 0) ? ORIENTATION_CCW : ((d == 0) ? ORIENTATION_COLINEAR : ORIENTATION_CW);
}

// Return orientation of the polygon by checking orientation of the left bottom corner of the polygon
// using exact arithmetics. The input polygon must not contain duplicate points
// (or at least the left bottom corner point must not have duplicates).
bool is_ccw(const Domain::Polygon& poly)
{
    // The polygon shall be at least a triangle.
    assert(poly.points.size() >= 3);
    if (poly.points.size() < 3)
        return true;

    // 1) Find the lowest lexicographical point.
    unsigned int imin = 0;
    for (unsigned int i = 1; i < poly.points.size(); ++i) {
        const Domain::Point& pmin = poly.points[imin];
        const Domain::Point& p = poly.points[i];
        if (p(0) < pmin(0) || (p(0) == pmin(0) && p(1) < pmin(1)))
            imin = i;
    }

    // 2) Detect the orientation of the corner imin.
    size_t iPrev = ((imin == 0) ? poly.points.size() : imin) - 1;
    size_t iNext = ((imin + 1 == poly.points.size()) ? 0 : imin + 1);
    Orientation o = orient(poly.points[iPrev], poly.points[imin], poly.points[iNext]);
    // The lowest bottom point must not be collinear if the polygon does not contain duplicate
    // points or overlapping segments.
    assert(o != ORIENTATION_COLINEAR);
    return o == ORIENTATION_CCW;
}

bool ray_ray_intersection(
    const Domain::Vec2d& p1,
    const Domain::Vec2d& v1,
    const Domain::Vec2d& p2,
    const Domain::Vec2d& v2,
    Domain::Vec2d& res
)
{
    double denom = v1(0) * v2(1) - v2(0) * v1(1);
    if (std::abs(denom) < Domain::EPSILON)
        return false;
    double t = (v2(0) * (p1(1) - p2(1)) - v2(1) * (p1(0) - p2(0))) / denom;
    res(0) = p1(0) + t * v1(0);
    res(1) = p1(1) + t * v1(1);
    return true;
}

bool segment_segment_intersection(
    const Domain::Vec2d& p1,
    const Domain::Vec2d& v1,
    const Domain::Vec2d& p2,
    const Domain::Vec2d& v2,
    Domain::Vec2d& res
)
{
    double denom = v1(0) * v2(1) - v2(0) * v1(1);
    if (std::abs(denom) < Domain::EPSILON)
        // Lines are collinear.
        return false;
    double s12_x = p1(0) - p2(0);
    double s12_y = p1(1) - p2(1);
    double s_numer = v1(0) * s12_y - v1(1) * s12_x;
    bool denom_is_positive = false;
    if (denom < 0.) {
        denom_is_positive = true;
        denom = -denom;
        s_numer = -s_numer;
    }
    if (s_numer < 0.)
        // Intersection outside of the 1st segment.
        return false;
    double t_numer = v2(0) * s12_y - v2(1) * s12_x;
    if (!denom_is_positive)
        t_numer = -t_numer;
    if (t_numer < 0. || s_numer > denom || t_numer > denom)
        // Intersection outside of the 1st or 2nd segment.
        return false;
    // Intersection inside both of the segments.
    double t = t_numer / denom;
    res(0) = p1(0) + t * v1(0);
    res(1) = p1(1) + t * v1(1);
    return true;
}

bool segments_intersect(
    const Domain::Point& ip1,
    const Domain::Point& ip2,
    const Domain::Point& jp1,
    const Domain::Point& jp2
)
{
    assert(ip1 != ip2);
    assert(jp1 != jp2);

    auto segments_could_intersect =
        [](const Domain::Point& ip1, const Domain::Point& ip2, const Domain::Point& jp1,
           const Domain::Point& jp2) -> std::pair<int, int> {
        Domain::Vec2big iv = (ip2 - ip1).cast<int64_t>();
        Domain::Vec2big vij1 = (jp1 - ip1).cast<int64_t>();
        Domain::Vec2big vij2 = (jp2 - ip1).cast<int64_t>();
        int64_t tij1 = cross2(iv, vij1);
        int64_t tij2 = cross2(iv, vij2);
        return std::make_pair(
            // signum
            (tij1 > 0) ? 1 : ((tij1 < 0) ? -1 : 0), (tij2 > 0) ? 1 : ((tij2 < 0) ? -1 : 0)
        );
    };

    std::pair<int, int> sign1 = segments_could_intersect(ip1, ip2, jp1, jp2);
    std::pair<int, int> sign2 = segments_could_intersect(jp1, jp2, ip1, ip2);
    int test1 = sign1.first * sign1.second;
    int test2 = sign2.first * sign2.second;
    if (test1 <= 0 && test2 <= 0) {
        // The segments possibly intersect. They may also be collinear, but not intersect.
        if (test1 != 0 || test2 != 0)
            // Certainly not collinear, then the segments intersect.
            return true;
        // If the first segment is collinear with the other, the other is collinear with the first segment.
        assert((sign1.first == 0 && sign1.second == 0) == (sign2.first == 0 && sign2.second == 0));
        if (sign1.first == 0 && sign1.second == 0) {
            // The segments are certainly collinear. Now verify whether they overlap.
            Domain::Point vi = ip2 - ip1;
            // Project both on the longer coordinate of vi.
            int axis = std::abs(vi.x()) > std::abs(vi.y()) ? 0 : 1;
            Domain::coord_t i = ip1(axis);
            Domain::coord_t j = ip2(axis);
            Domain::coord_t k = jp1(axis);
            Domain::coord_t l = jp2(axis);
            if (i > j)
                std::swap(i, j);
            if (k > l)
                std::swap(k, l);
            return (k >= i && k <= j) || (i >= k && i <= l);
        }
    }
    return false;
}

template<typename T>
T foot_pt(const T& line_pt, const T& line_dir, const T& pt)
{
    T v = pt - line_pt;
    auto l2 = line_dir.squaredNorm();
    auto t = (l2 == 0) ? 0 : v.dot(line_dir) / l2;
    return line_pt + line_dir * t;
}

Domain::Vec2d foot_pt(const Vec2d& line_pt, const Vec2d& line_dir, const Vec2d& pt)
{
    return foot_pt<Domain::Vec2d>(
        line_pt, line_dir, pt
    );
}

Domain::Vec2d foot_pt(const Domain::Line& iline, const Domain::Point& ipt)
{
    return foot_pt<Domain::Vec2d>(
        iline.a.cast<double>(), (iline.b - iline.a).cast<double>(), ipt.cast<double>()
    );
}

template<typename T>
auto ray_point_distance_squared(const T& ray_pt, const T& ray_dir, const T& pt)
{
    return (foot_pt(ray_pt, ray_dir, pt) - pt).squaredNorm();
}

template<typename T>
auto ray_point_distance(const T& ray_pt, const T& ray_dir, const T& pt)
{
    return (foot_pt(ray_pt, ray_dir, pt) - pt).norm();
}

double ray_point_distance_squared(const Domain::Line& iline, const Domain::Point& ipt)
{
    return (foot_pt(iline, ipt) - ipt.cast<double>()).squaredNorm();
}

double ray_point_distance(const Domain::Line& iline, const Domain::Point& ipt)
{
    return (foot_pt(iline, ipt) - ipt.cast<double>()).norm();
}

double ray_point_distance(const Vec2d& ray_pt, const Vec2d& ray_dir, const Vec2d& pt)
{
    return ray_point_distance<Vec2d>(ray_pt, ray_dir, pt);
}

bool directions_parallel(double angle1, double angle2, double max_diff)
{
    double diff = fabs(angle1 - angle2);
    max_diff += EPSILON;
    return diff < max_diff || fabs(diff - PI) < max_diff;
}

bool directions_perpendicular(double angle1, double angle2, double max_diff)
{
    double diff = fabs(angle1 - angle2);
    max_diff += EPSILON;
    return fabs(diff - 0.5 * PI) < max_diff || fabs(diff - 1.5 * PI) < max_diff;
}

template<class T>
bool contains(const std::vector<T> &vector, const Point &point)
{
    for (typename std::vector<T>::const_iterator it = vector.begin(); it != vector.end(); ++it) {
        if (Algorithms::ExPolygon::contains(*it, point)) return true;
    }
    return false;
}
template bool contains(const ExPolygons &vector, const Point &point);

void simplify_polygons(const Polygons &polygons, double tolerance, Polygons* retval)
{
    using Algorithms::Polygon::to_polyline;
    using Algorithms::DouglasPeucker::douglas_peucker;
    Polygons simplified_raw;
    for (const Polygon &source_polygon : polygons) {
        Points simplified = douglas_peucker(to_polyline(source_polygon).points, tolerance);
        if (simplified.size() > 3) {
            simplified.pop_back();
            simplified_raw.push_back(Polygon{ std::move(simplified) });
        }
    }
    using Slic3r::Biz::Algorithms::ClipperUtils::simplify_polygons;
    *retval = simplify_polygons(simplified_raw);
}

double linint(double value, double oldmin, double oldmax, double newmin, double newmax)
{
    return (value - oldmin) * (newmax - newmin) / (oldmax - oldmin) + newmin;
}

#if 0
// Point with a weight, by which the points are sorted.
// If the points have the same weight, sort them lexicographically by their positions.
struct ArrangeItem {
    ArrangeItem() {}
    Vec2d    pos;
    double  weight;
    bool operator<(const ArrangeItem &other) const {
        return weight < other.weight ||
            ((weight == other.weight) && (pos(1) < other.pos(1) || (pos(1) == other.pos(1) && pos(0) < other.pos(0))));
    }
};

Pointfs arrange(size_t num_parts, const Vec2d &part_size, double gap, const BoundingBoxf* bed_bounding_box)
{
    // Use actual part size (the largest) plus separation distance (half on each side) in spacing algorithm.
    const Vec2d       cell_size(part_size(0) + gap, part_size(1) + gap);

    const BoundingBoxf bed_bbox = (bed_bounding_box != NULL && bed_bounding_box->defined) ? 
        *bed_bounding_box :
        // Bogus bed size, large enough not to trigger the unsufficient bed size error.
        BoundingBoxf(
            Vec2d(0, 0),
            Vec2d(cell_size(0) * num_parts, cell_size(1) * num_parts));

    // This is how many cells we have available into which to put parts.
    size_t cellw = size_t(floor((bed_bbox.size()(0) + gap) / cell_size(0)));
    size_t cellh = size_t(floor((bed_bbox.size()(1) + gap) / cell_size(1)));
    if (num_parts > cellw * cellh)
        throw Slic3r::InvalidArgument("%zu parts won't fit in your print area!\n", num_parts);
    
    // Get a bounding box of cellw x cellh cells, centered at the center of the bed.
    Vec2d       cells_size(cellw * cell_size(0) - gap, cellh * cell_size(1) - gap);
    Vec2d       cells_offset(bed_bbox.center() - 0.5 * cells_size);
    BoundingBoxf cells_bb(cells_offset, cells_size + cells_offset);
    
    // List of cells, sorted by distance from center.
    std::vector<ArrangeItem> cellsorder(cellw * cellh, ArrangeItem());
    for (size_t j = 0; j < cellh; ++ j) {
        // Center of the jth row on the bed.
        double cy = linint(j + 0.5, 0., double(cellh), cells_bb.min(1), cells_bb.max(1));
        // Offset from the bed center.
        double yd = cells_bb.center()(1) - cy;
        for (size_t i = 0; i < cellw; ++ i) {
            // Center of the ith column on the bed.
            double cx = linint(i + 0.5, 0., double(cellw), cells_bb.min(0), cells_bb.max(0));
            // Offset from the bed center.
            double xd = cells_bb.center()(0) - cx;
            // Cell with a distance from the bed center.
            ArrangeItem &ci = cellsorder[j * cellw + i];
            // Cell center
            ci.pos(0) = cx;
            ci.pos(1) = cy;
            // Square distance of the cell center to the bed center.
            ci.weight = xd * xd + yd * yd;
        }
    }
    // Sort the cells lexicographically by their distances to the bed center and left to right / bttom to top.
    std::sort(cellsorder.begin(), cellsorder.end());
    cellsorder.erase(cellsorder.begin() + num_parts, cellsorder.end());

    // Return the (left,top) corners of the cells.
    Pointfs positions;
    positions.reserve(num_parts);
    for (std::vector<ArrangeItem>::const_iterator it = cellsorder.begin(); it != cellsorder.end(); ++ it)
        positions.push_back(Vec2d(it->pos(0) - 0.5 * part_size(0), it->pos(1) - 0.5 * part_size(1)));
    return positions;
}
#else
class ArrangeItem {
public:
    Vec2d pos = Vec2d::Zero();
    size_t index_x, index_y;
    double dist;
};
class ArrangeItemIndex {
public:
    double index;
    ArrangeItem item;
    ArrangeItemIndex(double _index, ArrangeItem _item) : index(_index), item(_item) {};
};

bool
arrange(size_t total_parts, const Vec2d &part_size, double dist, const BoundingBox2d* bb, Vec2ds &positions)
{
    namespace bounding_box = Slic3r::Biz::Algorithms::BoundingBox;
    positions.clear();

    Vec2d part = part_size;

    // use actual part size (the largest) plus separation distance (half on each side) in spacing algorithm
    part(0) += dist;
    part(1) += dist;
    
    Vec2d area(Vec2d::Zero());
    if (bb != NULL && bb->defined) {
        area = bounding_box::sizes(*bb);
    } else {
        // bogus area size, large enough not to trigger the error below
        area(0) = part(0) * total_parts;
        area(1) = part(1) * total_parts;
    }
    
    // this is how many cells we have available into which to put parts
    size_t cellw = floor((area(0) + dist) / part(0));
    size_t cellh = floor((area(1) + dist) / part(1));
    if (total_parts > (cellw * cellh))
        return false;
    
    // total space used by cells
    Vec2d cells(cellw * part(0), cellh * part(1));
    
    // bounding box of total space used by cells
    BoundingBox2d cells_bb;
    cells_bb = bounding_box::merge(cells_bb, Vec2d(0,0)); // min
    cells_bb = bounding_box::merge(cells_bb, cells);  // max
    
    // center bounding box to area
    cells_bb = bounding_box::translated(cells_bb, Vec2d{
        (area(0) - cells(0)) / 2,
        (area(1) - cells(1)) / 2
    });
    
    // list of cells, sorted by distance from center
    std::vector<ArrangeItemIndex> cellsorder;
    
    // work out distance for all cells, sort into list
    for (size_t i = 0; i <= cellw-1; ++i) {
        for (size_t j = 0; j <= cellh-1; ++j) {
            double cx = linint(i + 0.5, 0, cellw, cells_bb.min(0), cells_bb.max(0));
            double cy = linint(j + 0.5, 0, cellh, cells_bb.min(1), cells_bb.max(1));
            
            double xd = fabs((area(0) / 2) - cx);
            double yd = fabs((area(1) / 2) - cy);
            
            ArrangeItem c;
            c.pos(0) = cx;
            c.pos(1) = cy;
            c.index_x = i;
            c.index_y = j;
            c.dist = xd * xd + yd * yd - fabs((cellw / 2) - (i + 0.5));
            
            // binary insertion sort
            {
                double index = c.dist;
                size_t low = 0;
                size_t high = cellsorder.size();
                while (low < high) {
                    size_t mid = (low + ((high - low) / 2)) | 0;
                    double midval = cellsorder[mid].index;
                    
                    if (midval < index) {
                        low = mid + 1;
                    } else if (midval > index) {
                        high = mid;
                    } else {
                        cellsorder.insert(cellsorder.begin() + mid, ArrangeItemIndex(index, c));
                        goto ENDSORT;
                    }
                }
                cellsorder.insert(cellsorder.begin() + low, ArrangeItemIndex(index, c));
            }
            ENDSORT: ;
        }
    }
    
    // the extents of cells actually used by objects
    double lx = 0;
    double ty = 0;
    double rx = 0;
    double by = 0;

    // now find cells actually used by objects, map out the extents so we can position correctly
    for (size_t i = 1; i <= total_parts; ++i) {
        ArrangeItemIndex c = cellsorder[i - 1];
        double cx = c.item.index_x;
        double cy = c.item.index_y;
        if (i == 1) {
            lx = rx = cx;
            ty = by = cy;
        } else {
            if (cx > rx) rx = cx;
            if (cx < lx) lx = cx;
            if (cy > by) by = cy;
            if (cy < ty) ty = cy;
        }
    }
    // now we actually place objects into cells, positioned such that the left and bottom borders are at 0
    for (size_t i = 1; i <= total_parts; ++i) {
        ArrangeItemIndex c = cellsorder.front();
        cellsorder.erase(cellsorder.begin());
        double cx = c.item.index_x - lx;
        double cy = c.item.index_y - ty;
        
        positions.push_back(Vec2d(cx * part(0), cy * part(1)));
    }
    
    if (bb != NULL && bb->defined) {
        for (auto p = positions.begin(); p != positions.end(); ++p) {
            p->x() += bb->min(0);
            p->y() += bb->min(1);
        }
    }
    
    return true;
}
#endif

// Euclidian distance of two boost::polygon points.
template<typename T>
T dist(const boost::polygon::point_data<T> &p1,const boost::polygon::point_data<T> &p2)
{
	T dx = p2(0) - p1(0);
	T dy = p2(1) - p1(1);
	return sqrt(dx*dx+dy*dy);
}

// Find a foot point of "px" on a segment "seg".
template<typename segment_type, typename point_type>
inline point_type project_point_to_segment(segment_type &seg, point_type &px)
{
    typedef typename point_type::coordinate_type T;
    const point_type &p0 = low(seg);
    const point_type &p1 = high(seg);
    const point_type  dir(p1(0)-p0(0), p1(1)-p0(1));
    const point_type  dproj(px(0)-p0(0), px(1)-p0(1));
    const T           t = (dir(0)*dproj(0) + dir(1)*dproj(1)) / (dir(0)*dir(0) + dir(1)*dir(1));
    assert(t >= T(-1e-6) && t <= T(1. + 1e-6));
    return point_type(p0(0) + t*dir(0), p0(1) + t*dir(1));
}

void assemble_transform(Transform3d& transform, const Vec3d& translation, const Vec3d& rotation, const Vec3d& scale, const Vec3d& mirror)
{
    transform = Transform3d::Identity();
    transform.translate(translation);
    transform.rotate(Eigen::AngleAxisd(rotation(2), Vec3d::UnitZ()) * Eigen::AngleAxisd(rotation(1), Vec3d::UnitY()) * Eigen::AngleAxisd(rotation(0), Vec3d::UnitX()));
    transform.scale(scale.cwiseProduct(mirror));
}

Transform3d assemble_transform(const Vec3d& translation, const Vec3d& rotation, const Vec3d& scale, const Vec3d& mirror)
{
    Transform3d transform;
    assemble_transform(transform, translation, rotation, scale, mirror);
    return transform;
}

void assemble_transform(Transform3d& transform, const Transform3d& translation, const Transform3d& rotation, const Transform3d& scale, const Transform3d& mirror)
{
    transform = translation * rotation * scale * mirror;
}

Transform3d assemble_transform(const Transform3d& translation, const Transform3d& rotation, const Transform3d& scale, const Transform3d& mirror)
{
    Transform3d transform;
    assemble_transform(transform, translation, rotation, scale, mirror);
    return transform;
}

// For parsing a transformation matrix from 3MF / AMF.
Transform3d transform3d_from_string(const std::string& transform_str)
{
    assert(is_decimal_separator_point()); // for atof
    Transform3d transform = Transform3d::Identity();

    if (!transform_str.empty()) {
        std::vector<std::string> mat_elements_str;
        boost::split(mat_elements_str, transform_str, boost::is_any_of(" "), boost::token_compress_on);

        const unsigned int size = (unsigned int)mat_elements_str.size();
        if (size == 16) {
            unsigned int i = 0;
            for (unsigned int r = 0; r < 4; ++r) {
                for (unsigned int c = 0; c < 4; ++c) {
                    transform(r, c) = ::atof(mat_elements_str[i++].c_str());
                }
            }
        }
    }

    return transform;
}

Eigen::Quaterniond rotation_xyz_diff(const Vec3d &rot_xyz_from, const Vec3d &rot_xyz_to)
{
    return
        // From the current coordinate system to world.
        Eigen::AngleAxisd(rot_xyz_to.z(), Vec3d::UnitZ()) * Eigen::AngleAxisd(rot_xyz_to.y(), Vec3d::UnitY()) * Eigen::AngleAxisd(rot_xyz_to.x(), Vec3d::UnitX()) *
        // From world to the initial coordinate system.
        Eigen::AngleAxisd(-rot_xyz_from.x(), Vec3d::UnitX()) * Eigen::AngleAxisd(-rot_xyz_from.y(), Vec3d::UnitY()) * Eigen::AngleAxisd(-rot_xyz_from.z(), Vec3d::UnitZ());
}

// This should only be called if it is known, that the two rotations only differ in rotation around the Z axis.
double rotation_diff_z(const Transform3d &trafo_from, const Transform3d &trafo_to)
{
    auto  m  = trafo_to.linear() * trafo_from.linear().inverse();
    assert(std::abs(m.determinant() - 1) < EPSILON);
    Vec3d vx = m * Vec3d(1., 0., 0);
    // Verify that the linear part of rotation from trafo_from to trafo_to rotates around Z and is unity.
    assert(std::abs(std::hypot(vx.x(), vx.y()) - 1.) < 1e-5);
    assert(std::abs(vx.z()) < 1e-5);
    return atan2(vx.y(), vx.x());
}

bool is_rotation_ninety_degrees(double a)
{
    a = fmod(std::abs(a), 0.5 * PI);
    if (a > 0.25 * PI)
        a = 0.5 * PI - a;
    return a < 0.001;
}

bool is_rotation_ninety_degrees(const Domain::Vec3d& rotation)
{
    return is_rotation_ninety_degrees(rotation.x()) && is_rotation_ninety_degrees(rotation.y()) &&
        is_rotation_ninety_degrees(rotation.z());
}

bool trafos_differ_in_rotation_by_z_and_mirroring_by_xy_only(const Transform3d &t1, const Transform3d &t2)
{
    if (std::abs(t1.translation().z() - t2.translation().z()) > EPSILON)
        // One of the object is higher than the other above the build plate (or below the build plate).
        return false;
    SquareMatrix3d m1 = t1.matrix().block<3, 3>(0, 0);
    SquareMatrix3d m2 = t2.matrix().block<3, 3>(0, 0);
    SquareMatrix3d m = m2.inverse() * m1;
    Vec3d    z = m.block<3, 1>(0, 2);
    if (std::abs(z.x()) > EPSILON || std::abs(z.y()) > EPSILON || std::abs(z.z() - 1.) > EPSILON)
        // Z direction or length changed.
        return false;
    // Z still points in the same direction and it has the same length.
    Vec3d    x = m.block<3, 1>(0, 0);
    Vec3d    y = m.block<3, 1>(0, 1);
    if (std::abs(x.z()) > EPSILON || std::abs(y.z()) > EPSILON)
        return false;
    double   lx2 = x.squaredNorm();
    double   ly2 = y.squaredNorm();
    if (lx2 - 1. > EPSILON * EPSILON || ly2 - 1. > EPSILON * EPSILON)
        return false;
    // Verify whether the vectors x, y are still perpendicular.
    double   d   = x.dot(y);
    return std::abs(d * d) < EPSILON * lx2 * ly2;
}

bool trafos_differ_in_rotation_by_z_and_mirroring_by_xy_only(
    const Transformation& t1, const Transformation& t2
)
{
    return trafos_differ_in_rotation_by_z_and_mirroring_by_xy_only(t1.get_matrix(), t2.get_matrix());
}

bool is_point_inside_polygon_corner(const Point &a, const Point &b, const Point &c, const Point &query_point) {
    // Cast all input points into int64_t to prevent overflows when points are close to max values of coord_t.
    const Domain::Vec2big a_i64           = a.cast<int64_t>();
    const Domain::Vec2big b_i64           = b.cast<int64_t>();
    const Domain::Vec2big c_i64           = c.cast<int64_t>();
    const Domain::Vec2big query_point_i64 = query_point.cast<int64_t>();

    // Shift all points to have a base in vertex B.
    // Then construct normalized vectors to ensure that we will work with vectors with endpoints on the unit circle.
    const Vec2d ba = (a_i64 - b_i64).cast<double>().normalized();
    const Vec2d bc = (c_i64 - b_i64).cast<double>().normalized();
    const Vec2d bq = (query_point_i64 - b_i64).cast<double>().normalized();

    // Points A and C has to be different.
    assert(ba != bc);

    // Construct a normal for the vector BQ that points to the left side of the vector BQ.
    const Vec2d bq_left_normal = perp(bq);

    const double proj_a_on_bq_normal = ba.dot(bq_left_normal); // Project point A on the normal of BQ.
    const double proj_c_on_bq_normal = bc.dot(bq_left_normal); // Project point C on the normal of BQ.
    if ((proj_a_on_bq_normal > 0. && proj_c_on_bq_normal <= 0.) || (proj_a_on_bq_normal <= 0. && proj_c_on_bq_normal > 0.)) {
        // Q is between points A and C or lies on one of those vectors (BA or BC).

        // Based on the CCW order of polygons (contours) and order of corner ABC,
        // when this condition is met, the query point is inside the corner.
        return proj_a_on_bq_normal > 0.;
    } else {
        // Q isn't between points A and C, but still it can be inside the corner.

        const double proj_a_on_bq = ba.dot(bq); // Project point A on BQ.
        const double proj_c_on_bq = bc.dot(bq); // Project point C on BQ.

        // The value of proj_a_on_bq_normal is the same when we project the vector BA on the normal of BQ.
        // So we can say that the Q is on the right side of the vector BA when proj_a_on_bq_normal > 0, and
        // that the Q is on the left side of the vector BA proj_a_on_bq_normal < 0.
        // Also, the Q is on the right side of the bisector of oriented angle ABC when proj_c_on_bq < proj_a_on_bq, and
        // the Q is on the left side of the bisector of oriented angle ABC when proj_c_on_bq > proj_a_on_bq.

        // So the Q is inside the corner when one of the following conditions is met:
        //  * The Q is on the right side of the vector BA, and the Q is on the right side of the bisector of the oriented angle ABC.
        //  * The Q is on the left side of the vector BA, and the Q is on the left side of the bisector of the oriented angle ABC.
        return (proj_a_on_bq_normal > 0. && proj_c_on_bq < proj_a_on_bq) || (proj_a_on_bq_normal <= 0. && proj_c_on_bq >= proj_a_on_bq);
    }
}

} // namespace Slic3r::Geometry
