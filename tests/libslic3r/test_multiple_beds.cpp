#include <catch2/catch_test_macros.hpp>

#include <libslic3r/MultipleBeds.hpp>
#include <numeric>

using namespace Slic3r;
TEST_CASE("Conversion between grid coords and index", "[MultipleBeds]")
{
    std::vector<BedsGrid::Index> original_indices(10);
    std::iota(original_indices.begin(), original_indices.end(), 0);

    // Add indexes covering the whole int positive range.
    const int n{100};
    std::generate_n(std::back_inserter(original_indices), n, [i = 1]() mutable {
        return std::numeric_limits<int>::max() / n * i++;
    });

    std::vector<BedsGrid::GridCoords> coords;
    std::transform(
        original_indices.begin(),
        original_indices.end(),
        std::back_inserter(coords),
        BedsGrid::index2grid_coords
    );

    std::vector<BedsGrid::Index> indices;
    std::transform(
        coords.begin(),
        coords.end(),
        std::back_inserter(indices),
        BedsGrid::grid_coords2index
    );

    CHECK(original_indices == indices);
}
