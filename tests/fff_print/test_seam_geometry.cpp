#include <libslic3r/Point.hpp>
#include <catch2/catch.hpp>
#include <libslic3r/GCode/SeamGeometry.hpp>
#include <libslic3r/Geometry.hpp>

using namespace Slic3r;

TEST_CASE("Lists mapping", "[Seams][SeamGeometry]") {
    // clang-format off
    std::vector<std::vector<int>> list_of_lists{
        {},
        {7, 2, 3},
        {9, 6, 3, 6, 7},
        {1, 1, 3},
        {1},
        {3},
        {1},
        {},
        {3}
    };
    // clang-format on

    std::vector<std::size_t> sizes;
    sizes.reserve(list_of_lists.size());
    for (const std::vector<int> &list : list_of_lists) {
        sizes.push_back(list.size());
    }

    const auto [mapping, bucket_cout]{Seams::Geometry::get_mapping(
        sizes,
        [&](const std::size_t layer_index,
            const std::size_t item_index) -> Seams::Geometry::MappingOperatorResult {
            unsigned max_diff{0};
            std::optional<std::size_t> index;
            const std::vector<int> &layer{list_of_lists[layer_index]};
            const std::vector<int> &next_layer{list_of_lists[layer_index + 1]};
            for (std::size_t i{0}; i < next_layer.size(); ++i) {
                const long diff{std::abs(next_layer[i] - layer[item_index])};
                if (diff > max_diff) {
                    max_diff = diff;
                    index = i;
                }
            }
            if (!index) {
                return std::nullopt;
            }
            return std::pair{*index, static_cast<double>(max_diff)};
        }
    )};

    // clang-format off
    CHECK(mapping == std::vector<std::vector<std::size_t>>{
        {},
        {0, 1, 2},
        {1, 3, 0, 4, 5},
        {1, 6, 7},
        {7},
        {7},
        {7},
        {},
        {8}
    });
    // clang-format on
}

TEST_CASE("Vertex angle calculation counterclockwise", "[Seams][SeamGeometry]") {
    std::vector<Vec2d> points{Vec2d{0, 0}, Vec2d{1, 0}, Vec2d{1, 1}, Vec2d{0, 1}};
    std::vector<double> angles{Seams::Geometry::get_vertex_angles(points, 0.1)};

    CHECK(angles.size() == 4);
    for (const double angle : angles) {
        CHECK(angle == Approx(-M_PI / 2));
    }
}

TEST_CASE("Vertex angle calculation clockwise", "[Seams][SeamGeometry]") {
    std::vector<Vec2d> points = {Vec2d{0, 0}, Vec2d{0, 1}, Vec2d{1, 1}, Vec2d{1, 0}};
    std::vector<double> angles = Seams::Geometry::get_vertex_angles(points, 0.1);

    CHECK(angles.size() == 4);
    for (const double angle : angles) {
        CHECK(angle == Approx(M_PI / 2));
    }
}

TEST_CASE("Vertex angle calculation small convex", "[Seams][SeamGeometry]") {
    std::vector<Vec2d> points = {Vec2d{0, 0}, Vec2d{-0.01, 1}, Vec2d{0, 2}, Vec2d{-2, 1}};
    std::vector<double> angles = Seams::Geometry::get_vertex_angles(points, 0.1);

    CHECK(angles.size() == 4);
    CHECK(angles[1] > 0);
    CHECK(angles[1] < 0.02);
}

TEST_CASE("Vertex angle calculation small concave", "[Seams][SeamGeometry]") {
    std::vector<Vec2d> points = {Vec2d{0, 0}, Vec2d{0.01, 1}, Vec2d{0, 2}, Vec2d{-2, 1}};
    std::vector<double> angles = Seams::Geometry::get_vertex_angles(points, 0.1);

    CHECK(angles.size() == 4);
    CHECK(angles[1] < 0);
    CHECK(angles[1] > -0.02);
}

TEST_CASE("Vertex angle is rotation agnostic", "[Seams][SeamGeometry]") {
    std::vector<Vec2d> points = {Vec2d{0, 0}, Vec2d{0.01, 1}, Vec2d{0, 2}, Vec2d{-2, 1}};
    std::vector<double> angles = Seams::Geometry::get_vertex_angles(points, 0.1);

    Points polygon_points;
    using std::transform, std::back_inserter;
    transform(points.begin(), points.end(), back_inserter(polygon_points), [](const Vec2d &point) {
        return scaled(point);
    });
    Polygon polygon{polygon_points};
    polygon.rotate(M_PI - Slic3r::Geometry::deg2rad(10.0));

    std::vector<Vec2d> rotated_points;
    using std::transform, std::back_inserter;
    transform(
        polygon.points.begin(), polygon.points.end(), back_inserter(rotated_points),
        [](const Point &point) { return unscaled(point); }
    );

    std::vector<double> rotated_angles = Seams::Geometry::get_vertex_angles(points, 0.1);
    CHECK(rotated_angles[1] == Approx(angles[1]));
}

TEST_CASE("Calculate overhangs", "[Seams][SeamGeometry]") {
    const ExPolygon square{
        scaled(Vec2d{0.0, 0.0}),
        scaled(Vec2d{1.0, 0.0}),
        scaled(Vec2d{1.0, 1.0}),
        scaled(Vec2d{0.0, 1.0})
    };
    const std::vector<Vec2d> points{Seams::Geometry::unscaled(square.contour.points)};
    ExPolygon previous_layer{square};
    previous_layer.translate(scaled(Vec2d{-0.5, 0}));
    AABBTreeLines::LinesDistancer<Linef> previous_layer_distancer{
        to_unscaled_linesf({previous_layer})};
    const std::vector<double> overhangs{
        Seams::Geometry::get_overhangs(points, previous_layer_distancer, 0.5)};
    REQUIRE(overhangs.size() == points.size());
    CHECK_THAT(overhangs, Catch::Matchers::Approx(std::vector<double>{
        0.0, M_PI / 4.0, M_PI / 4.0, 0.0
    }));
}

const Linesf lines{to_unscaled_linesf({ExPolygon{
    scaled(Vec2d{0.0, 0.0}),
    scaled(Vec2d{1.0, 0.0}),
    scaled(Vec2d{1.0, 1.0}),
    scaled(Vec2d{0.0, 1.0})
}})};

TEST_CASE("Offset along loop lines forward", "[Seams][SeamGeometry]") {
    const std::optional<Seams::Geometry::PointOnLine> result{Seams::Geometry::offset_along_lines(
        {0.5, 0.0}, 0, lines, 3.9, Seams::Geometry::Direction1D::forward
    )};
    REQUIRE(result);
    const auto &[point, line_index] = *result;
    CHECK((scaled(point) - Point::new_scale(0.4, 0.0)).norm() < scaled(EPSILON));
    CHECK(line_index == 0);
}

TEST_CASE("Offset along loop lines backward", "[Seams][SeamGeometry]") {
    const std::optional<Seams::Geometry::PointOnLine> result{Seams::Geometry::offset_along_lines(
        {1.0, 0.5}, 1, lines, 1.8, Seams::Geometry::Direction1D::backward
    )};
    REQUIRE(result);
    const auto &[point, line_index] = *result;
    CHECK((scaled(point) - Point::new_scale(0.0, 0.3)).norm() < scaled(EPSILON));
    CHECK(line_index == 3);
}
