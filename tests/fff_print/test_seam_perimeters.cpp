#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/GCode/SeamPerimeters.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Point.hpp"
#include <catch2/catch.hpp>
#include <libslic3r/GCode/SeamGeometry.hpp>
#include <libslic3r/Geometry.hpp>
#include <fstream>

#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Seams;

constexpr bool debug_files{false};

const ExPolygon square{
    scaled(Vec2d{0.0, 0.0}), scaled(Vec2d{1.0, 0.0}), scaled(Vec2d{1.0, 1.0}),
    scaled(Vec2d{0.0, 1.0})};

TEST_CASE("Oversample painted", "[Seams][SeamPerimeters]") {
    auto is_painted{[](const Vec3f &position, float radius) {
        return (position - Vec3f{0.5, 0.0, 1.0}).norm() < radius;
    }};
    std::vector<Vec2d> points{Perimeters::Impl::oversample_painted(
        Seams::Geometry::unscaled(square.contour.points), is_painted, 1.0, 0.2
    )};

    REQUIRE(points.size() == 8);
    CHECK((points[1] - Vec2d{0.2, 0.0}).norm() == Approx(0.0));

    points = Perimeters::Impl::oversample_painted(
        Seams::Geometry::unscaled(square.contour.points), is_painted, 1.0, 0.199
    );
    CHECK(points.size() == 9);
}

TEST_CASE("Remove redundant points", "[Seams][SeamPerimeters]") {
    using Perimeters::PointType;
    using Perimeters::PointClassification;

    std::vector<Vec2d> points{{0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}, {3.0, 0.0},
                              {3.0, 1.0}, {3.0, 2.0}, {0.0, 2.0}};
    std::vector<PointType> point_types{PointType::common,
                                       PointType::enforcer, // Should keep this.
                                       PointType::enforcer, // Should keep this.
                                       PointType::blocker,
                                       PointType::blocker, // Should remove this.
                                       PointType::blocker,  PointType::common};

    const auto [resulting_points, resulting_point_types]{
        Perimeters::Impl::remove_redundant_points(points, point_types, 0.1)};

    REQUIRE(resulting_points.size() == 6);
    REQUIRE(resulting_point_types.size() == 6);
    CHECK((resulting_points[3] - Vec2d{3.0, 0.0}).norm() == Approx(0.0));
    CHECK((resulting_points[4] - Vec2d{3.0, 2.0}).norm() == Approx(0.0));
    CHECK(resulting_point_types[3] == PointType::blocker);
    CHECK(resulting_point_types[4] == PointType::blocker);
}

TEST_CASE("Perimeter constructs KD trees", "[Seams][SeamPerimeters]") {
    using Perimeters::PointType;
    using Perimeters::PointClassification;
    using Perimeters::AngleType;

    std::vector<Vec2d> positions{Vec2d{0.0, 0.0}, Vec2d{1.0, 0.0}, Vec2d{1.0, 1.0}, Vec2d{0.0, 1.0}};
    std::vector<double> angles(4, -M_PI / 2.0);
    std::vector<PointType>
        point_types{PointType::enforcer, PointType::blocker, PointType::common, PointType::common};
    std::vector<PointClassification> point_classifications{
        PointClassification::overhang, PointClassification::embedded, PointClassification::embedded,
        PointClassification::common};
    std::vector<AngleType>
        angle_types{AngleType::convex, AngleType::concave, AngleType::smooth, AngleType::smooth};
    Perimeters::Perimeter perimeter{
        3.0,
        2,
        false,
        std::move(positions),
        std::move(angles),
        std::move(point_types),
        std::move(point_classifications),
        std::move(angle_types)};

    CHECK(perimeter.enforced_points.overhanging_points);
    CHECK(perimeter.blocked_points.embedded_points);
    CHECK(perimeter.common_points.common_points);
    CHECK(perimeter.common_points.embedded_points);
}

using std::filesystem::path;

constexpr const char *to_string(Perimeters::PointType point_type) {
    using Perimeters::PointType;

    switch (point_type) {
    case PointType::enforcer: return "enforcer";
    case PointType::blocker: return "blocker";
    case PointType::common: return "common";
    }
    throw std::runtime_error("Unreachable");
}

constexpr const char *to_string(Perimeters::PointClassification point_classification) {
    using Perimeters::PointClassification;

    switch (point_classification) {
    case PointClassification::embedded: return "embedded";
    case PointClassification::overhang: return "overhang";
    case PointClassification::common: return "common";
    }
    throw std::runtime_error("Unreachable");
}

constexpr const char *to_string(Perimeters::AngleType angle_type) {
    using Perimeters::AngleType;

    switch (angle_type) {
    case AngleType::convex: return "convex";
    case AngleType::concave: return "concave";
    case AngleType::smooth: return "smooth";
    }
    throw std::runtime_error("Unreachable");
}

void serialize_shells(std::ostream &output, const Shells::Shells<> &shells) {
    output << "x,y,z,point_type,point_classification,angle_type,layer_index,"
              "point_index,distance,distance_to_previous,is_degenerate,shell_index"
           << std::endl;

    for (std::size_t shell_index{0}; shell_index < shells.size(); ++shell_index) {
        const Shells::Shell<> &shell{shells[shell_index]};
        for (std::size_t perimeter_index{0}; perimeter_index < shell.size(); ++perimeter_index) {
            const Shells::Slice<> &slice{shell[perimeter_index]};
            const Perimeters::Perimeter &perimeter{slice.boundary};
            const std::vector<Vec2d> &points{perimeter.positions};

            double total_distance{0.0};
            for (std::size_t point_index{0}; point_index < perimeter.point_types.size(); ++point_index) {
                const Vec3d point{to_3d(points[point_index], perimeter.slice_z)};
                const Perimeters::PointType point_type{perimeter.point_types[point_index]};
                const Perimeters::PointClassification point_classification{
                    perimeter.point_classifications[point_index]};
                const Perimeters::AngleType angle_type{perimeter.angle_types[point_index]};
                const std::size_t layer_index{slice.layer_index};
                const std::size_t previous_index{point_index == 0 ? points.size() - 1 : point_index - 1};
                const double distance_to_previous{(points[point_index] - points[previous_index]).norm()};
                total_distance += point_index == 0 ? 0.0 : distance_to_previous;
                const double distance{total_distance};
                const bool is_degenerate{perimeter.is_degenerate};

                // clang-format off
                    output
                        << point.x() << ","
                        << point.y() << ","
                        << point.z() << ","
                        << to_string(point_type) << ","
                        << to_string(point_classification) << ","
                        << to_string(angle_type) << ","
                        << layer_index << ","
                        << point_index << ","
                        << distance << ","
                        << distance_to_previous << ","
                        << is_degenerate << ","
                        << shell_index << std::endl;
                // clang-format on
            }
        }
    }
}

TEST_CASE_METHOD(Test::SeamsFixture, "Create perimeters", "[Seams][SeamPerimeters][Integration]") {
    Seams::Perimeters::LayerPerimeters perimeters{
        Seams::Perimeters::create_perimeters(projected, layer_infos, painting, params.perimeter)};

    Seams::Shells::Shells<> shells{
        Seams::Shells::create_shells(std::move(perimeters), params.max_distance)};

    if constexpr (debug_files) {
        std::ofstream csv{"perimeters.csv"};
        serialize_shells(csv, shells);
    }
}
