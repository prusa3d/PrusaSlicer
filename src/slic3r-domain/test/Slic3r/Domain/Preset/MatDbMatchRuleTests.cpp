#include <catch2/catch_test_macros.hpp>
#include "Slic3r/Domain/Preset/MatDbMatchRule.hpp"

using namespace Slic3r::Domain;

TEST_CASE("MatDbMatchRule tests", "[preset][matdb]")
{
    MatDb::Material mat;

    #define SECTION_MATCH_MAT_ATTR(N) SECTION(#N) \
    { \
        Preset::MatDbMatchRule rule{.N = "1234"}; \
        mat.N = "1234"; \
        REQUIRE(rule.match(mat) == 1); \
        mat.N = "12345"; \
        REQUIRE(rule.match(mat) == 0); \
    }
#define SECTION_MATCH_BRAND_ATTR(N) SECTION("brand_" #N) \
    { \
        Preset::MatDbMatchRule rule{.brand_##N = "1234"}; \
        mat.brand.N = "1234"; \
        REQUIRE(rule.match(mat) == 1); \
        mat.brand.N = "12345"; \
        REQUIRE(rule.match(mat) == 0); \
}


    // OptString uuid;
    // OptString brand_slug;
    // OptString slug;
    // OptString name;
    //
    // OptString klass;
    // OptString type;
    // OptString abbrev;
    // Strings include_tags;
    // Strings exclude_tags;

    SECTION_MATCH_MAT_ATTR(uuid)
    SECTION_MATCH_BRAND_ATTR(slug)
    SECTION_MATCH_MAT_ATTR(slug)
    SECTION_MATCH_MAT_ATTR(name)
    SECTION_MATCH_MAT_ATTR(klass)
    SECTION_MATCH_MAT_ATTR(type)
    SECTION_MATCH_MAT_ATTR(abbreviation)

    SECTION("required_tags")
    {
        mat.tags = {"a", "b"};
        Preset::MatDbMatchRule rule{.required_tags = {"a"}};
        REQUIRE(rule.match(mat) == 1);
        mat.tags = {"c", "d"};
        REQUIRE(rule.match(mat) == 0);
    }

    SECTION("optional_tags")
    {
        mat.tags = {"a", "b"};
        Preset::MatDbMatchRule rule{.optional_tags = {"a", "b", "c"}};
        REQUIRE(rule.match(mat) == 2);
        mat.tags = {"c", "d"};
        REQUIRE(rule.match(mat) == 1);
        mat.tags = {"d"};
        REQUIRE(rule.match(mat) == 0);
    }

    SECTION("exclude_tags")
    {
        mat.tags = {"a", "b"};
        Preset::MatDbMatchRule rule{.optional_tags = {"c"}, .exclude_tags = {"a"}};
        REQUIRE(rule.match(mat) == 0);
        mat.tags = {"c", "d"};
        REQUIRE(rule.match(mat) == 1);
    }

}
