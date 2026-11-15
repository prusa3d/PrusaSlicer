#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "slic3r/Utils/PrinterTools.hpp"

using namespace Slic3r;
using namespace Slic3r::printer_tools_detail;

namespace {

// GET /slicer/v2/printers for a CORE One INDX (8 docks), shaped as Prusa Connect answered in
// November 2026 (uuids and the name made up). Tools 0 and 1 have a material assigned in Connect's
// Tools page (brand, product name, uuid and the material database's color); tool 2 has only what
// the printer reported (type and the printer's color); tool 3 is empty.
const std::string INDX_BODY = R"({"printers":[{
  "uuid":"00000000-0000-4000-8000-000000000001","name":"My INDX","connect_state":"IDLE",
  "model":"COREONE_INDX","base_model":"COREONE_INDX","tool_count":8,
  "legacy_printer_model":"COREONE_INDX8T","supported_printer_models":["COREONE_INDX8T","COREONE_INDX4T"],
  "tools":{
    "0":{"class":"FFF","type":"PRINT_HEAD","features":{"nozzle_diameter":0.25,"nozzle_high_flow":false,"nozzle_hardened":false},
         "telemetry":{"active":false},
         "material_package_instance":{"class":"FFF","is_from_nfc":false,"dynamic_data":{"consumed_weight":7.9},
           "package":{"class":"FFF","filament_diameter":1750,"nominal_netto_full_weight":1000,
             "material":{"uuid":"00000000-0000-5000-8000-0000000000a0","class":"FFF","type":"PCTG","abbreviation":"PCTG",
               "name":"Pro PCTG, Translucent Cyan","brand":{"name":"3D Fuel","uuid":"3eb597ab-9f9b-5ecf-87e6-8ac1e31f51a8"},
               "primary_color":{"color_rgba":"#0099e6ff"},"secondary_colors":[],"tags":[],"certifications":[],"photos":[]}}}},
    "1":{"class":"FFF","type":"PRINT_HEAD","features":{"nozzle_diameter":0.4,"nozzle_high_flow":true,"nozzle_hardened":true},
         "material_package_instance":{"class":"FFF","is_from_nfc":false,
           "package":{"class":"FFF","material":{"class":"FFF","type":"PETG","abbreviation":"PETG","name":"PETG Jet Black",
             "brand":{"name":"Prusament","uuid":"ae5ff34e-298e-50c9-8f77-92a97fb30b09"},"primary_color":{"color_rgba":"#000000ff"}}}}},
    "2":{"class":"FFF","type":"PRINT_HEAD","features":{"nozzle_diameter":0.4,"nozzle_high_flow":false,"nozzle_hardened":false},
         "material_package_instance":{"class":"FFF","is_from_nfc":false,
           "package":{"class":"FFF","material":{"class":"FFF","type":"PCTG","abbreviation":"PCTG","primary_color":{"color_rgba":"#00ffff"},
             "tags":[],"properties":{"min_print_temperature":260}}}}},
    "3":{"class":"FFF","type":"PRINT_HEAD","features":{"nozzle_diameter":0.4,"nozzle_high_flow":true,"nozzle_hardened":false},
         "material_package_instance":null}
  }}]})";

std::vector<ConnectPrinter> parse_ok(
    const std::string &body, std::vector<std::string> *warnings_out = nullptr
) {
    std::vector<ConnectPrinter> out;
    std::vector<std::string> warnings;
    std::string error;
    REQUIRE(parse_connect_printers_json(body, out, warnings, error));
    if (warnings_out)
        *warnings_out = warnings;
    return out;
}

std::string printer_body(const std::string &tools) {
    return R"({"printers":[{"uuid":"u1","legacy_printer_model":"COREONE_INDX8T","tools":)" + tools +
        "}]}";
}

std::string tool_with_material(const std::string &material) {
    return R"({"0":{"features":{"nozzle_diameter":0.4},"material_package_instance":{"package":{"material":)" +
        material + "}}}}";
}

} // namespace

