#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <catch2/catch_approx.hpp>
#include <libslic3r/GCode/Travels.hpp>
#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/GCode.hpp>
#include <boost/math/special_functions/pow.hpp>

using namespace Slic3r;
using namespace Slic3r::GCode::Impl::Travels;
using namespace Catch;

struct ApproxEqualsPoints : public Catch::Matchers::MatcherBase<Points> {
    ApproxEqualsPoints(const Points& expected, unsigned tolerance): expected(expected), tolerance(tolerance) {}
    bool match(const Points& points) const override {
        if (points.size() != expected.size()) {
            return false;
        }
        for (auto i = 0u; i < points.size(); ++i) {
            const Point& point = points[i];
            const Point& expected_point = this->expected[i];
            if (
                std::abs(point.x() - expected_point.x()) > int(this->tolerance)
                || std::abs(point.y() - expected_point.y()) > int(this->tolerance)
            ) {
                return false;
            }
        }
        return true;
    }
    std::string describe() const override {
        std::stringstream ss;
        ss << std::endl;
        for (const Point& point : expected) {
            ss << "(" << point.x() << ", " << point.y() << ")" << std::endl;
        }
        ss << "With tolerance: " << this->tolerance;

        return "Equals " + ss.str();
    }

private:
    Points expected;
    unsigned tolerance;
};

Points get_points(const std::vector<DistancedPoint>& result) {
    Points result_points;
    std::transform(
        result.begin(),
        result.end(),
        std::back_inserter(result_points),
        [](const DistancedPoint& point){
            return point.point;
        }
    );
    return result_points;
}

std::vector<double> get_distances(const std::vector<DistancedPoint>& result) {
    std::vector<double> result_distances;
    std::transform(
        result.begin(),
        result.end(),
        std::back_inserter(result_distances),
        [](const DistancedPoint& point){
            return point.distance_from_start;
        }
    );
    return result_distances;
}

TEST_CASE("Place points at distances - expected use", "[GCode]") {
    std::vector<Point> line{
        scaled(Vec2f{0, 0}),
        scaled(Vec2f{1, 0}),
        scaled(Vec2f{2, 1}),
        scaled(Vec2f{2, 2})
    };
    std::vector<double> distances{0, 0.2, 0.5, 1 + std::sqrt(2)/2, 1 + std::sqrt(2) + 0.5, 100.0};
    std::vector<DistancedPoint> result = slice_xy_path(line, distances);

    REQUIRE_THAT(get_points(result), ApproxEqualsPoints(Points{
        scaled(Vec2f{0, 0}),
        scaled(Vec2f{0.2, 0}),
        scaled(Vec2f{0.5, 0}),
        scaled(Vec2f{1, 0}),
        scaled(Vec2f{1.5, 0.5}),
        scaled(Vec2f{2, 1}),
        scaled(Vec2f{2, 1.5}),
        scaled(Vec2f{2, 2})
    }, 5));

    REQUIRE_THAT(get_distances(result), Catch::Matchers::Approx(std::vector<double>{
        distances[0], distances[1], distances[2], 1, distances[3], 1 + std::sqrt(2), distances[4], 2 + std::sqrt(2)
    }));
}

TEST_CASE("Place points at distances - edge case", "[GCode]") {
    std::vector<Point> line{
        scaled(Vec2f{0, 0}),
        scaled(Vec2f{1, 0}),
        scaled(Vec2f{2, 0})
    };
    std::vector<double> distances{0, 1, 1.5, 2};
    Points result{get_points(slice_xy_path(line, distances))};
    CHECK(result == Points{
        scaled(Vec2f{0, 0}),
        scaled(Vec2f{1, 0}),
        scaled(Vec2f{1.5, 0}),
        scaled(Vec2f{2, 0})
    });
}

