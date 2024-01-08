#include <catch2/catch_all.hpp>

#include <memory>

#include "libslic3r/GCode.hpp"

using namespace Slic3r;
using namespace Slic3r::GCode::Impl;

SCENARIO("Origin manipulation", "[GCode]") {
	Slic3r::GCodeGenerator gcodegen;
	WHEN("set_origin to (10,0)") {
    	gcodegen.set_origin(Vec2d(10,0));
    	REQUIRE(gcodegen.origin() == Vec2d(10, 0));
    }
	WHEN("set_origin to (10,0) and translate by (5, 5)") {
		gcodegen.set_origin(Vec2d(10,0));
		gcodegen.set_origin(gcodegen.origin() + Vec2d(5, 5));
		THEN("origin returns reference to point") {
    		REQUIRE(gcodegen.origin() == Vec2d(15,5));
    	}
    }
}

struct ApproxEqualsPoints : public Catch::MatcherBase<Points> {
    ApproxEqualsPoints(const Points& expected, unsigned tolerance): expected(expected), tolerance(tolerance) {}
    bool match(const Points& points) const override {
        if (points.size() != expected.size()) {
            return false;
        }
        for (auto i = 0u; i < points.size(); ++i) {
            const Point& point = points[i];
            const Point& expected_point = this->expected[i];
            if (
                std::abs(point.x() - expected_point.x()) > this->tolerance
                || std::abs(point.y() - expected_point.y()) > this->tolerance
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
        scaled(Vec3f{0, 0, 3.0}),
        scaled(Vec3f{0.2, 0, 3.2}),
        scaled(Vec3f{0.5, 0, 3.5}),
        scaled(Vec3f{1, 0, 4.0})
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

    // Try different cases by skipping lines in the travel.
    AABBTreeLines::LinesDistancer<Linef> distancer = get_expolygons_distancer({square_with_hole, square_above});
    CHECK(*get_first_crossed_line_distance(travel, distancer) == Approx(1));
    CHECK(*get_first_crossed_line_distance(tcb::span{travel}.subspan(1), distancer) == Approx(0.2));
    CHECK(*get_first_crossed_line_distance(tcb::span{travel}.subspan(2), distancer) == Approx(0.5));
    CHECK(*get_first_crossed_line_distance(tcb::span{travel}.subspan(3), distancer) == Approx(1.0)); //Edge case
    CHECK(*get_first_crossed_line_distance(tcb::span{travel}.subspan(4), distancer) == Approx(0.7));
    CHECK(*get_first_crossed_line_distance(tcb::span{travel}.subspan(5), distancer) == Approx(1.6));
    CHECK_FALSE(get_first_crossed_line_distance(tcb::span{travel}.subspan(6), distancer));
}

TEST_CASE("Generate regular polygon", "[GCode]") {
    const unsigned points_count{32};
    const Point centroid{scaled(Vec2d{5, -2})};
    const Polygon result{generate_regular_polygon(centroid, scaled(Vec2d{0, 0}), points_count)};
    const Point oposite_point{centroid * 2};

    REQUIRE(result.size() == 32);
    CHECK(result[16].x() == Approx(oposite_point.x()));
    CHECK(result[16].y() == Approx(oposite_point.y()));

    std::vector<double> angles;
    angles.reserve(points_count);
    for (unsigned index = 0; index < points_count; index++) {
        const unsigned previous_index{index == 0 ? points_count - 1 : index - 1};
        const unsigned next_index{index == points_count - 1 ? 0 : index + 1};

        const Point previous_point = result.points[previous_index];
        const Point current_point = result.points[index];
        const Point next_point = result.points[next_index];

        angles.emplace_back(angle(Vec2crd{previous_point - current_point}, Vec2crd{next_point - current_point}));
    }

    std::vector<double> expected;
    angles.reserve(points_count);
    std::generate_n(std::back_inserter(expected), points_count, [&](){
        return angles.front();
    });

    CHECK_THAT(angles, Catch::Matchers::Approx(expected));
}

TEST_CASE("Square bed with padding", "[GCode]") {
    const Bed bed{
        {
            Vec2d{0, 0},
            Vec2d{100, 0},
            Vec2d{100, 100},
            Vec2d{0, 100}
        },
        10.0
    };

    CHECK(bed.centroid.x() == 50);
    CHECK(bed.centroid.y() == 50);
    CHECK(bed.contains_within_padding(Vec2d{10, 10}));
    CHECK_FALSE(bed.contains_within_padding(Vec2d{9, 10}));

}
