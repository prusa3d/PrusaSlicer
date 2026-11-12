#pragma once

#include <optional>
#include <tuple>
#include <map>
#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Biz/Algorithms/SVG.hpp"
#include "Slic3r/Biz/Algorithms/PointUtils.hpp"

namespace Slic3r::Biz::Algorithms {

/// <summary>
/// Class which contain collection of static function
/// for work with Line and Lines.
/// QUESTION: Is it only for SLA?
/// </summary>
class LineUtils
{
public:
    LineUtils() = delete;

    /// <summary>
    /// Sort lines to be in counter clock wise order only by point Line::a and function std::atan2
    /// </summary>
    /// <param name="lines">Lines to be sort</param>
    /// <param name="center">Center for CCW order</param>
    static void sort_CCW(Domain::Lines &lines, const Domain::Point &center);

    /// <summary>
    /// Create line segment intersection of line and circle
    /// </summary>
    /// <param name="line">Input line.</param>
    /// <param name="center">Circle center.</param>
    /// <param name="radius">Circle radius.</param>
    /// <returns>Chord -> line segment inside circle</returns>
    static std::optional<Domain::Line> crop_line(const Domain::Line & line,
                                                 const Domain::Point &center,
                                                 double       radius);
    static std::optional<Domain::Line2d> crop_line(const Domain::Line2d & line,
                                                 const Domain::Point &center,
                                                 double       radius);
    /// <summary>
    /// Create line segment intersection of ray and circle, when exist
    /// </summary>
    /// <param name="ray">Input ray.</param>
    /// <param name="center">Circle center.</param>
    /// <param name="radius">Circle radius.</param>
    /// <returns>Chord -> line segment inside circle</returns>
    static std::optional<Domain::Line> crop_ray(const Domain::Line & ray,
                                                const Domain::Point &center,
                                                double       radius);
    static std::optional<Domain::Line2d> crop_ray(const Domain::Line2d & ray,
                                                const Domain::Point &center,
                                                double       radius);
    /// <summary>
    /// Create line segment intersection of half ray(start point and direction) and circle, when exist
    /// </summary>
    /// <param name="half_ray">Use Line::a as start point and Line::b as direction but no limit</param>
    /// <param name="center">Circle center.</param>
    /// <param name="radius">Circle radius.</param>
    /// <returns>Chord -> line segment inside circle</returns>
    static std::optional<Domain::Line> crop_half_ray(const Domain::Line & half_ray,
                                                     const Domain::Point &center,
                                                     double       radius);
    static std::optional<Domain::Line2d> crop_half_ray(const Domain::Line2d & half_ray,
                                                     const Domain::Point &center,
                                                     double       radius);

    /// <summary>
    /// check if line is parallel to Y
    /// </summary>
    /// <param name="line">Input line</param>
    /// <returns>True when parallel otherwise FALSE</returns>
    static bool is_parallel_y(const Domain::Line &line);
    static bool is_parallel_y(const Domain::Line2d &line);

    /// <summary>
    /// Create parametric coeficient
    /// ax + by + c = 0
    /// Can't be parallel to Y axis - check by function is_parallel_y
    /// </summary>
    /// <param name="line">Input line - cant be parallel with y axis</param>
    /// <returns>a, b, c</returns>
    static std::tuple<double, double, double> get_param(const Domain::Line &line);
    static std::tuple<double, double, double> get_param(const Domain::Line2d &line);

    /// <summary>
    /// Calculate distance between point and ray
    /// </summary>
    /// <param name="line">a and b are only for direction, not limit</param>
    /// <param name="p">Point in space</param>
    /// <returns>Euclid perpedicular distance</returns>
    static double perp_distance(const Domain::Line2d &line, Domain::Vec2d p);

    /// <summary>
    /// Create cross product of line direction.
    /// When zero than they are parallel.
    /// </summary>
    /// <param name="first">First line</param>
    /// <param name="second">Second line</param>
    /// <returns>True when direction are same(scaled or oposit or combination) otherwise FALSE</returns>
    static bool is_parallel(const Domain::Line &first, const Domain::Line &second);

    /// <summary>
    /// Intersection of line - no matter on line limitation
    /// </summary>
    /// <param name="ray1">first line</param>
    /// <param name="ray2">second line</param>
    /// <returns>intersection point when exist</returns>
    static std::optional<Domain::Vec2d> intersection(const Domain::Line &ray1, const Domain::Line &ray2);

