#include "LineUtils.hpp"
#include <libslic3r/Geometry.hpp>
#include <libslic3r/Geometry/Circle.hpp>
#include <functional>
#include "VectorUtils.hpp"
#include "PointUtils.hpp"

using namespace Slic3r::sla;

// sort counter clock wise lines
void LineUtils::sort_CCW(Lines &lines, const Point& center)
{
    std::function<double(const Line &)> calc = [&center](const Line &line) {
        Point p = line.a - center;
        return std::atan2(p.y(), p.x());
    };
    VectorUtils::sort_by(lines, calc);
}

bool LineUtils::is_parallel_y(const Line &line) { 
    coord_t x_change = line.a.x() - line.b.x();
    return (x_change == 0);
}
bool LineUtils::is_parallel_y(const Linef &line)
{
    double x_change = line.a.x() - line.b.x();
    return (fabs(x_change) < std::numeric_limits<double>::epsilon());
}

std::optional<Slic3r::Line> LineUtils::crop_ray(const Line & ray,
                                                const Point &center,
                                                double       radius)
{
    if (is_parallel_y(ray)) {
        coord_t x        = ray.a.x();
        coord_t diff     = x - center.x();
        coord_t abs_diff = abs(diff);
        if (abs_diff > radius) return {};
        // create cross points
        double  move_y = sqrt(radius * radius - static_cast<double>(x) * x);
        coord_t y = static_cast<coord_t>(std::round(move_y));
        coord_t cy = center.y();
        Point first(x, cy + y);
        Point second(x,cy - y);
        return Line(first, second);
    } else {
        Line   moved_line(ray.a - center, ray.b - center);
        double a, b, c;
        std::tie(a, b, c) = get_param(moved_line);
        std::pair<Vec2d, Vec2d> points;
        int count = Slic3r::Geometry::ray_circle_intersections(
            radius, a, b, c, points);
        if (count != 2) return {};
        return Line(points.first.cast<coord_t>() + center,
                    points.second.cast<coord_t>() + center);
    }
}
std::optional<Slic3r::Linef> LineUtils::crop_ray(const Linef &ray,
                                                 const Point &center,
                                                 double       radius)
{
    Vec2d center_d = center.cast<double>();
    if (is_parallel_y(ray)) {
        double x        = ray.a.x();
        double diff     = x - center_d.x();
        double abs_diff = fabs(diff);
        if (abs_diff > radius) return {};
        // create cross points
        double  y = sqrt(radius * radius - x * x);
        Vec2d   first(x, y);
        Vec2d   second(x, -y);
        return Linef(first + center_d,
                     second + center_d);
    } else {
        Linef moved_line(ray.a - center_d, ray.b - center_d);
        double a, b, c;
        std::tie(a, b, c) = get_param(moved_line);
        std::pair<Vec2d, Vec2d> points;
        int count = Slic3r::Geometry::ray_circle_intersections(radius, a, b,
                                                               c, points);
        if (count != 2) return {};
        return Linef(points.first + center_d, points.second + center_d);
    }
}

std::optional<Slic3r::Line> LineUtils::crop_half_ray(const Line & half_ray,
                                                     const Point &center,
                                                     double       radius)
{
    std::optional<Line> segment = crop_ray(half_ray, center, radius);
    if (!segment.has_value()) return {};
    Point dir = LineUtils::direction(half_ray);
    using fnc = std::function<bool(const Point &)>;
    fnc use_point_x = [&half_ray, &dir](const Point &p) -> bool {
        return (p.x() > half_ray.a.x()) == (dir.x() > 0);
    };
    fnc use_point_y = [&half_ray, &dir](const Point &p) -> bool {
        return (p.y() > half_ray.a.y()) == (dir.y() > 0);
    };
    bool use_x = PointUtils::is_majorit_x(dir);
    fnc use_point = (use_x) ? use_point_x : use_point_y;
    bool use_a = use_point(segment->a);
    bool use_b = use_point(segment->b);
    if (!use_a && !use_b) return {};
    if (use_a && use_b) return segment;
    return Line(half_ray.a, (use_a)?segment->a : segment->b);
}

