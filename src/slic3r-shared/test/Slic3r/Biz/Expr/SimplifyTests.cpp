#include <catch2/catch_test_macros.hpp>
#include <Slic3r/Biz/Expr/Simplify.hpp>
#include <Slic3r/Biz/Expr/Parser.hpp>
#include <boost/variant/get.hpp>
#include <iostream>

TEST_CASE("Simplify tests", "[expr]")
{
    using namespace Slic3r::Domain::Expr;
    using namespace Slic3r::Biz::Expr;

    Parser parser;

    SECTION("Absorb")
    {
        for (const auto source :
             {"d >= 0.2 && d >= 0.2",
              "d >= 0.2 && (hf || d >= 0.2)",
              "d >= 0.2 || d >= 0.2",
              "d >= 0.2 || (hf && d >= 0.2)",
              "d >= 0.2 || (d >= 0.2 && (hf || d >= 0.2))"})
        {
            auto expr = parser.parse(source);
            auto simplified = simplify(expr);
            REQUIRE(equals_to(simplified, parser.parse("d >= 0.2")));
        }
    }

    SECTION("Redundant Logic")
    {
        for (const auto source : {"hf && !!hf", "!!hf || hf"}) {
            auto expr = parser.parse(source);
            auto simplified = simplify(expr);
            REQUIRE(equals_to(simplified, parser.parse("hf")));
        }
    }

    SECTION("Non-Adjacent Duplicates")
    {
        // Tests the deduplication fix. std::unique only caught adjacent duplicates.
        // These tests verify that terms separated by other terms are still properly deduplicated.

        auto expr1 = parser.parse("hf && other_var && hf");
        REQUIRE(equals_to(simplify(expr1), parser.parse("hf && other_var")));

        auto expr2 = parser.parse("hf || other_var || hf");
        REQUIRE(equals_to(simplify(expr2), parser.parse("hf || other_var")));

        // Mixed with a numeric constraint
        auto expr3 = parser.parse("hf && d >= 0.4 && hf");
        // Because of how constraints are grouped, 'hf' is rebuilt first, then 'd >= 0.4'
        REQUIRE(equals_to(simplify(expr3), parser.parse("hf && d >= 0.4")));
    }

    SECTION("Numeric Constraints - Lower Bounds Merging")
    {
        for (const auto source :
             {"d >= 0.2 && d >= 0.4",
              "d >= 0.4 && d >= 0.2",
              "d >= 0.4 && d >= 0.4"})
        {
            auto expr = parser.parse(source);
            auto simplified = simplify(expr);
            REQUIRE(equals_to(simplified, parser.parse("d >= 0.4")));
        }
    }

    SECTION("Numeric Constraints - Upper Bounds Merging")
    {
        for (const auto source :
             {"d <= 0.4 && d <= 0.2",
              "d <= 0.2 && d <= 0.4",
              "d <= 0.2 && d <= 0.2"})
        {
            auto expr = parser.parse(source);
            auto simplified = simplify(expr);
            REQUIRE(equals_to(simplified, parser.parse("d <= 0.2")));
        }
    }

    SECTION("Numeric Constraints - Equality Absorption")
    {
        // When equality is present, it acts as a black hole absorbing redundant bounds and not-equals
        for (const auto source :
             {"d == 0.4 && d != 0.5",
              "d != 0.5 && d == 0.4",
              "d == 0.4 && d > 0.2",
              "d == 0.4 && d < 0.6",
              "d == 0.4 && d >= 0.4",
              "d == 0.4 && d <= 0.4",
              "d == 0.4 && d != 0.5 && d > 0.2 && d < 0.6"})
        {
            auto expr = parser.parse(source);
            auto simplified = simplify(expr);
            REQUIRE(equals_to(simplified, parser.parse("d == 0.4")));
        }
    }

    SECTION("Numeric Constraints - Redundant NotEq Removal")
    {
        // Not-Equal constraints outside the allowed bounds are safely dropped
        for (const auto source :
             {"d < 0.4 && d != 0.5",
              "d != 0.5 && d < 0.4"})
        {
            auto expr = parser.parse(source);
            auto simplified = simplify(expr);
            REQUIRE(equals_to(simplified, parser.parse("d < 0.4")));
        }

        for (const auto source :
             {"d > 0.4 && d != 0.2",
              "d != 0.2 && d > 0.4"})
        {
            auto expr = parser.parse(source);
            auto simplified = simplify(expr);
            REQUIRE(equals_to(simplified, parser.parse("d > 0.4")));
        }
    }

    SECTION("Numeric Constraints - Contradictions (Resolve to false)")
    {
        // Invalid boundaries or direct contradictions should collapse the entire AND chain to 'false'
        for (const auto source :
             {"d > 0.5 && d < 0.2",
              "d >= 0.5 && d <= 0.2",
              "d == 0.4 && d == 0.5",
              "d == 0.4 && d != 0.4",
              "d == 0.4 && d > 0.5",
              "d == 0.4 && d < 0.3",
              "d == 0.4 && d >= 0.5",
              "d == 0.4 && d <= 0.3"})
        {
            auto expr = parser.parse(source);
            auto simplified = simplify(expr);
            REQUIRE(equals_to(simplified, parser.parse("false")));
        }
    }

    SECTION("Complex Real-World Example")
    {
        // Testing combination of flattening, duplication, absorption, and bounds checking
        for (const auto source :
             {"d >= 0.4 && hf && d >= 0.4 && (d >= 0.4 || (hf && d >= 0.3))"})
        {
            auto expr = parser.parse(source);
            auto simplified = simplify(expr);

            REQUIRE(equals_to(simplified, parser.parse("d >= 0.4 && hf")));
        }
    }

    SECTION("String Constraints - Equality Absorption and Contradictions")
    {
        // Absorption: Exact match absorbs redundant inequalities
        for (const auto source :
             {R"(s == "A" && s != "B")",
              R"(s != "B" && s == "A")",
              R"(s == "A" && s == "A")"})
        {
            auto expr = parser.parse(source);
            auto simplified = simplify(expr);
            REQUIRE(equals_to(simplified, parser.parse("s == \"A\"")));
        }

        // Contradictions: Conflicting equalities or inequality to the exact match
        for (const auto source :
             {R"(s == "A" && s == "B")",
              R"(s == "A" && s != "A")",
              R"(s != "A" && s == "A")",
              R"(s == "A" && s == "B" && s != "C")"})
        {
            auto expr = parser.parse(source);
            auto simplified = simplify(expr);
            INFO(to_string(simplified));
            REQUIRE(equals_to(simplified, parser.parse("false")));
        }
    }

    SECTION("Boolean Constraints - Equality Absorption and Contradictions")
    {
        // Absorption
        for (const auto source :
             {"flag == true && flag != false",
              "flag != false && flag == true",
              "flag == true && flag == true"})
        {
            auto expr = parser.parse(source);
            auto simplified = simplify(expr);
            REQUIRE(equals_to(simplified, parser.parse("flag == true")));
        }

        // Contradictions
        for (const auto source :
             {"flag == true && flag == false",
              "flag == true && flag != true",
              "flag != false && flag == false"})
        {
            auto expr = parser.parse(source);
            auto simplified = simplify(expr);
            REQUIRE(equals_to(simplified, parser.parse("false")));
        }
    }

    SECTION("Negated Constraints (Not Unwrapping)")
    {
        // Negated equality acts as NotEq and gets absorbed by an exact Eq match
        for (const auto& [source, expected] :
             std::initializer_list<std::pair<const char*, const char*>>{
                 {"d == 0.4 && not (d == 0.5)", "d == 0.4"},
                 {"not (d == 0.5) && d == 0.4", "d == 0.4"},
                 {R"(s == "A" && not (s == "B"))", "s == \"A\""}
             })
        {
            auto expr       = parser.parse(source);
            auto simplified = simplify(expr);
            REQUIRE(equals_to(simplified, parser.parse(expected)));
        }

        // Negated equality contradicting an exact match resolves to false
        for (const auto source : {"d == 0.4 && not (d == 0.4)", R"(not (s == "A") && s == "A")"}) {
            auto expr       = parser.parse(source);
            auto simplified = simplify(expr);
            REQUIRE(equals_to(simplified, parser.parse("false")));
        }

        // Negated inequalities are inverted (e.g., not (d < 0.4) -> d >= 0.4) and merged
        for (const auto source : {"not (d < 0.4) && d >= 0.5", "d >= 0.5 && not (d < 0.4)"}) {
            auto expr       = parser.parse(source);
            auto simplified = simplify(expr);
            REQUIRE(equals_to(simplified, parser.parse("d >= 0.5")));
        }

        // Complex Real-World Expression
        // The exact match (nozzle_diameter == 0.4) absorbs the redundant negated constraints
        // (not 0.25, not 0.3), while the complex unrelated constraint (feeder) is kept intact.
        auto complex_source =
            R"((((((tool.nozzle_diameter == 0.4) and (printer.model == "COREONE")) and not (tool.nozzle_diameter == 0.25)) and not (tool.nozzle_diameter == 0.3)) and not ((feeder.base_model == "MMU3") and not feeder.single_mode)))";
        auto expected_result =
            R"((tool.nozzle_diameter == 0.4) and (printer.model == "COREONE") and not ((feeder.base_model == "MMU3") and not feeder.single_mode))";

        auto expr       = parser.parse(complex_source);
        auto simplified = simplify(expr);
        REQUIRE(equals_to(simplified, parser.parse(expected_result)));
    }
}
