#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "slic3r/Utils/PrinterTools.hpp"

using namespace Slic3r;
using namespace Slic3r::printer_tools_detail;

namespace {

// GET /api/v1/tools from Prusa-Firmware-Buddy v6.9.2-hy.3 (tools_renderer.cpp): docks 1, 2, 3
// and 5 calibrated, 3 empty, 5 loaded without vendor or color.
const std::string FIRMWARE_BODY =
    R"({"tools":{"1":{"loaded":true,"material":"PLA","vendor":"Prusament","color":"Orange","color_hex":"#F8651B","nozzle_diameter":0.40,"hardened":false,"high_flow":false},)"
    R"("2":{"loaded":true,"material":"PETG","vendor":"Polymaker","color":"Black","color_hex":"#000000","nozzle_diameter":0.40,"hardened":false,"high_flow":false},)"
    R"("3":{"loaded":false,"material":"","vendor":"","color":"","color_hex":"","nozzle_diameter":0.40,"hardened":false,"high_flow":false},)"
    R"("5":{"loaded":true,"material":"PA","vendor":"","color":"","color_hex":"","nozzle_diameter":0.60,"hardened":true,"high_flow":true}}})";

PrinterTools parse_ok(const std::string &body)
{
    PrinterTools out;
    std::string  error;
    REQUIRE(parse_tools_json(body, out, error));
    return out;
}

std::string tool_body(const std::string &key, const std::string &fields)
{
    return R"({"tools":{")" + key + R"(":{)" + fields + "}}}";
}

const std::string LOADED = R"("loaded":true,"material":"PETG","nozzle_diameter":0.4)";

} // namespace

TEST_CASE("The firmware's tools body parses with gaps kept", "[PrinterTools]")
{
    const PrinterTools t = parse_ok(FIRMWARE_BODY);
    REQUIRE(t.tools.size() == 4);
    CHECK(t.warnings.empty());

    CHECK(t.tools[0].extruder_idx == 0);
    CHECK(t.tools[0].loaded);
    CHECK(t.tools[0].material == "PLA");
    CHECK(t.tools[0].vendor == "Prusament");
    CHECK(t.tools[0].color == "Orange");
    CHECK(t.tools[0].color_hex == "#F8651B");
    CHECK(t.tools[0].nozzle_diameter == Catch::Approx(0.4));
    CHECK_FALSE(t.tools[0].hardened);
    CHECK_FALSE(t.tools[0].high_flow);

    CHECK(t.tools[2].extruder_idx == 2);
    CHECK_FALSE(t.tools[2].loaded);

    // Dock 4 isn't calibrated: tool 5 is still extruder index 4
    CHECK(t.tools[3].extruder_idx == 4);
    CHECK(t.tools[3].material == "PA");
    CHECK(t.tools[3].vendor.empty());
    CHECK(t.tools[3].color_hex.empty());
    CHECK(t.tools[3].nozzle_diameter == Catch::Approx(0.6));
    CHECK(t.tools[3].hardened);
    CHECK(t.tools[3].high_flow);
}

TEST_CASE("No calibrated dock is an empty list, not an error", "[PrinterTools]")
{
    const PrinterTools t = parse_ok(R"({"tools":{}})");
    CHECK(t.tools.empty());
    CHECK(t.warnings.empty());
}

TEST_CASE("Tools come out in tool order, not key order", "[PrinterTools]")
{
    const PrinterTools t = parse_ok(R"({"tools":{"10":{)" + LOADED + R"(},"2":{)" + LOADED + "}}}");
    REQUIRE(t.tools.size() == 2);
    CHECK(t.tools[0].extruder_idx == 1);
    CHECK(t.tools[1].extruder_idx == 9);
}

TEST_CASE("Unknown keys are ignored", "[PrinterTools]")
{
    const PrinterTools t = parse_ok(R"({"version":2,"tools":{"1":{)" + LOADED + R"(,"spool_id":"x"}}})");
    REQUIRE(t.tools.size() == 1);
    CHECK(t.warnings.empty());
}

TEST_CASE("Keys that aren't tool numbers are skipped with a warning", "[PrinterTools]")
{
    for (const std::string key : {"0", "x", "01", "-1", "+1", "1234", "99999999999", "1.5", ""}) {
        CAPTURE(key);
        const PrinterTools t = parse_ok(tool_body(key, LOADED));
        CHECK(t.tools.empty());
        CHECK(t.warnings.size() == 1);
    }
    // Beyond the INDX's 8 tools is still a tool number; the printer profile decides
    CHECK(parse_ok(tool_body("9", LOADED)).tools.size() == 1);
}