    /// <summary>
    /// Check when point lays on line and belongs line range(from point a to point b)
    /// </summary>
    /// <param name="line">source line</param>
    /// <param name="point">point to check</param>
    /// <param name="point">maximal distance from line to belongs line</param>
    /// <returns>True when points lay between line.a and line.b</returns>
    static bool belongs(const Domain::Line & line,
                        const Domain::Point &point,
                        double       benevolence = 1.);

    /// <summary>
    /// Create direction from point a to point b
    /// Direction vector is represented as point
    /// </summary>
    /// <param name="line">input line</param>
    /// <returns>line direction | b - a </returns>
    static Domain::Point direction(const Domain::Line &line);

    /// <summary>
    /// middle point, center of line
    /// </summary>
    /// <param name="line"></param>
    /// <returns>ceneter of line | a+b / 2 </returns>
    static Domain::Point middle(const Domain::Line &line);

    /// <summary>
    /// Calculate foot point in maner of Geometry::foot_pt
    /// - without unnecessary conversion
    /// </summary>
    /// <param name="line">input line</param>
    /// <param name="point">point to search foot on line</param>
    /// <returns>ration betwen point line.a and line.b (in range from 0. to 1.)</returns>
    static double foot(const Domain::Line &line, const Domain::Point& point);

    //                          line index, <a connection, b connection>
    using LineConnection = std::map<size_t, std::pair<size_t, size_t>>;
    /// <summary>
    /// Create data structure from exPolygon lines to find if two lines are connected
    /// !! not tested
    /// </summary>
    /// <param name="lines">Lines created from ExPolygon</param>
    /// <returns>map of connected lines.</returns>
    static LineConnection create_line_connection(const Domain::Lines &lines);

    /// <summary>
    /// create bounding box around lines
    /// </summary>
    /// <param name="lines">input lines</param>
    /// <returns>Bounding box</returns>
    static Domain::BoundingBox2crd create_bounding_box(const Domain::Lines &lines);

    /// <summary>
    /// Create data structure from exPolygon lines to store connected line indexes
    /// </summary>
    /// <param name="lines">Lines created from ExPolygon</param>
    /// <returns>map of connected lines over point line::b</returns>
    static std::map<size_t, size_t> create_line_connection_over_b(const Domain::Lines &lines);

    /// <summary>
    /// Comparator to sort points laying on line from point a to point b
    /// </summary>
    struct SortFromAToB
    {
        std::function<bool(const Domain::Point &, const Domain::Point &)> compare;
        SortFromAToB(const Domain::Line &line)
        {
            Domain::Point dir = LineUtils::direction(line);
            compare   = (PointUtils::is_majorit_x(dir)) ?
                            ((dir.x() < 0) ? is_x_grater : is_x_smaller) :
                            ((dir.y() < 0) ? is_y_grater : is_y_smaller);
        }
        static bool is_x_grater(const Domain::Point &left, const Domain::Point &right)
        {
            return left.x() > right.x();
        }
        static bool is_x_smaller(const Domain::Point &left, const Domain::Point &right)
        {
            return left.x() < right.x();
        }
        static bool is_y_grater(const Domain::Point &left, const Domain::Point &right)
        {
            return left.y() > right.y();
        }
        static bool is_y_smaller(const Domain::Point &left, const Domain::Point &right)
        {
            return left.y() < right.y();
        }
        bool operator()(const Domain::Point &left, const Domain::Point &right)
        {
            return compare(left, right);
        }
    };

    static void draw(Biz::Algorithms::SVG::SVG &       svg,
                     const Domain::Line &line,
                     const char *color        = "gray",
                     double    stroke_width = 0,
                     const char *name         = nullptr,
                     bool        side_points  = false,
                     const char *color_a      = "lightgreen",
                     const char *color_b      = "lightblue");
    static void draw(Biz::Algorithms::SVG::SVG &       svg,
                     const Domain::Lines &lines,
                     const char *color        = "gray",
                     double    stroke_width = 0,
                     bool ord         = false, // write order as text
                     bool        side_points  = false,
                     const char *color_a      = "lightgreen",
                     const char *color_b      = "lightblue");
};

}
