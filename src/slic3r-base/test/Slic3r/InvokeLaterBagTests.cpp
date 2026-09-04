#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "Slic3r/InvokeLaterBag.hpp"

TEST_CASE("InvokeLaterBag", "[invoke later]")
{
    using namespace Slic3r;
    std::vector<int> v;

    {
        InvokeLaterBag bag;

        bag.add([&v](){ v.push_back(1); });
        bag.add([&v](){ v.push_back(2); });
        bag.add([&v](){ v.push_back(3); });

        REQUIRE(v.empty());
    }

    REQUIRE(v.size() == 3);
    REQUIRE(v == std::vector<int>{1, 2, 3});
}

TEST_CASE("DecouplingInvokeLaterBag", "[invoke later]")
{
    using namespace Slic3r;
    using LaterBag = DeduplicatingInvokeLaterBag<int, int>;
    std::vector<int> v;

    {
        LaterBag bag;

        bag.add({1, 0}, [&v](){ v.push_back(1); });
        bag.add({1, 1}, [&v](){ v.push_back(2); });
        bag.add({1, 0}, [&v](){ v.push_back(3); });

        REQUIRE(v.empty());
    }

    REQUIRE(v.size() == 2);
    REQUIRE(v == std::vector<int>{2, 3});
}