std::optional<Slic3r::Linef> LineUtils::crop_half_ray(const Linef & half_ray,
                                                     const Point &center,
                                                     double       radius)
{
    std::optional<Linef> segment = crop_ray(half_ray, center, radius);
    if (!segment.has_value()) return {};
    Vec2d dir       = half_ray.b - half_ray.a;
    using fnc       = std::function<bool(const Vec2d &)>;
    fnc use_point_x = [&half_ray, &dir](const Vec2d &p) -> bool {
        return (p.x() > half_ray.a.x()) == (dir.x() > 0);
    };
    fnc use_point_y = [&half_ray, &dir](const Vec2d &p) -> bool {
        return (p.y() > half_ray.a.y()) == (dir.y() > 0);
    };
    bool use_x = PointUtils::is_majorit_x(dir);
    fnc  use_point = (use_x) ? use_point_x : use_point_y;
    bool use_a     = use_point(segment->a);
    bool use_b     = use_point(segment->b);
    if (!use_a && !use_b) return {};
    if (use_a && use_b) return segment;
    return Linef(half_ray.a, (use_a) ? segment->a : segment->b);
}

std::optional<Slic3r::Line> LineUtils::crop_line(const Line & line,
                                                 const Point &center,
                                                 double       radius)
{
    std::optional<Line> segment = crop_ray(line, center, radius);
    if (!segment.has_value()) return {};

    Point dir       = line.b - line.a;
    using fnc       = std::function<bool(const Point &)>;
    fnc use_point_x = [&line, &dir](const Point &p) -> bool {
        return (dir.x() > 0) ? (p.x() > line.a.x()) && (p.x() < line.b.x()) :
                               (p.x() < line.a.x()) && (p.x() > line.b.x());
    };
    fnc use_point_y = [&line, &dir](const Point &p) -> bool {
        return (dir.y() > 0) ? (p.y() > line.a.y()) && (p.y() < line.b.y()) :
                               (p.y() < line.a.y()) && (p.y() > line.b.y());
    };
    bool use_x = PointUtils::is_majorit_x(dir);
    fnc  use_point = (use_x) ? use_point_x : use_point_y;
    bool use_a     = use_point(segment->a);
    bool use_b     = use_point(segment->b);
    if (!use_a && !use_b) return {};
    if (use_a && use_b) return segment;
    bool same_dir = (use_x) ?
        ((dir.x() > 0) == ((segment->b.x() - segment->a.x()) > 0)) :
        ((dir.y() > 0) == ((segment->b.y() - segment->a.y()) > 0)) ;
    if (use_a) { 
        if (same_dir) 
            return Line(segment->a, line.b);
        else 
            return Line(line.a, segment->a);
    } else { // use b
        if (same_dir)
            return Line(line.a, segment->b);
        else
            return Line(segment->b, line.b);
    }
}

std::optional<Slic3r::Linef> LineUtils::crop_line(const Linef & line,
                                                 const Point &center,
                                                 double       radius)
{
    std::optional<Linef> segment = crop_ray(line, center, radius);
    if (!segment.has_value()) return {};

    Vec2d dir       = line.b - line.a;
    using fnc       = std::function<bool(const Vec2d &)>;
    fnc use_point_x = [&line, &dir](const Vec2d &p) -> bool {
        return (dir.x() > 0) ? (p.x() > line.a.x()) && (p.x() < line.b.x()) :
                               (p.x() < line.a.x()) && (p.x() > line.b.x());
    };
    fnc use_point_y = [&line, &dir](const Vec2d &p) -> bool {
        return (dir.y() > 0) ? (p.y() > line.a.y()) && (p.y() < line.b.y()) :
                               (p.y() < line.a.y()) && (p.y() > line.b.y());
    };
    bool use_x = PointUtils::is_majorit_x(dir);
    fnc  use_point = (use_x) ? use_point_x : use_point_y;
    bool use_a     = use_point(segment->a);
    bool use_b     = use_point(segment->b);
    if (!use_a && !use_b) return {};
    if (use_a && use_b) return segment;
    bool same_dir = (use_x) ? ((dir.x() > 0) ==
                               ((segment->b.x() - segment->a.x()) > 0)) :
                              ((dir.y() > 0) ==
                               ((segment->b.y() - segment->a.y()) > 0));
    if (use_a) {
        if (same_dir)
            return Linef(segment->a, line.b);
        else
            return Linef(line.a, segment->a);
    } else { // use b
        if (same_dir)
            return Linef(line.a, segment->b);
        else
            return Linef(segment->b, line.b);
    }
}


