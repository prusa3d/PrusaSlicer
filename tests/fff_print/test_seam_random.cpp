#include <libslic3r/Point.hpp>
#include <catch2/catch_test_macros.hpp>
#include <libslic3r/GCode/SeamRandom.hpp>
#include "test_data.hpp"
#include <fstream>

using namespace Slic3r;
using namespace Slic3r::Seams;

constexpr bool debug_files{false};

namespace RandomTest {
Perimeters::Perimeter get_perimeter() {
    const double slice_z{1.0};
    const std::size_t layer_index{};
    std::vector<Vec2d> positions{{0.0, 0.0}, {0.5, 0.0}, {1.0, 0.0}};
    std::vector<double> angles(positions.size(), -M_PI / 2.0);
    std::vector<Perimeters::PointType> point_types(positions.size(), Perimeters::PointType::common);
    std::vector<Perimeters::PointClassification>
        point_classifications{positions.size(), Perimeters::PointClassification::common};
    std::vector<Perimeters::AngleType> angle_type(positions.size(), Perimeters::AngleType::concave);

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
} // namespace RandomTest

double get_chi2_uniform(const std::vector<double> &data, const double min, const double max, const std::size_t bin_count) {
    std::vector<std::size_t> bins(bin_count);
    const double bin_size{(max - min) / bin_count};
    const double expected_frequncy{static_cast<double>(data.size()) / bin_count};

    for (const double value : data) {
        auto bin{static_cast<int>(std::floor((value - min) / bin_size))};
        bins[bin]++;
    }

    return std::accumulate(bins.begin(), bins.end(), 0.0, [&](const double total, const std::size_t count_in_bin){
        return total + std::pow(static_cast<double>(count_in_bin - expected_frequncy), 2.0) / expected_frequncy;
    });
}

TEST_CASE("Random is uniform", "[Seams][SeamRandom]") {
    const int seed{42};
    std::mt19937 random_engine{seed};
    const Random::Impl::Random random{random_engine};
    Perimeters::Perimeter perimeter{RandomTest::get_perimeter()};

    std::vector<double> x_positions;
    const std::size_t count{1001};
    x_positions.reserve(count);
    std::generate_n(std::back_inserter(x_positions), count, [&]() {
        std::optional<SeamChoice> choice{
            random(perimeter, Perimeters::PointType::common, Perimeters::PointClassification::common)};
        return choice->position.x();
    });
    const std::size_t degrees_of_freedom{10};
    const double critical{29.588}; // dof 10, significance 0.001

    CHECK(get_chi2_uniform(x_positions, 0.0, 1.0, degrees_of_freedom + 1) < critical);
}

TEST_CASE("Random respects point type", "[Seams][SeamRandom]") {
    const int seed{42};
    std::mt19937 random_engine{seed};
    const Random::Impl::Random random{random_engine};
    Perimeters::Perimeter perimeter{RandomTest::get_perimeter()};
    std::optional<SeamChoice> choice{
        random(perimeter, Perimeters::PointType::common, Perimeters::PointClassification::common)};

    REQUIRE(choice);
    const std::size_t picked_index{choice->previous_index};
    perimeter.point_types[picked_index] = Perimeters::PointType::blocker;
    choice = random(perimeter, Perimeters::PointType::common, Perimeters::PointClassification::common);
    REQUIRE(choice);
    CHECK(choice->previous_index != picked_index);
}

TEST_CASE_METHOD(Test::SeamsFixture, "Generate random seam", "[Seams][SeamRandom][Integration]") {
    Seams::Perimeters::LayerPerimeters perimeters{
        Seams::Perimeters::create_perimeters(projected, layer_infos, painting, params.perimeter)};
    const std::vector<std::vector<SeamPerimeterChoice>> seams{
        Random::get_object_seams(std::move(perimeters), params.random_seed)};

    if constexpr (debug_files) {
        std::ofstream csv{"random_seam.csv"};
        Test::serialize_seam(csv, seams);
    }
}
