#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/ConnectPrinter/ConnectPrinter.hpp"

#include <nlohmann/json.hpp>

using namespace Slic3r::Biz::ConnectPrinter;

TEST_CASE("Connect printer list: envelope and element handling", "[ConnectPrinter]")
{
    SECTION("fields parse from the printers envelope") {
        const auto printers = parse_connect_printer_list(
            R"({"printers":[{"uuid":"a","name":"My printer","model":"MK4S","base_model":"MK4","connect_state":"IDLE","tool_count":1}]})"
        );
        REQUIRE(printers);
        REQUIRE(printers->size() == 1);
        const ConnectPrinter& printer = (*printers)[0];
        CHECK(printer.uuid == "a");
        CHECK(printer.name == "My printer");
        CHECK(printer.model == "MK4S");
        CHECK(printer.base_model == "MK4");
        CHECK(printer.state == ConnectPrinterState::Idle);
        CHECK(nlohmann::json::parse(printer.json)["tool_count"] == 1);
    }

    SECTION("elements without uuid are skipped") {
        const auto printers = parse_connect_printer_list(
            R"({"printers":[{"name":"x"},{"printer_uuid":"p-1"},{"uuid":"b"},3]})"
        );
        REQUIRE(printers);
        REQUIRE(printers->size() == 1);
        CHECK((*printers)[0].uuid == "b");
    }

    SECTION("empty printers array is an empty list, not a failure") {
        const auto printers = parse_connect_printer_list(R"({"printers":[]})");
        REQUIRE(printers);
        CHECK(printers->empty());
    }

    SECTION("not JSON") {
        CHECK_FALSE(parse_connect_printer_list("not json"));
        CHECK_FALSE(extract_connect_printers_array("not json"));
    }

    SECTION("other shapes are rejected") {
        CHECK_FALSE(parse_connect_printer_list(R"({"foo":1})"));
        CHECK_FALSE(extract_connect_printers_array(R"({"printers":{"uuid":"a"}})"));
        CHECK_FALSE(extract_connect_printers_array(R"([{"uuid":"a"}])"));
        CHECK_FALSE(extract_connect_printers_array(R"([{"printers":[{"uuid":"a"}]}])"));
        CHECK_FALSE(extract_connect_printers_array(R"({"items":[{"uuid":"a"}]})"));
    }

    SECTION("extract returns the printers array") {
        CHECK(extract_connect_printers_array(R"({"printers":[1,2]})")->size() == 2);
    }
}