std::tuple<double, double, double> LineUtils::get_param(const Line &line) {
    Vector normal = line.normal();
    double a = normal.x();
    double b = normal.y();
    double c = -a * line.a.x() - b * line.a.y();
    return {a, b, c};
}

std::tuple<double, double, double> LineUtils::get_param(const Linef &line)
{
    Vec2d  direction = line.b - line.a;
    Vec2d  normal(-direction.y(), direction.x());
    double a = normal.x();
    double b = normal.y();
    double c = -a * line.a.x() - b * line.a.y();
    return {a, b, c};
}

void LineUtils::draw(SVG &       svg,
                     const Line &line,
                     const char *color,
                     coordf_t stroke_width,
                     const char *name,
                     bool        side_points,
                     const char *color_a,
                     const char *color_b)
{
    svg.draw(line, color, stroke_width);
    bool use_name = name != nullptr;
    if (use_name) {
        Point middle = line.a/2 + line.b/2;
        svg.draw_text(middle, name, color);
    }
    if (side_points) {
        std::string name_a = (use_name) ? "A" : (std::string("A_") + name);            
        std::string name_b = (use_name) ? "B" : (std::string("B_") + name);
        svg.draw_text(line.a, name_a.c_str(), color_a);
        svg.draw_text(line.b, name_b.c_str(), color_b);
    }
}

double LineUtils::perp_distance(const Linef &line, Vec2d p)
{
    Vec2d v  = line.b - line.a; // direction
    Vec2d va = p - line.a;
    return std::abs(cross2(v, va)) / v.norm();
}

bool LineUtils::is_parallel(const Line &first, const Line &second) 
{
    Vec2i64 dir1 = direction(first).cast<int64_t>();
    Vec2i64 dir2 = direction(second).cast<int64_t>();
    return Slic3r::cross2(dir1, dir2) == 0;
}

std::optional<Slic3r::Vec2d> LineUtils::intersection(const Line &ray1, const Line &ray2)
{
    const Vec2d v1    = direction(ray1).cast<double>();
    const Vec2d v2    = direction(ray2).cast<double>();
    double      denom = cross2(v1, v2);
    if (fabs(denom) < std::numeric_limits<float>::epsilon()) return {};

    const Vec2d v12 = (ray1.a - ray2.a).cast<double>();
    double nume = cross2(v2, v12);
    double t = nume / denom;
    return (ray1.a.cast<double>() + t * v1);
}

bool LineUtils::belongs(const Line &line, const Point &point, double benevolence)
{
    const Point &a = line.a;
    const Point &b = line.b;
    auto is_in_interval = [](coord_t value, coord_t from, coord_t to) -> bool 
    { 
        if (from < to) {
            // from < value < to
            if (from > value || to < value) return false;
        } else {
            // to < value < from
            if (from < value || to > value) return false;
        }
        return true;
    };
                              
    if (!is_in_interval(point.x(), a.x(), b.x()) ||
        !is_in_interval(point.y(), a.y(), b.y()) )
    { // out of interval
        return false;
    }
    double distance = line.perp_distance_to(point);
    if (distance < benevolence) return true;
    return false;
}

Slic3r::Point LineUtils::direction(const Line &line)
{
    return line.b - line.a;
}

Slic3r::Point LineUtils::middle(const Line &line) {
    // division before adding to prevent data type overflow
    return line.a / 2 + line.b / 2;
}

double LineUtils::foot(const Line &line, const Point &point)
{
    Vec2d  a   = line.a.cast<double>();
    Vec2d  vec = point.cast<double>() - a;
    Vec2d  b   = line.b.cast<double>();
    Vec2d  dir = b - a;
    double l2  = dir.squaredNorm();
    return vec.dot(dir) / l2;
}

