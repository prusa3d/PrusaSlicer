#include <catch2/catch_test_macros.hpp>
#include "Slic3r/Biz/Preset/JsonUtils.hpp"

using Slic3r::Biz::Preset::merge_json;

TEST_CASE("Custom parameters merging", "[JsonUtils]")
{
    SECTION("Merge two non-empty JSON strings")
    {
        std::string bottom = R"({"a": 1, "b": 2})";
        std::string top    = R"({"b": 3, "c": 4})";
        std::string merged = merge_json(bottom, top);
        REQUIRE(merged == R"({"a":1,"b":3,"c":4})");
    }

    SECTION("Merge with empty bottom JSON")
    {
        std::string bottom = "";
        std::string top    = R"({"a": 1})";
        std::string merged = merge_json(bottom, top);
        REQUIRE(merged == top);
    }

    SECTION("Merge with empty top JSON")
    {
        std::string bottom = R"({"a": 1})";
        std::string top    = "";
        std::string merged = merge_json(bottom, top);
        REQUIRE(merged == bottom);
    }

    SECTION("Merge with invalid JSON")
    {
        std::string bottom = R"({"a": 1})";
        std::string top    = "invalid json";
        std::string merged = merge_json(bottom, top);
        REQUIRE(merged == top);
    }
}