TEST_CASE("Tool entries with wrong types are skipped, the rest kept", "[PrinterTools]")
{
    const std::vector<std::string> bad = {
        R"("loaded":"true","material":"PLA","nozzle_diameter":0.4)",
        R"("material":"PLA","nozzle_diameter":0.4)",
        R"("loaded":true,"material":"PLA","nozzle_diameter":"0.40")",
        R"("loaded":true,"material":"PLA")",
        R"("loaded":true,"material":"PLA","nozzle_diameter":0.4,"vendor":null)",
        R"("loaded":true,"material":"PLA","nozzle_diameter":0.4,"color":7)",
        R"("loaded":true,"material":"PLA","nozzle_diameter":0.4,"color_hex":{})",
        R"("loaded":true,"material":"PLA","nozzle_diameter":0.4,"hardened":"no")",
        R"("loaded":true,"material":"PLA","nozzle_diameter":0.4,"high_flow":1)",
        R"("loaded":true,"material":"","nozzle_diameter":0.4)",
        R"("loaded":true,"nozzle_diameter":0.4)",
    };
    for (const std::string &fields : bad) {
        CAPTURE(fields);
        const PrinterTools t = parse_ok(R"({"tools":{"1":{)" + fields + R"(},"2":{)" + LOADED + "}}}");
        REQUIRE(t.tools.size() == 1);
        CHECK(t.tools[0].extruder_idx == 1);
        CHECK(t.warnings.size() == 1);
    }
    // A tool value that isn't an object
    const PrinterTools t = parse_ok(R"({"tools":{"1":[1,2],"2":{)" + LOADED + "}}}");
    CHECK(t.tools.size() == 1);
    CHECK(t.warnings.size() == 1);
}

TEST_CASE("An unloaded tool needs no material and reports no names", "[PrinterTools]")
{
    const PrinterTools t = parse_ok(tool_body("1", R"("loaded":false,"material":"PLA","vendor":"X","nozzle_diameter":0.4)"));
    REQUIRE(t.tools.size() == 1);
    CHECK_FALSE(t.tools[0].loaded);
    CHECK(t.tools[0].material.empty());
    CHECK(t.tools[0].vendor.empty());
}

TEST_CASE("Colors are normalized to uppercase, bad ones dropped with a warning", "[PrinterTools]")
{
    const auto hex_of = [](const std::string &hex, size_t warnings) {
        const PrinterTools t = parse_ok(tool_body("1", LOADED + R"(,"color":"C","color_hex":")" + hex + R"(")"));
        REQUIRE(t.tools.size() == 1);
        CHECK(t.warnings.size() == warnings);
        return t.tools[0].color_hex;
    };
    CHECK(hex_of("#f8651b", 0) == "#F8651B");
    CHECK(hex_of("#FFA500", 0) == "#FFA500");
    CHECK(hex_of("", 0).empty());
    CHECK(hex_of("#GGGGGG", 1).empty());
    CHECK(hex_of("F8651B", 1).empty());
    CHECK(hex_of("#FFF", 1).empty());
    CHECK(hex_of("#F8651B0", 1).empty());

    CHECK(normalize_color_hex("#00ff7F") == "#00FF7F");
    CHECK(normalize_color_hex(" #00FF7F").empty());
}

TEST_CASE("Bodies that aren't the tools object fail with a reason", "[PrinterTools]")
{
    for (const std::string body : {"", "oops", "<html><body>Login</body></html>", "[1,2]", R"({"tools":[1,2]})",
                                   R"({"tools":"none"})", R"({"tool":{}})", R"({"tools":{"1":{"loaded":tr)", "null"}) {
        CAPTURE(body);
        PrinterTools out;
        out.warnings.push_back("stale");
        std::string  error;
        CHECK_FALSE(parse_tools_json(body, out, error));
        CHECK_FALSE(error.empty());
        CHECK(out.tools.empty());
    }
}

TEST_CASE("Status pages are summarized in one line", "[PrinterTools]")
{
    CHECK(status_page_summary(R"({"title": "404: Not Found","message":""})", 404) == "404: Not Found");
    CHECK(status_page_summary(R"({"title": "500: Internal Server Error","message":"boom"})", 500) == "500: Internal Server Error: boom");
    CHECK(status_page_summary("401: Unauthorized\n\n\n", 401) == "401: Unauthorized");
    CHECK(status_page_summary("\n  503: Service Unavailable  \nretry\n", 503) == "503: Service Unavailable");
    // The firmware cuts pages at 127 bytes, which can leave the JSON unterminated
    CHECK(status_page_summary(R"({"title": "500: Internal Server Error","message":"a very long message that got cu)", 500) ==
          "500: Internal Server Error");
    CHECK(status_page_summary("", 502) == "HTTP 502");
    CHECK(status_page_summary(std::string(300, 'x'), 500).size() == 120);
}

TEST_CASE("Status page summaries stay valid UTF-8 and bounded", "[PrinterTools]")
{
    // A two-byte character straddling the cut
    const std::string accented = std::string(116, 'a') + "\xC3\xAD" + std::string(20, 'b');
    const std::string cut      = status_page_summary(accented, 500);
    CHECK(cut.size() <= 120);
    CHECK(cut.substr(cut.size() - 3) == "...");
    CHECK(cut.find('\xC3') == std::string::npos);

    // Latin-1 is no use to a message box
    CHECK(status_page_summary("Chyba serveru: p\xF8\xEDli\xB9 mnoho", 500) == "HTTP 500");

    // A huge body with an unterminated title: only its start is looked at
    const std::string huge = R"({"title": ")" + std::string(1 << 20, 'x');
    CHECK(status_page_summary(huge, 500).size() <= 120);
}

TEST_CASE("Tools that are listed but unreadable are recorded as such", "[PrinterTools]")
{
    const PrinterTools t = parse_ok(R"({"tools":{"x":{},"3":{"loaded":"yes"},"1":{)" + LOADED + R"(},"2":[1]}})");
    REQUIRE(t.tools.size() == 1);
    CHECK(t.warnings.size() == 3);
    // "x" isn't a tool number, so it isn't among them
    CHECK(t.unreadable == std::vector<int>{1, 2});
}