TEST_CASE("Generate elevated travel", "[GCode]") {
    std::vector<Point> xy_path{
        scaled(Vec2f{0, 0}),
        scaled(Vec2f{1, 0}),
    };
    std::vector<double> ensure_points_at_distances{0.2, 0.5};
    Points3 result{generate_elevated_travel(xy_path, ensure_points_at_distances, 2.0, [](double x){return 1 + x;})};

    CHECK(result == Points3{
        scaled(Vec3f{ 0.f, 0.f,  3.f}),
        scaled(Vec3f{0.2f, 0.f, 3.2f}),
        scaled(Vec3f{0.5f, 0.f, 3.5f}),
        scaled(Vec3f{ 1.f, 0.f,  4.f})
    });
}

TEST_CASE("Get first crossed line distance", "[GCode]") {
    // A 2x2 square at 0, 0, with 1x1 square hole in its center.
    ExPolygon square_with_hole{
        {
            scaled(Vec2f{-1, -1}),
            scaled(Vec2f{1, -1}),
            scaled(Vec2f{1, 1}),
            scaled(Vec2f{-1, 1})
        },
        {
            scaled(Vec2f{-0.5, -0.5}),
            scaled(Vec2f{0.5, -0.5}),
            scaled(Vec2f{0.5, 0.5}),
            scaled(Vec2f{-0.5, 0.5})
        }
    };
    // A 2x2 square above the previous square at (0, 3).
    ExPolygon square_above{
        {
            scaled(Vec2f{-1, 2}),
            scaled(Vec2f{1, 2}),
            scaled(Vec2f{1, 4}),
            scaled(Vec2f{-1, 4})
        }
    };

    // Bottom-up travel intersecting the squares.
    Lines travel{Polyline{
        scaled(Vec2f{0, -2}),
        scaled(Vec2f{0, -0.7}),
        scaled(Vec2f{0, 0}),
        scaled(Vec2f{0, 1}),
        scaled(Vec2f{0, 1.3}),
        scaled(Vec2f{0, 2.4}),
        scaled(Vec2f{0, 4.5}),
        scaled(Vec2f{0, 5}),
    }.lines()};

    std::vector<GCode::ObjectOrExtrusionLinef> lines;
    for (const ExPolygon& polygon : {square_with_hole, square_above}) {
        for (const Line& line : polygon.lines()) {
            lines.emplace_back(unscale(line.a), unscale(line.b));
        }
    }
    // Try different cases by skipping lines in the travel.
    AABBTreeLines::LinesDistancer<GCode::ObjectOrExtrusionLinef> distancer{std::move(lines)};

    CHECK(get_first_crossed_line_distance(travel, distancer) == Approx(1));
    CHECK(get_first_crossed_line_distance(tcb::span{travel}.subspan(1), distancer) == Approx(0.2));
    CHECK(get_first_crossed_line_distance(tcb::span{travel}.subspan(2), distancer) == Approx(0.5));
    CHECK(get_first_crossed_line_distance(tcb::span{travel}.subspan(3), distancer) == Approx(1.0)); //Edge case
    CHECK(get_first_crossed_line_distance(tcb::span{travel}.subspan(4), distancer) == Approx(0.7));
    CHECK(get_first_crossed_line_distance(tcb::span{travel}.subspan(5), distancer) == Approx(1.6));
    CHECK(get_first_crossed_line_distance(tcb::span{travel}.subspan(6), distancer) == std::numeric_limits<double>::max());
}


TEST_CASE("Elevated travel formula", "[GCode]") {
    const double lift_height{10};
    const double slope_end{10};
    const double blend_width{10};
    const ElevatedTravelParams params{lift_height, slope_end, blend_width};

    ElevatedTravelFormula f{params};

    const double distance = slope_end - blend_width / 2;
    const double slope = (f(distance) - f(0)) / distance;
    // At the begining it has given slope.
    CHECK(slope == lift_height / slope_end);
    // At the end it is flat.
    CHECK(f(slope_end + blend_width / 2) == f(slope_end + blend_width));
    // Should be smoothed.
    CHECK(f(slope_end) < lift_height);
}