TEST_CASE(
    "Connect's printer list parses an INDX with assigned and reported materials", "[PrinterTools]"
) {
    std::vector<std::string> warnings;
    const std::vector<ConnectPrinter> printers = parse_ok(INDX_BODY, &warnings);
    CHECK(warnings.empty());
    REQUIRE(printers.size() == 1);
    const ConnectPrinter &p = printers.front();
    CHECK(p.uuid == "00000000-0000-4000-8000-000000000001");
    CHECK(p.name == "My INDX");
    CHECK(p.state == "IDLE");
    CHECK(p.model == "COREONE_INDX");
    CHECK(p.legacy_model == "COREONE_INDX8T");
    CHECK(p.supported_models == std::vector<std::string>{"COREONE_INDX8T", "COREONE_INDX4T"});
    CHECK_FALSE(p.has_feeder_slots);
    CHECK(p.tools.source == "Prusa Connect");
    CHECK(p.tools.warnings.empty());
    CHECK(p.tools.unreadable.empty());
    REQUIRE(p.tools.tools.size() == 4);

    // Assigned in Connect: brand, product and the material database's color, alpha dropped
    const PrinterTool &t0 = p.tools.tools[0];
    CHECK(t0.extruder_idx == 0);
    CHECK(t0.loaded);
    CHECK(t0.material == "PCTG");
    CHECK(t0.vendor == "3D Fuel");
    CHECK(t0.product == "Pro PCTG, Translucent Cyan");
    CHECK(t0.color == "Cyan");
    CHECK(t0.color_hex == "#0099E6");
    CHECK(t0.nozzle_diameter == Catch::Approx(0.25));
    CHECK_FALSE(t0.hardened);
    CHECK_FALSE(t0.high_flow);

    const PrinterTool &t1 = p.tools.tools[1];
    CHECK(t1.vendor == "Prusament");
    CHECK(t1.material == "PETG");
    CHECK(t1.color == "Black");
    CHECK(t1.color_hex == "#000000");
    CHECK(t1.hardened);
    CHECK(t1.high_flow);

    // Only what the printer reported: no vendor or product, the printer's own color, which is
    // one of its palette's and so gets the name the printer gives it over PrusaLink
    const PrinterTool &t2 = p.tools.tools[2];
    CHECK(t2.loaded);
    CHECK(t2.material == "PCTG");
    CHECK(t2.vendor.empty());
    CHECK(t2.product.empty());
    CHECK(t2.color == "Cyan");
    CHECK(t2.color_hex == "#00FFFF");

    // Connect's tools count from 0: tool "3" is extruder index 3
    const PrinterTool &t3 = p.tools.tools[3];
    CHECK(t3.extruder_idx == 3);
    CHECK_FALSE(t3.loaded);
    CHECK(t3.material.empty());
    CHECK(t3.high_flow);
}

TEST_CASE("Connect's printer list may come wrapped in an array", "[PrinterTools]") {
    const std::vector<ConnectPrinter> printers = parse_ok("[" + INDX_BODY + "]");
    REQUIRE(printers.size() == 1);
    CHECK(printers.front().tools.tools.size() == 4);
}

TEST_CASE("A reply without a printers list is an error, an empty list is not", "[PrinterTools]") {
    std::vector<ConnectPrinter> out;
    std::vector<std::string> warnings;
    std::string error;
    CHECK_FALSE(parse_connect_printers_json("not json", out, warnings, error));
    CHECK(error == "the reply isn't JSON");
    for (const char *body :
         {R"({})", R"({"printers":{}})", R"([])", R"([{"printers":[]},{"printers":[]}])", R"("x")"}) {
        error.clear();
        CHECK_FALSE(parse_connect_printers_json(body, out, warnings, error));
        CHECK(error == "the reply has no \"printers\" list");
    }
    CHECK(parse_ok(R"({"printers":[]})").empty());
}

