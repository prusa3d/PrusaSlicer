#include "sla_test_utils.hpp"

#include <libslic3r/SLA/SupportIslands/ParabolaUtils.hpp>

using namespace Slic3r;
using namespace Slic3r::sla;

void parabola_check_length(const ParabolaSegment &parabola)
{
    auto   diffPoint = parabola.to - parabola.from;
    double min       = sqrt(diffPoint.x() * diffPoint.x() +
                      diffPoint.y() * diffPoint.y());
    double max       = static_cast<double>(diffPoint.x()) + diffPoint.y();
    double len = ParabolaUtils::length(parabola);
    double len2 = ParabolaUtils::length_by_sampling(parabola, 1.);
    CHECK(fabs(len2 - len) < 1.);
    CHECK(len >= min);
    CHECK(len <= max);
}

// after generalization put to ParabolaUtils
double getParabolaY(const Parabola &parabola, double x)
{
    double f    = ParabolaUtils::focal_length(parabola);
    Vec2d  perp = parabola.directrix.normal().cast<double>();
    // work only for test cases
    if (perp.y() > 0.) perp *= -1.;
    perp.normalize();
    Vec2d v = parabola.focus.cast<double>() + perp * f;
    return 1 / (4 * f) * (x - v.x()) * (x - v.x()) + v.y();
}

TEST_CASE("Parabola length", "[SupGen][Voronoi][Parabola]")
{
    using namespace Slic3r::sla;
    double scale = 1e6;
    // U shape parabola
    Parabola parabola_x2(Line({-1. * scale, -.25 * scale},
                              {1. * scale, -.25 * scale}),
                         Point(0. * scale, .25 * scale));

    double from_x = 1 * scale;
    double to_x   = 3 * scale;
    Point  from(from_x, getParabolaY(parabola_x2, from_x));
    Point  to(to_x, getParabolaY(parabola_x2, to_x));
    ParabolaSegment parabola_segment(parabola_x2, from, to);
    parabola_check_length(parabola_segment);
} 


