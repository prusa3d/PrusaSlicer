#include <catch2/catch_test_macros.hpp>
#include <libslic3r/SLA/SupportIslands/VectorUtils.hpp>

using namespace Slic3r::sla;

TEST_CASE("Reorder", "[Utils], [VectorUtils]")
{
    std::vector<int> data{0, 1, 3, 2, 4, 7, 6, 5, 8};
    std::vector<int> order{0, 1, 3, 2, 4, 7, 6, 5, 8};

    VectorUtils::reorder(order.begin(), order.end(), data.begin());
    for (size_t i = 0; i < data.size() - 1; ++i) {
        CHECK(data[i] < data[i + 1]);
    }
}

TEST_CASE("Reorder destructive", "[Utils], [VectorUtils]"){
    std::vector<int> data {0, 1, 3, 2, 4, 7, 6, 5, 8};
    std::vector<int> order{0, 1, 3, 2, 4, 7, 6, 5, 8};

    VectorUtils::reorder_destructive(order.begin(), order.end(), data.begin());
    for (size_t i = 0; i < data.size() - 1;++i) { 
        CHECK(data[i] < data[i + 1]);
    }
}