TEST_CASE("Printers without a uuid are skipped with a warning", "[PrinterTools]") {
    std::vector<std::string> warnings;
    const auto printers =
        parse_ok(R"({"printers":[{"name":"No uuid"},7,{"uuid":"u2","tools":{}}]})", &warnings);
    REQUIRE(printers.size() == 1);
    CHECK(printers.front().uuid == "u2");
    CHECK(printers.front().tools.tools.empty());
    CHECK(
        warnings ==
        std::vector<std::string>{"printer 1: no uuid, skipped", "printer 2: not an object, skipped"}
    );
}

TEST_CASE("A legacy printer model sent as a list counts by its first entry", "[PrinterTools]") {
    const auto printers = parse_ok(
        R"({"printers":[{"uuid":"u","legacy_printer_model":["MK4IS","MK4"]}]})"
    );
    REQUIRE(printers.size() == 1);
    CHECK(printers.front().legacy_model == "MK4IS");
}

TEST_CASE("An MMU's slots mark the printer, they aren't read as extruders", "[PrinterTools]") {
    const auto printers = parse_ok(printer_body(
        R"({"0":{"features":{"nozzle_diameter":0.4},"feeder":{"type":"MMU"}},)"
        R"("0.0":{"material_package_instance":{"package":{"material":{"type":"PLA"}}}},"0.1":{}})"
    ));
    REQUIRE(printers.size() == 1);
    CHECK(printers.front().has_feeder_slots);
    REQUIRE(printers.front().tools.tools.size() == 1);
    CHECK(printers.front().tools.tools.front().extruder_idx == 0);
    CHECK(printers.front().tools.warnings.empty());
}

TEST_CASE("Tool entries that can't be read are skipped and marked unreadable", "[PrinterTools]") {
    const auto printers = parse_ok(printer_body(
        R"({"x":{},"01":{},"5":"text","4":{"features":{}},"2":{"features":{"nozzle_diameter":0.4,"nozzle_hardened":"yes"}},)"
        R"("6":{"features":{"nozzle_diameter":0.4},"material_package_instance":{"package":{"material":{"name":"No type"}}}},)"
        R"("1":{"features":{"nozzle_diameter":0.6,"nozzle_high_flow":null}}})"
    ));
    REQUIRE(printers.size() == 1);
    const PrinterTools &t = printers.front().tools;
    REQUIRE(t.tools.size() == 1);
    CHECK(t.tools.front().extruder_idx == 1);
    CHECK(t.tools.front().nozzle_diameter == Catch::Approx(0.6));
    CHECK_FALSE(t.tools.front().high_flow);
    CHECK(t.unreadable == std::vector<int>{2, 4, 5, 6});
    CHECK(t.warnings.size() == 6);
}

TEST_CASE("The type falls back to the abbreviation", "[PrinterTools]") {
    const auto printers = parse_ok(printer_body(tool_with_material(R"({"abbreviation":"ASA"})")));
    REQUIRE(printers.front().tools.tools.size() == 1);
    CHECK(printers.front().tools.tools.front().material == "ASA");
}

TEST_CASE("A color that isn't #RRGGBB or #RRGGBBAA is dropped with a warning", "[PrinterTools]") {
    const auto printers = parse_ok(
        printer_body(tool_with_material(R"({"type":"PLA","primary_color":{"color_rgba":"blue"}})"))
    );
    const PrinterTools &t = printers.front().tools;
    REQUIRE(t.tools.size() == 1);
    CHECK(t.tools.front().color_hex.empty());
    REQUIRE(t.warnings.size() == 1);
    // Named by extruder, from 1, as the preview's rows are
    CHECK(t.warnings.front() == "tool 1: color \"blue\" isn't #RRGGBB or #RRGGBBAA, ignored");
}

