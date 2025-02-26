#include <catch2/catch_test_macros.hpp>
#include <string_view>

#include "libslic3r/StaticMap.hpp"

TEST_CASE("Empty static map should be possible to create and should be empty", "[StaticMap]")
{
    using namespace Slic3r;

    static const constexpr StaticSet EmptySet;

    static const constexpr auto EmptyMap = make_staticmap<int, int>();

    constexpr bool is_map_empty = EmptyMap.empty();
    constexpr bool is_set_empty = EmptySet.empty();

    REQUIRE(is_map_empty);
    REQUIRE(is_set_empty);
}

TEST_CASE("StaticSet should derive it's type from the initializer", "[StaticMap]") {
    using namespace Slic3r;
    static const constexpr StaticSet iOneSet = { 1 };
    static constexpr size_t iOneSetSize = iOneSet.size();

    REQUIRE(iOneSetSize == 1);

    static const constexpr StaticSet iManySet = { 1, 3, 5, 80, 40 };
    static constexpr size_t iManySetSize = iManySet.size();

    REQUIRE(iManySetSize == 5);
}

TEST_CASE("StaticMap should derive it's type using make_staticmap", "[StaticMap]") {
    using namespace Slic3r;
    static const constexpr auto ciOneMap = make_staticmap<char, int>({
        {'a', 1},
    });

    static constexpr size_t ciOneMapSize = ciOneMap.size();
    static constexpr bool ciOneMapValid = query(ciOneMap, 'a').value_or(0) == 1;

    REQUIRE(ciOneMapSize == 1);
    REQUIRE(ciOneMapValid);

    static const constexpr auto ciManyMap = make_staticmap<char, int>({
        {'a', 1}, {'b', 2}, {'A', 10}
    });

    static constexpr size_t ciManyMapSize = ciManyMap.size();
    static constexpr bool ciManyMapValid =
        query(ciManyMap, 'a').value_or(0) == 1 &&
        query(ciManyMap, 'b').value_or(0) == 2 &&
        query(ciManyMap, 'A').value_or(0) == 10 &&
        !contains(ciManyMap, 'B') &&
        !query(ciManyMap, 'c').has_value();

    REQUIRE(ciManyMapSize == 3);
    REQUIRE(ciManyMapValid);

    for (auto &[k, v] : ciManyMap) {
        auto val = query(ciManyMap, k);
        REQUIRE(val.has_value());
        REQUIRE(*val == v);
    }
}

TEST_CASE("StaticSet should be able to find contained values", "[StaticMap]")
{
    using namespace Slic3r;
    using namespace std::string_view_literals;

    auto cmp = [](const char *a, const char *b) constexpr {
        return std::string_view{a} < std::string_view{b};
    };

    static constexpr StaticSet CStrSet = {cmp, "One", "Two", "Three"};
    static constexpr StaticSet StringSet = {"One"sv, "Two"sv, "Three"sv};

    static constexpr bool CStrSetValid = query(CStrSet, "One").has_value() &&
                                         contains(CStrSet, "Two") &&
                                         contains(CStrSet, "Three") &&
                                         !contains(CStrSet, "one") &&
                                         !contains(CStrSet, "two") &&
                                         !contains(CStrSet, "three");

    static constexpr bool StringSetValid =  contains(StringSet, "One"sv) &&
                                            contains(StringSet, "Two"sv) &&
                                            contains(StringSet, "Three"sv) &&
                                           !contains(StringSet, "one"sv) &&
                                           !contains(StringSet, "two"sv) &&
                                           !contains(StringSet, "three"sv);

    REQUIRE(CStrSetValid);
    REQUIRE(StringSetValid);
    REQUIRE(CStrSet.size() == 3);
    REQUIRE(StringSet.size() == 3);
}
