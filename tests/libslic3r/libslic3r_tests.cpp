#include <catch_main.hpp>

#include "libslic3r/Utils.hpp"

// bimap test
#include <string_view>
#include <boost/bimap.hpp>
#include <boost/assign.hpp>

namespace {

inline void sort_remove_duplicates(std::vector<int>& vec)
{
    std::sort(vec.begin(), vec.end());
    vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
}

TEST_CASE("sort_remove_duplicates", "[utils]") {
	std::vector<int> data_src = { 3, 0, 2, 1, 15, 3, 5, 6, 3, 1, 0 };
	std::vector<int> data_dst = { 0, 1, 2, 3, 5, 6, 15 };
	sort_remove_duplicates(data_src);
    REQUIRE(data_src == data_dst);
}

TEST_CASE("string_printf", "[utils]") {
    SECTION("Empty format with empty data should return empty string") {
        std::string outs = Slic3r::string_printf("");
        REQUIRE(outs.empty());
    }
    
    SECTION("String output length should be the same as input") {
        std::string outs = Slic3r::string_printf("1234");
        REQUIRE(outs.size() == 4);
    }
    
    SECTION("String format should be interpreted as with sprintf") {
        std::string outs = Slic3r::string_printf("%d %f %s", 10, 11.4, " This is a string");
        char buffer[1024];
        
        sprintf(buffer, "%d %f %s", 10, 11.4, " This is a string");
        
        REQUIRE(outs.compare(buffer) == 0);
    }
    
    SECTION("String format should survive large input data") {
        std::string input(2048, 'A');
        std::string outs = Slic3r::string_printf("%s", input.c_str());
        REQUIRE(outs.compare(input) == 0);
    }
}

TEST_CASE("Bimap duplicity behavior") {
    enum class number {
        one = 1,
        three = 3,
        tri = 3 // ONLY alias
    };

    using BimapType = boost::bimap<std::string_view, number>;
    BimapType bimap = boost::assign::list_of<BimapType::relation>
        ("one", number::one)
        ("three", number::three)
        ("tri", number::tri) // no matter if it is there
        ;

    const auto& to_type = bimap.left;    
    
    auto item_number1 = to_type.find("one");
    REQUIRE(item_number1 != to_type.end());
    CHECK(item_number1->second == number::one);

    auto item_number3 = to_type.find("three");
    REQUIRE(item_number3 != to_type.end());
    CHECK(item_number3->second == number::three);

    // to_type.find("tri"); // not in map
    
    const auto &to_name = bimap.right;
    
    auto it1 = to_name.find(number::one);
    REQUIRE(it1 != to_name.end());
    CHECK(it1->second == "one");

    auto it2 = to_name.find(number::three);
    REQUIRE(it2 != to_name.end());
    CHECK(it2->second == "three");

    auto it3 = to_name.find(number::tri);
    REQUIRE(it3 != to_name.end());
    REQUIRE(number::three == number::tri);        
    CHECK(it3->second == "three");
}

} // end namespace