TEST_CASE("A printer without tool data has no tools and no warnings", "[PrinterTools]") {
    for (const std::string body :
         {R"({"printers":[{"uuid":"u","tools":{}}]})", R"({"printers":[{"uuid":"u"}]})",
          R"({"printers":[{"uuid":"u","tools":null}]})"}) {
        const auto printers = parse_ok(body);
        REQUIRE(printers.size() == 1);
        CHECK(printers.front().tools.tools.empty());
        CHECK(printers.front().tools.warnings.empty());
        CHECK(printers.front().tools.unreadable.empty());
    }
    const auto printers = parse_ok(R"({"printers":[{"uuid":"u","tools":[]}]})");
    CHECK(
        printers.front().tools.warnings ==
        std::vector<std::string>{"\"tools\" isn't an object, ignored"}
    );
}

TEST_CASE("Connect colors normalize to #RRGGBB", "[PrinterTools]") {
    CHECK(normalize_color_rgba("#0099e6ff") == "#0099E6");
    CHECK(normalize_color_rgba("#00ffff") == "#00FFFF");
    CHECK(normalize_color_rgba("#ABCDEF00") == "#ABCDEF");
    for (const char *bad : {"", "#", "0099e6ff", "#0099e6f", "#0099e6fff", "#0099g6ff", "#12345"})
        CHECK(normalize_color_rgba(bad).empty());
}

TEST_CASE(
    "The printer's color name comes from the product name, else from an exact palette color",
    "[PrinterTools]"
) {
    CHECK(printer_colour("Pro PCTG, Translucent Cyan", "#0099E6") == "Cyan");
    CHECK(printer_colour("PLA Galaxy Grey", "#4A4A4A") == "Gray");
    CHECK(printer_colour("PETG Jet BLACK", "") == "Black");
    CHECK(printer_colour("PLA Azure Blue", "#0080FFFF") == "Blue");
    CHECK(printer_colour("Redwood PLA", "#A0522D").empty());
    // Of several, the one closest to the product's color, whichever comes last
    CHECK(printer_colour("PETG Black Silver Glitter", "#1A1A1A") == "Black");
    CHECK(printer_colour("Silk Dual Red Blue", "#E00010") == "Red");
    CHECK(printer_colour("Silk Dual Red Blue", "#1010E0") == "Blue");
    CHECK(printer_colour("Black Black White", "#F0F0F0") == "White");
    // Without a color code, the last of several
    CHECK(printer_colour("Silver Lining PLA Gold", "") == "Gold");
    // No name, as for a color the printer reported: only an exact palette color has a name
    CHECK(printer_colour("", "#00FFFF") == "Cyan");
    CHECK(printer_colour("", "#f8651b") == "Orange");
    CHECK(printer_colour("", "#000000FF") == "Black");
    CHECK(printer_colour("", "#00FFFE").empty());
    CHECK(printer_colour("PLA Azure", "#FFD700") == "Gold");
    CHECK(printer_colour("", "").empty());
    CHECK(printer_colour("", "cyan").empty());
}

TEST_CASE(
    "A Connect printer fits a profile by its legacy or a supported printer model", "[PrinterTools]"
) {
    ConnectPrinter p;
    p.legacy_model = "COREONE_INDX8T";
    p.supported_models = {"COREONE_INDX4T"};
    CHECK(connect_printer_fits(p, "COREONE_INDX8T"));
    CHECK(connect_printer_fits(p, "coreone_indx8t"));
    CHECK(connect_printer_fits(p, "COREONE_INDX4T"));
    CHECK_FALSE(connect_printer_fits(p, "COREONE"));
    CHECK_FALSE(connect_printer_fits(p, ""));
    p.legacy_model.clear();
    CHECK_FALSE(connect_printer_fits(p, ""));
}

TEST_CASE("Connect's error replies give their message", "[PrinterTools]") {
    CHECK(
        connect_error_summary(
            R"({"message":"Missing or invalid security token","code":"UNAUTHORIZED"})", 401
        ) == "Missing or invalid security token"
    );
    CHECK(connect_error_summary(R"({"message":""})", 500) == "HTTP 500");
    CHECK(connect_error_summary("<html>Bad gateway</html>", 502) == "HTTP 502");
    const std::string long_message = std::string(200, 'x');
    CHECK(connect_error_summary(R"({"message":")" + long_message + R"("})", 400).size() == 120);
}
