#include <catch2/catch_test_macros.hpp>
#include "libslic3r/CustomParametersHandling.hpp"
#include "libslic3r/PlaceholderParser.hpp"

using namespace Slic3r;

TEST_CASE("CustomParametersHandling tests", "[CustomParametersHandling]")
{
    SECTION("Valid simple values") {
        std::string json_str = R"({
            "string_param": "hello",
            "int_param": 42,
            "double_param": 3.14,
            "bool_param": true,
            "null_param": null
        })";
        auto result = parse_custom_parameters(json_str);
        REQUIRE(result.has_value());
        auto& values = *result;
        REQUIRE(values.size() == 5);
        REQUIRE(std::get<std::string>(values["string_param"]) == "hello");
        REQUIRE(std::get<int>(values["int_param"]) == 42);
        REQUIRE(std::get<double>(values["double_param"]) == 3.14);
        REQUIRE(std::get<bool>(values["bool_param"]) == true);
        REQUIRE(std::holds_alternative<std::monostate>(values["null_param"]));
    }

    SECTION("Invalid JSON") {
        std::string json_str = R"({ "param": "value" )"; // Incomplete
        auto result = parse_custom_parameters(json_str);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Not a JSON object") {
        std::string json_str = R"([1, 2, 3])";
        auto result = parse_custom_parameters(json_str);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Array") {
        std::string json_str = R"({ "mixed_vec": ["a", 1, true] })";
        auto result = parse_custom_parameters(json_str);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Empty array") {
        std::string json_str = R"({ "empty_vec": [] })";
        auto result = parse_custom_parameters(json_str);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Unsupported type") {
        std::string json_str = R"({ "object_param": { "a": 1 } })";
        auto result = parse_custom_parameters(json_str);
        REQUIRE_FALSE(result.has_value());
    }
    
    SECTION("Empty input") {
        std::string json_str = "";
        auto result = parse_custom_parameters(json_str);
        REQUIRE(result.has_value());
        REQUIRE(result->empty());
    }

    SECTION("Empty JSON object") {
        std::string json_str = "{}";
        auto result = parse_custom_parameters(json_str);
        REQUIRE(result.has_value());
        REQUIRE(result->empty());
    }
}



TEST_CASE("CustomParametersHandling - validation", "[CustomParametersHandling]") {
    std::string cp_print;
    std::string cp_printer;
    std::vector<std::string> cp_filaments;

    SECTION("Filament parameters mismatch") {
        cp_filaments = {
            "{ \"key1\": 5}",
            "{ \"key1\": \"text\"}"
        };
        REQUIRE_FALSE(check_custom_parameters(cp_print, cp_printer, cp_filaments));
    }
}



TEST_CASE("CustomParametersHandling - placeholder parser", "[CustomParametersHandling]") {
    
    std::string cp_print =
      "{"
        "\"key1\": \"first_value\","
        "\"key2\": null"
       "}";
    std::string cp_printer = "{ \"key1\": 5.3 }";
    std::vector<std::string> cp_filaments = {
        "{ \"key1\": 1}",
        "{ \"key1\": 2, \"key2\": 1, \"key3\": 8.7, \"key4\": false, \"key5\": \"str\"}"
    };

    PlaceholderParser parser;
    parser.apply_config(std::move(parse_custom_parameters_to_dynamic_config(cp_print, cp_printer, cp_filaments)));

    REQUIRE(parser.process("{custom_parameter_print_key1}") == "first_value");
    REQUIRE(parser.process("{custom_parameter_printer_key1}") == "5.3");
    REQUIRE(parser.process("{custom_parameter_filament_key1[0]}") == "1");
    REQUIRE(parser.process("{custom_parameter_filament_key1[1]}") == "2");
    REQUIRE(parser.process("{custom_parameter_filament_key2[1]}") == "1");
    REQUIRE(parser.process("{custom_parameter_filament_key5[0]}") == "");

    REQUIRE_THROWS(parser.process("{custom_parameter_print_key2}"));
    REQUIRE_THROWS(parser.process("{custom_parameter_filament_key2[0]}"));
    REQUIRE_THROWS(parser.process("{custom_parameter_filament_key3[0]}"));
    REQUIRE_THROWS(parser.process("{custom_parameter_filament_key4[0]}"));
}



TEST_CASE("Custom parameters merging", "[CustomParametersHandling]")
{
    SECTION("Merge two non-empty JSON strings") {
        std::string bottom = R"({"a": 1, "b": 2})";
        std::string top = R"({"b": 3, "c": 4})";
        std::string merged = merge_json(bottom, top);
        REQUIRE(merged == R"({"a":1,"b":3,"c":4})");
    }

    SECTION("Merge with empty bottom JSON") {
        std::string bottom = "";
        std::string top = R"({"a": 1})";
        std::string merged = merge_json(bottom, top);
        REQUIRE(merged == top);
    }

    SECTION("Merge with empty top JSON") {
        std::string bottom = R"({"a": 1})";
        std::string top = "";
        std::string merged = merge_json(bottom, top);
        REQUIRE(merged == bottom);
    }

    SECTION("Merge with invalid JSON") {
        std::string bottom = R"({"a": 1})";
        std::string top = "invalid json";
        std::string merged = merge_json(bottom, top);
        REQUIRE(merged == top);
    }
}
