#include "ParabolaUtils.hpp"
#include "PointUtils.hpp"
#include "VoronoiGraphUtils.hpp"

// sampling parabola
#include <libslic3r/Geometry.hpp>
#include <libslic3r/Geometry/VoronoiOffset.hpp>
#include <libslic3r/Geometry/VoronoiVisualUtils.hpp>

using namespace Slic3r::sla;

double ParabolaUtils::length(const ParabolaSegment &parabola)
{
    const Point &point = parabola.focus;
    const Line & line  = parabola.directrix;
    Line norm_line(point, point + line.normal());

    // sign of distance is resolved by dot product in function is_over_zero()
    double scaled_x1 = norm_line.perp_distance_to(parabola.from);
    double scaled_x2 = norm_line.perp_distance_to(parabola.to);

    double parabola_scale = 1. / (4. * focal_length(parabola));

    double x1 = scaled_x1 * parabola_scale;
    double x2 = scaled_x2 * parabola_scale;

    double length_x1 = parabola_arc_length(x1) / parabola_scale;
    double length_x2 = parabola_arc_length(x2) / parabola_scale;

    return (is_over_zero(parabola)) ?
               (length_x1 + length_x2) :    // interval is over zero
               fabs(length_x1 - length_x2); // interval is on same side of parabola
}
double ParabolaUtils::length_by_sampling(
    const ParabolaSegment &parabola,
    double          discretization_step)
{
    using VD = Slic3r::Geometry::VoronoiDiagram;
    std::vector<VD::point_type> parabola_samples({
        VoronoiGraphUtils::to_point(parabola.from),
        VoronoiGraphUtils::to_point(parabola.to)});
    VD::point_type source_point = VoronoiGraphUtils::to_point(parabola.focus);
    VD::segment_type source_segment = VoronoiGraphUtils::to_segment(parabola.directrix);
    ::boost::polygon::voronoi_visual_utils<double>::discretize(
        source_point, source_segment, discretization_step, &parabola_samples);

    double sumLength = 0;
    for (size_t index = 1; index < parabola_samples.size(); ++index) {
        double diffX = parabola_samples[index - 1].x() -
                       parabola_samples[index].x();
        double diffY = parabola_samples[index - 1].y() -
                       parabola_samples[index].y();
        double length = sqrt(diffX * diffX + diffY * diffY);
        sumLength += length;
    }
    return sumLength;
}

double ParabolaUtils::focal_length(const Parabola &parabola)
{
    // https://en.wikipedia.org/wiki/Parabola
    // p = 2f; y = 1/(4f) * x^2; y = 1/(2p) * x^2
    double p = parabola.directrix.perp_distance_to(parabola.focus);
    double f = p / 2.;
    return f;
}

bool ParabolaUtils::is_over_zero(const ParabolaSegment &parabola)
{
    Vec2i64 line_direction = (parabola.directrix.b - parabola.directrix.a).cast<int64_t>();
    Vec2i64 focus_from     = (parabola.focus - parabola.from).cast<int64_t>();
    Vec2i64 focus_to       = (parabola.focus - parabola.to).cast<int64_t>();;
    bool    is_positive_x1 = line_direction.dot(focus_from) > 0;
    bool    is_positive_x2 = line_direction.dot(focus_to) > 0;
    return is_positive_x1 != is_positive_x2;
}

void ParabolaUtils::draw(SVG &                  svg,
                         const ParabolaSegment &parabola,
                         const char *           color,
                         coord_t                width,
                         double                 discretization_step)
{
    using VD = Slic3r::Geometry::VoronoiDiagram;
    if (PointUtils::is_equal(parabola.from, parabola.to)) return;

    std::vector<VD::point_type> parabola_samples(
        {VoronoiGraphUtils::to_point(parabola.from),
         VoronoiGraphUtils::to_point(parabola.to)});  
    VD::point_type source_point = VoronoiGraphUtils::to_point(parabola.focus);
    VD::segment_type source_segment = VoronoiGraphUtils::to_segment(parabola.directrix);

    ::boost::polygon::voronoi_visual_utils<double>::discretize(
        source_point, source_segment, discretization_step, &parabola_samples);

    for (size_t index = 1; index < parabola_samples.size(); ++index) {
        const auto& s0 = parabola_samples[index - 1];
        const auto& s1 = parabola_samples[index];
        Line        l(Point(s0.x(), s0.y()), Point(s1.x(), s1.y()));
        svg.draw(l, color, width);
    }    
}

// PRIVATE
double ParabolaUtils::parabola_arc_length(double x)
{
    double sqrtRes = sqrt(1 + 4 * x * x);
    return 1 / 4. * log(2 * x + sqrtRes) + 1 / 2. * x * sqrtRes;
};
