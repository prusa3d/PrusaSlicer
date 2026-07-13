#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <algorithm>

#include "libslic3r/ConicalOverhangs.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"

using namespace Slic3r;
using Catch::Approx;

static const double kPI = 3.14159265358979323846;

static double total_area(const ExPolygons &expolys)
{
    double a = 0.;
    for (const ExPolygon &ex : expolys)
        a += ex.area();
    return a;
}

// A CCW axis-aligned square of side `s` mm centred on the origin.
static ExPolygon square_mm(double s)
{
    ExPolygon e;
    const double h = 0.5 * s;
    e.contour.points = {
        Point::new_scale(-h, -h), Point::new_scale( h, -h),
        Point::new_scale( h,  h), Point::new_scale(-h,  h),
    };
    return e;
}

static void densify_ex(ExPolygons &expolys, double mm)
{
    for (ExPolygon &ex : expolys) {
        ex.contour.densify(float(scale_(mm)));
        for (Polygon &hole : ex.holes)
            hole.densify(float(scale_(mm)));
    }
}

static double max_abs_turn(const ExPolygons &expolys)
{
    double m = 0.;
    for (const ExPolygon &ex : expolys) {
        const Points &p = ex.contour.points;
        const int n = int(p.size());
        for (int i = 0; i < n; ++i) {
            const Vec2d a(p[(i - 1 + n) % n].x(), p[(i - 1 + n) % n].y());
            const Vec2d b(p[i].x(),               p[i].y());
            const Vec2d c(p[(i + 1) % n].x(),      p[(i + 1) % n].y());
            m = std::max(m, std::abs(conical_signed_turning_angle(a, b, c)));
        }
    }
    return m;
}

TEST_CASE("conical: one melt step rounds sharp corners", "[ConicalOverhangs]")
{
    ExPolygons eroded = offset_ex(ExPolygons{ square_mm(10.) }, -float(scale_(0.5)));
    densify_ex(eroded, 0.5);

    const double before = max_abs_turn(eroded);
    const ExPolygons relaxed = relax_cone_ridges(eroded, scale_(0.3));
    const double after = max_abs_turn(relaxed);

    INFO("max turn before = " << before << ", after = " << after);
    REQUIRE(before == Approx(kPI / 2).margin(0.25));   // corners start ~90 deg
    REQUIRE(after < before - 1e-3);                    // a single step already rounds
}

TEST_CASE("conical: melting a square converges towards a circle", "[ConicalOverhangs]")
{
    // Compounded down the stack, curve-shortening turns the square rounder and
    // rounder: max turning angle decreases monotonically towards a circle (~0).
    ExPolygons e = offset_ex(ExPolygons{ square_mm(20.) }, -float(scale_(0.5)));
    densify_ex(e, 0.5);

    double prev_turn = max_abs_turn(e);
    for (int k = 0; k < 15; ++k) {
        e = relax_cone_ridges(e, scale_(0.2));
        densify_ex(e, 0.5);                     // keep it subdivided as it shrinks
        const double turn = max_abs_turn(e);
        REQUIRE(turn <= prev_turn + 1e-6);      // monotonically rounder
        prev_turn = turn;
    }
    REQUIRE(prev_turn < 0.8);                    // well rounded (a circle -> ~0)
}

TEST_CASE("conical: melt step never moves a vertex past the cap", "[ConicalOverhangs]")
{
    // The displacement clamp is the printability guarantee: nothing recedes more
    // than max_travel, so dilating the result by max_travel must cover the input.
    ExPolygons eroded = offset_ex(ExPolygons{ square_mm(10.) }, -float(scale_(0.5)));
    densify_ex(eroded, 0.5);

    const double mt = scale_(0.15);
    const ExPolygons relaxed = relax_cone_ridges(eroded, mt);
    const ExPolygons cover   = offset_ex(relaxed, float(mt) + float(scale_(0.01)));
    REQUIRE(total_area(diff_ex(eroded, cover)) <= 0.005 * total_area(eroded));
}