LineUtils::LineConnection LineUtils::create_line_connection(
    const Slic3r::Lines &lines)
{
    LineConnection line_connection;
    static const size_t bad_index = -1;
    auto insert = [&](size_t line_index, size_t connected, bool connect_by_a){
        auto item = line_connection.find(line_index);
        if (item == line_connection.end()) {
            // create new
            line_connection[line_index] = (connect_by_a) ?
                    std::pair<size_t, size_t>(connected, bad_index) :
                    std::pair<size_t, size_t>(bad_index, connected);
        } else {
            std::pair<size_t, size_t> &pair = item->second;
            size_t &ref_index = (connect_by_a) ? pair.first : pair.second;
            assert(ref_index == bad_index);
            ref_index = connected;
        }
    };

    auto inserts = [&](size_t i1, size_t i2)->bool{
        bool is_l1_a_connect = true; // false => l1_b_connect
        const Slic3r::Line &l1 = lines[i1];
        const Slic3r::Line &l2 = lines[i2];
        if (!PointUtils::is_equal(l1.a, l2.b)) return false;
        if (!PointUtils::is_equal(l1.b, l2.a)) return false;
        else is_l1_a_connect = false;
        insert(i1, i2, is_l1_a_connect);
        insert(i2, i1, !is_l1_a_connect);
        return true;
    };

    std::vector<size_t> not_finished;
    size_t              prev_index = lines.size() - 1;
    for (size_t index = 0; index < lines.size(); ++index) {
        if (!inserts(prev_index, index)) {
            bool found_index      = false;
            bool found_prev_index = false;
            not_finished.erase(std::remove_if(not_finished.begin(),
                                       not_finished.end(),
                           [&](const size_t &not_finished_index) {
                               if (!found_index && inserts(index, not_finished_index)) {
                                   found_index = true;
                                   return true;
                               }
                               if (!found_prev_index && inserts(prev_index, not_finished_index)) {
                                   found_prev_index = true;
                                   return true;
                               }
                               return false;
                                      }),
                       not_finished.end());
            if (!found_index) not_finished.push_back(index);
            if (!found_prev_index) not_finished.push_back(prev_index);
        }
        prev_index = index;
    }
    assert(not_finished.empty());
    return line_connection;
}

Slic3r::BoundingBox LineUtils::create_bounding_box(const Lines &lines) {
    Points pts;
    pts.reserve(lines.size()*2);
    for (const Line &line : lines) {
        pts.push_back(line.a);
        pts.push_back(line.b);
    }
    return BoundingBox(pts);
}

std::map<size_t, size_t> LineUtils::create_line_connection_over_b(const Lines &lines)
{
    std::map<size_t, size_t> line_connection;
    auto inserts = [&](size_t i1, size_t i2) -> bool {
        const Line &l1 = lines[i1];
        const Line &l2 = lines[i2];
        if (!PointUtils::is_equal(l1.b, l2.a))
            return false;
        assert(line_connection.find(i1) == line_connection.end());
        line_connection[i1] = i2;
        return true;
    };

    std::vector<size_t> not_finished_a;
    std::vector<size_t> not_finished_b;
    size_t prev_index = lines.size() - 1;
    for (size_t index = 0; index < lines.size(); ++index) {
        if (!inserts(prev_index, index)) {
            bool found_b = false;
            not_finished_b.erase(std::remove_if(not_finished_b.begin(), not_finished_b.end(),
                           [&](const size_t &not_finished_index) {
                               if (!found_b && inserts(prev_index, not_finished_index)) {
                                   found_b = true;
                                   return true;
                               }
                               return false;
                           }),not_finished_b.end());
            if (!found_b) not_finished_a.push_back(prev_index);

            bool found_a = false;
            not_finished_a.erase(std::remove_if(not_finished_a.begin(), not_finished_a.end(),
                           [&](const size_t &not_finished_index) {
                               if (!found_a && inserts(not_finished_index, index)) {
                                   found_a = true;
                                   return true;
                               }
                               return false;
                           }),not_finished_a.end());
            if (!found_a) not_finished_b.push_back(index);
        }
        prev_index = index;
    }
    assert(not_finished_a.empty());
    assert(not_finished_b.empty());
    return line_connection;
}

void LineUtils::draw(SVG &        svg,
                     const Lines &lines,
                     const char * color,
                     coordf_t     stroke_width,
                     bool         ord,
                     bool         side_points,
                     const char * color_a,
                     const char * color_b)
{
    for (const auto &line : lines) {
        draw(svg, line, color, stroke_width,
            (ord) ? std::to_string(&line - &lines.front()).c_str() : nullptr,
            side_points, color_a, color_b);
    }
}