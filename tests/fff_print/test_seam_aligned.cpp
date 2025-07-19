#include <libslic3r/Point.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <libslic3r/GCode/SeamAligned.hpp>
#include "test_data.hpp"
#include <fstream>

using namespace Slic3r;
using namespace Slic3r::Seams;
using namespace Catch;

constexpr bool debug_files{false};

namespace AlignedTest {
Perimeters::Perimeter get_perimeter() {
    const double slice_z{1.0};
    const std::size_t layer_index{};
    std::vector<Vec2d> positions{{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}, {0.0, 0.5}};
    std::vector<double> angles(positions.size(), -M_PI / 2.0);
    angles[4] = 0.0;
    std::vector<Perimeters::PointType> point_types(positions.size(), Perimeters::PointType::common);
    std::vector<Perimeters::PointClassification>
        point_classifications{positions.size(), Perimeters::PointClassification::common};
    std::vector<Perimeters::AngleType> angle_type(positions.size(), Perimeters::AngleType::concave);
    angle_type[4] = Perimeters::AngleType::smooth;

    return {
        slice_z,
        layer_index,
        false,
        std::move(positions),
        std::move(angles),
        std::move(point_types),
        std::move(point_classifications),
        std::move(angle_type)};
}
} // namespace AlignedTest

TEST_CASE("Snap to angle", "[Seams][SeamAligned]") {
    const Vec2d point{0.0, 0.4};
    const std::size_t search_start{4};
    const Perimeters::Perimeter perimeter{AlignedTest::get_perimeter()};

    std::optional<std::size_t> snapped_to{
        Aligned::Impl::snap_to_angle(point, search_start, perimeter, 0.5)};

    REQUIRE(snapped_to);
    CHECK(*snapped_to == 0);

    snapped_to = Aligned::Impl::snap_to_angle(point, search_start, perimeter, 0.3);
    REQUIRE(!snapped_to);
}

TEST_CASE("Get seam options", "[Seams][SeamAligned]") {
    Perimeters::Perimeter perimeter{AlignedTest::get_perimeter()};
    const Vec2d prefered_position{0.0, 0.3};

    Aligned::Impl::SeamOptions options{Aligned::Impl::get_seam_options(
        perimeter, prefered_position, *perimeter.common_points.common_points, 0.4
    )};

    CHECK(options.closest == 4);
    CHECK(options.adjacent == 0);
    CHECK((options.on_edge - Vec2d{0.0, 0.3}).norm() == Approx(0.0));
    REQUIRE(options.snapped);
    CHECK(options.snapped == 0);
}

struct PickSeamOptionFixture
{
    Perimeters::Perimeter perimeter{AlignedTest::get_perimeter()};

    Aligned::Impl::SeamOptions options{
        4,               // closest
        0,               // adjacent
        true,            // forward
        false,           // snapped
        Vec2d{0.0, 0.3}, // on_edge
    };
};

TEST_CASE_METHOD(PickSeamOptionFixture, "Pick seam option", "[Seams][SeamAligned]") {
    auto [previous_index, next_index, position]{pick_seam_option(perimeter, options)};
    CHECK(previous_index == next_index);
    CHECK((position - Vec2d{0.0, 0.0}).norm() == Approx(0.0));
}

TEST_CASE_METHOD(PickSeamOptionFixture, "Pick seam option picks enforcer", "[Seams][SeamAligned]") {
    perimeter.point_types[4] = Perimeters::PointType::enforcer;

    auto [previous_index, next_index, position]{pick_seam_option(perimeter, options)};
    CHECK(previous_index == next_index);
    CHECK((position - Vec2d{0.0, 0.5}).norm() == Approx(0.0));
}

TEST_CASE_METHOD(PickSeamOptionFixture, "Nearest point", "[Seams][SeamAligned]") {
    const std::optional<SeamChoice> result{Aligned::Impl::Nearest{Vec2d{0.4, -0.1}, 0.2}(
        perimeter, Perimeters::PointType::common, Perimeters::PointClassification::common
    )};
    CHECK(result->previous_index == 0);
    CHECK(result->next_index == 1);
    CHECK((result->position - Vec2d{0.4, 0.0}).norm() == Approx(0.0));
}

TEST_CASE_METHOD(PickSeamOptionFixture, "Least visible point", "[Seams][SeamAligned]") {
    std::vector<double> precalculated_visibility{};
    for (std::size_t i{0}; i < perimeter.positions.size(); ++i) {
        precalculated_visibility.push_back(-static_cast<double>(i));
    }
    Aligned::Impl::LeastVisible least_visible{precalculated_visibility};
    const std::optional<SeamChoice> result{least_visible(
        perimeter, Perimeters::PointType::common, Perimeters::PointClassification::common
    )};
    CHECK(result->previous_index == 4);
    CHECK(result->next_index == 4);
    CHECK((result->position - Vec2d{0.0, 0.5}).norm() == Approx(0.0));
}

TEST_CASE_METHOD(Test::SeamsFixture, "Generate aligned seam", "[Seams][SeamAligned][Integration]") {
    Seams::Perimeters::LayerPerimeters perimeters{
        Seams::Perimeters::create_perimeters(projected, layer_infos, painting, params.perimeter)};
    Seams::Shells::Shells<> shells{
        Seams::Shells::create_shells(std::move(perimeters), params.max_distance)};

    const std::vector<std::vector<SeamPerimeterChoice>> seam{
        Aligned::get_object_seams(std::move(shells), visibility_calculator, params.aligned)};

    if constexpr (debug_files) {
        std::ofstream csv{"aligned_seam.csv"};
        Test::serialize_seam(csv, seam);
    }
}

TEST_CASE_METHOD(Test::SeamsFixture, "Calculate visibility", "[Seams][SeamAligned][Integration]") {
    if constexpr (debug_files) {
        std::ofstream csv{"visibility.csv"};
        csv << "x,y,z,visibility,total_visibility" << std::endl;

        Seams::Perimeters::LayerPerimeters perimeters{
            Seams::Perimeters::create_perimeters(projected, layer_infos, painting, params.perimeter)};

        Seams::Shells::Shells<> shells{
            Seams::Shells::create_shells(std::move(perimeters), params.max_distance)};
        for (const Shells::Shell<> &shell : shells) {
            for (const Shells::Slice<> &slice : shell) {
                for (std::size_t index{0}; index < slice.boundary.positions.size(); ++index) {
                    const Vec2d &position{slice.boundary.positions[index]};
                    const double point_visibility{visibility.calculate_point_visibility(
                        to_3d(position.cast<float>(), slice.boundary.slice_z)
                    )};
                    const double total_visibility{
                        visibility_calculator(SeamChoice{index, index, position}, slice.boundary)};

                    // clang-format off
                    csv <<
                        position.x() << "," <<
                        position.y() << "," <<
                        slice.boundary.slice_z << "," <<
                        point_visibility << "," <<
                        total_visibility << std::endl;
                    // clang-format on
                }
            }
        }
    }
}
