#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Slic3r/Biz/Config/ConfigLoad.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include <nlohmann/json.hpp>

using nlohmann::ordered_json;
using Slic3r::Biz::Config::BoxIssues;
using Slic3r::Biz::Config::GlobalParsingIssue;
using Slic3r::Biz::Config::IssuesPerLocation;
using Slic3r::Biz::Config::ItemParsingIssueType;
using Slic3r::Biz::Config::load;
using Slic3r::Biz::Config::LocationIssues;
using Slic3r::Domain::ConfigLocation;
using Slic3r::Domain::ConfigPackFDM;
using Slic3r::Domain::ConfigPackSLA;
using Slic3r::Domain::FDMConfigLocation;
using Slic3r::Domain::overloaded;
using Slic3r::Domain::SLAConfigLocation;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::FloatOrPercentage;
using Slic3r::Domain::Percentage;

std::optional<ItemParsingIssueType> get_issue(
    const IssuesPerLocation& issues,
    const ConfigLocation& location,
    const std::string& key,
    const std::optional<std::size_t>& index = std::nullopt
)
{
    if (!issues.contains(location)) {
        return std::nullopt;
    }

    const LocationIssues& location_issues{issues.at(location)};
    return std::visit(
        overloaded{
            [&](const std::vector<BoxIssues>& boxes_issues) -> std::optional<ItemParsingIssueType> {
                ASSERT(index && index < boxes_issues.size());
                if (boxes_issues[*index].contains(key)) {
                    return boxes_issues[*index].at(key).type;
                }
                return std::nullopt;
            },
            [&](const BoxIssues& box_issues) -> std::optional<ItemParsingIssueType> {
                ASSERT(!index);
                if (box_issues.contains(key)) {
                    return box_issues.at(key).type;
                }
                return std::nullopt;
            },
        },
        location_issues
    );
}

struct ConfigLoadFDMFixture
{
    ordered_json json{
        {"printer_settings",
         ordered_json::object({
             {"printer_technology", "FFF"},
         })},
        {"toolprint_settings", ordered_json::object()},
        {"print_settings", ordered_json::object()},
        {"filament_settings", ordered_json::object()},
        {"project_settings", ordered_json::object()},
    };
};

struct ConfigLoadSLAFixture
{
    ordered_json json{
        {"sla_printer_settings",
         ordered_json::object({
             {"printer_technology", "SLA"},
         })},
        {"sla_material_settings", ordered_json::object()},
        {"sla_print_settings", ordered_json::object()},
    };
};

const Slic3r::Domain::Preset::HwPrinterConfig fdm_hw_config{
    .technology = Slic3r::Domain::PrinterTechnology::FFF,
    .tool_count = 1
};
const Slic3r::Domain::Preset::HwPrinterConfig sla_hw_config{
    .technology = Slic3r::Domain::PrinterTechnology::SLA,
    .tool_count = 0
};

TEST_CASE("Loading fails if provided json is not an object", "[ConfigLoad]")
{
    const ordered_json json = nlohmann::json::array();

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(!result.has_value());
    CHECK(result.error() == GlobalParsingIssue::NotAJsonObject);
}

TEST_CASE("Loading fails if the printer technology is not present", "[ConfigLoad]")
{
    const ordered_json json = nlohmann::json::object();

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(!result.has_value());
    CHECK(result.error() == GlobalParsingIssue::UnableToDeducePrinterTechnology);
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Loading fails if printer_settings is not an object", "[ConfigLoad]")
{
    json["print_settings"] = 10;
    const auto result{load(json, fdm_hw_config)};
    REQUIRE(!result.has_value());
    CHECK(result.error() == GlobalParsingIssue::InvalidFDMPrintSettings);
}

TEST_CASE_METHOD(
    ConfigLoadFDMFixture,
    "Loading fails if toolprint_settings count is not equal to filament_settings count",
    "[ConfigLoad]"
)
{
    json["toolprint_settings"] = {{"extruder_colour", ordered_json::array({"red", "blue", "green"})}};

    json["filament_settings"] = {{"filament_colour", ordered_json::array({"red", "blue"})}};
    const auto result{load(json, fdm_hw_config)};
    REQUIRE(!result.has_value());
    CHECK(result.error() == GlobalParsingIssue::FilamentsAndToolsCountIsNotEqual);
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Extra not-loaded values are reported", "[ConfigLoad]")
{
    json["print_settings"]["invalid_key"] = 10;

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    CHECK(
        get_issue(result->issues, FDMConfigLocation::Print, "invalid_key") == ItemParsingIssueType::ExtraKey
    );
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Missing values are reported", "[ConfigLoad]")
{
    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    CHECK(
        get_issue(result->issues, FDMConfigLocation::Print, "raft_layers") == ItemParsingIssueType::NotFound
    );
    CHECK(
        get_issue(result->issues, FDMConfigLocation::Printer, "silent_mode") == ItemParsingIssueType::NotFound
    );
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Loading int works", "[ConfigLoad]")
{
    json["print_settings"]["raft_layers"] = 10;

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    const auto result_config{std::get<ConfigPackFDM>(result->config)};
    CHECK(result_config.print.items.opt("raft_layers").get<int>() == 10);
    CHECK(!get_issue(result->issues, FDMConfigLocation::Print, "raft_layers"));
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Loading double works", "[ConfigLoad]")
{
    json["print_settings"]["brim_width"] = 5;

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    const auto result_config{std::get<ConfigPackFDM>(result->config)};
    CHECK(result_config.print.items.opt("brim_width").get<double>() == 5);
    CHECK(!get_issue(result->issues, FDMConfigLocation::Print, "brim_width"));
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Loading bool works", "[ConfigLoad]")
{
    json["print_settings"]["extra_perimeters"] = true;

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    const auto result_config{std::get<ConfigPackFDM>(result->config)};
    CHECK(result_config.print.items.opt("extra_perimeters").get<bool>() == true);
    CHECK(!get_issue(result->issues, FDMConfigLocation::Print, "extra_perimeters"));
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Loading Vec2d works", "[ConfigLoad]")
{
    json["printer_settings"]["extruder_offset"] = ordered_json::array({ordered_json::array({2, 5.1})});

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    const auto result_config{std::get<ConfigPackFDM>(result->config)};
    CHECK(result_config.printer.items.opt("extruder_offset").get<std::vector<Vec2d>>() == std::vector{Vec2d{2, 5.1}});
    CHECK(!get_issue(result->issues, FDMConfigLocation::Printer, "extruder_offset"));
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Loading std::vector<Vec2d> works", "[ConfigLoad]")
{
    json["printer_settings"]["bed_shape"] = ordered_json::array(
        {ordered_json::array({2, 5.1}), ordered_json::array({3.2, 4})}
    );

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    const auto result_config{std::get<ConfigPackFDM>(result->config)};
    CHECK(
        result_config.printer.items.opt("bed_shape").get<std::vector<Vec2d>>()
        == std::vector{Vec2d{2, 5.1}, Vec2d{3.2, 4}}
    );
    CHECK(!get_issue(result->issues, FDMConfigLocation::Printer, "bed_shape"));
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Loading Percentage works", "[ConfigLoad]")
{
    json["print_settings"]["ironing_flowrate"] = {
        {"value", 10.2},
        {"is_percent", true},
    };

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    const auto result_config{std::get<ConfigPackFDM>(result->config)};
    CHECK(result_config.print.items.opt("ironing_flowrate").get<Percentage>() == Percentage{10.2});
    CHECK(!get_issue(result->issues, FDMConfigLocation::Print, "ironing_flowrate"));
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Loading Percentage fails if is_percent is false", "[ConfigLoad]")
{
    json["print_settings"]["ironing_flowrate"] = {
        {"value", 10.2},
        {"is_percent", false},
    };

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    const auto result_config{std::get<ConfigPackFDM>(result->config)};
    CHECK(
        get_issue(result->issues, FDMConfigLocation::Print, "ironing_flowrate")
        == ItemParsingIssueType::InvalidFormat
    );
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Loading FloatOrPercentage works", "[ConfigLoad]")
{
    json["print_settings"]["first_layer_height"] = {
        {"value", 1},
        {"is_percent", false},
    };

    json["print_settings"]["seam_gap_distance"] = {
        {"value", 23.1},
        {"is_percent", true},
    };

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    const auto result_config{std::get<ConfigPackFDM>(result->config)};
    const auto first_layer_height{
        result_config.print.items.opt("first_layer_height").get<FloatOrPercentage>()
    };
    const auto seam_gap_distance{
        result_config.print.items.opt("seam_gap_distance").get<FloatOrPercentage>()
    };

    CHECK(!first_layer_height.is_percentage());
    CHECK(first_layer_height.float_value() == 1.0);
    CHECK(seam_gap_distance.is_percentage());
    CHECK(seam_gap_distance.percentage() == Percentage{23.1});
    CHECK(!get_issue(result->issues, FDMConfigLocation::Print, "first_layer_height"));
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Loading std::optional<int> works", "[ConfigLoad]")
{
    json["filament_settings"]["idle_temperature"] = ordered_json::array({32});

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    const auto result_config{std::get<ConfigPackFDM>(result->config)};
    CHECK(result_config.filament.at(0).items.opt("idle_temperature").get<std::optional<int>>() == 32);
    CHECK(!get_issue(result->issues, FDMConfigLocation::Filament, "idle_temperature", 0));
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Loading std::optional<int> from null works", "[ConfigLoad]")
{
    json["filament_settings"]["idle_temperature"] = ordered_json::array({nullptr});

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    const auto result_config{std::get<ConfigPackFDM>(result->config)};
    CHECK(!result_config.filament.at(0).items.opt("idle_temperature").get<std::optional<int>>());
    CHECK(!get_issue(result->issues, FDMConfigLocation::Filament, "idle_temperature", 0));
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Loading int from other type reports an issue", "[ConfigLoad]")
{
    json["print_settings"]["raft_layers"] = "invalid";

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    const auto result_config{std::get<ConfigPackFDM>(result->config)};

    const ConfigPackFDM default_config;
    CHECK(
        result_config.print.items.opt("raft_layers").get<int>()
        == default_config.print.items.opt("raft_layers").get<int>()
    );
    const auto issue{get_issue(result->issues, FDMConfigLocation::Print, "raft_layers")};
    CHECK(issue == ItemParsingIssueType::InvalidFormat);
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Loading enum from string works", "[ConfigLoad]")
{
    json["print_settings"]["arc_fitting"] = "emit_center";

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    const auto result_config{std::get<ConfigPackFDM>(result->config)};

    using Slic3r::Domain::ArcFittingType;
    CHECK(
        result_config.print.items.opt("arc_fitting").get<ArcFittingType>() == ArcFittingType::EmitCenter
    );
    CHECK(!get_issue(result->issues, FDMConfigLocation::Print, "arc_fitting"));
}

TEST_CASE_METHOD(
    ConfigLoadFDMFixture,
    "Loading enum fails if the string does not correspond to an enum option",
    "[ConfigLoad]"
)
{
    const ConfigPackFDM default_config;
    json["print_settings"]["arc_fitting"] = "invalid";

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    const auto result_config{std::get<ConfigPackFDM>(result->config)};

    using Slic3r::Domain::ArcFittingType;
    CHECK(
        result_config.print.items.opt("arc_fitting").get<ArcFittingType>()
        == default_config.print.items.opt("arc_fitting").get<ArcFittingType>()
    );
    CHECK(
        get_issue(result->issues, FDMConfigLocation::Print, "arc_fitting")
        == ItemParsingIssueType::InvalidFormat
    );
}

TEST_CASE_METHOD(ConfigLoadSLAFixture, "Loading enum vector from vector of strings works", "[ConfigLoad]")
{
    json["sla_material_settings"]["tower_speed"] = ordered_json::array({"layer1", "layer2"});

    const auto result{load(json, sla_hw_config)};
    REQUIRE(result.has_value());
    const auto result_config{std::get<ConfigPackSLA>(result->config)};

    using Slic3r::Domain::TowerSpeeds;
    const auto& material_settings{result_config.sla_material_settings.items};
    CHECK(
        material_settings.opt("tower_speed").get<std::vector<TowerSpeeds>>()
        == std::vector{TowerSpeeds::tsLayer1, TowerSpeeds::tsLayer2}
    );
    CHECK(!get_issue(result->issues, FDMConfigLocation::Print, "tower_speed"));
}

TEST_CASE_METHOD(
    ConfigLoadSLAFixture,
    "Loading enum vector from vector of strings fails if any of the strings is invalid",
    "[ConfigLoad]"
)
{
    const ConfigPackSLA default_config;
    json["sla_material_settings"]["tower_speed"] = ordered_json::array({"layer1", "typo"});

    const auto result{load(json, sla_hw_config)};
    REQUIRE(result.has_value());
    const auto result_config{std::get<ConfigPackSLA>(result->config)};

    using Slic3r::Domain::TowerSpeeds;
    const auto& material_settings{result_config.sla_material_settings.items};
    const auto& default_material_settings{default_config.sla_material_settings.items};
    CHECK(
        material_settings.opt("tower_speed").get<std::vector<TowerSpeeds>>()
        == default_material_settings.opt("tower_speed").get<std::vector<TowerSpeeds>>()
    );
    CHECK(
        get_issue(result->issues, SLAConfigLocation::Material, "tower_speed")
        == ItemParsingIssueType::InvalidFormat
    );
}

TEST_CASE_METHOD(
    ConfigLoadFDMFixture,
    "Tool count is determined based on the hw config",
    "[ConfigLoad]"
)
{
    json["filament_settings"] = {
        {"filament_colour", ordered_json::array({"red", "blue", "green"})},
        {"temperature", ordered_json::array({100, 200})},
        {"extrusion_multiplier", ordered_json::array({1.1, 1.2, 1.3})},
    };

    for (uint8_t tool_count : {1, 3}) {
        auto hw_config = fdm_hw_config;
        hw_config.tool_count = tool_count;
        const auto result{load(json, hw_config)};
        REQUIRE(result.has_value());
        const auto result_config{std::get<ConfigPackFDM>(result->config)};
        REQUIRE(result_config.tool.size() == tool_count);
        CHECK(result_config.filament.size() == 3);
        CHECK(
            get_issue(result->issues, FDMConfigLocation::Filament, "temperature", 2)
            == ItemParsingIssueType::NotFound
        );

        const ConfigPackFDM default_config;
        CHECK(
            result_config.filament[2].items.opt("temperature").get<int>()
            == default_config.filament.front().items.opt("temperature").get<int>()
        );
    }
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Per filament not-loaded values are reported per filament", "[ConfigLoad]")
{
    json["toolprint_settings"] = {
        {"retract_length", ordered_json::array({1.4})},
    };
    json["filament_settings"] = {{"invalid_key", ordered_json::array({"invalid"})}};

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    CHECK(
        get_issue(result->issues, FDMConfigLocation::Filament, "invalid_key", 0)
        == ItemParsingIssueType::ExtraKey
    );
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Loading double per tool works", "[ConfigLoad]")
{
    json["toolprint_settings"] = {
        {"retract_length", ordered_json::array({1.4, 1.5, 1.6})},
    };
    json["filament_settings"] = {{"filament_colour", ordered_json::array({"red", "blue", "green"})}};

    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    const auto result_config{std::get<ConfigPackFDM>(result->config)};
    REQUIRE(result_config.tool.size() == 3);

    CHECK(result_config.tool.at(0).overrides.get("retract_length")->get<double>() == Catch::Approx(1.4));
    CHECK(result_config.tool.at(1).overrides.get("retract_length")->get<double>() == Catch::Approx(1.5));
    CHECK(result_config.tool.at(2).overrides.get("retract_length")->get<double>() == Catch::Approx(1.6));
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Missing overrides are not reported in issues", "[ConfigLoad]")
{
    const auto result{load(json, fdm_hw_config)};
    REQUIRE(result.has_value());
    const auto result_config{std::get<ConfigPackFDM>(result->config)};
    CHECK(!get_issue(result->issues, FDMConfigLocation::Filament, "retract_layer_change", 0));
    CHECK(!result_config.filament.at(0).overrides.get("retract_layer_change"));

    json["filament_settings"] = {{"retract_layer_change", ordered_json::array({true})}};
    const auto new_result{load(json, fdm_hw_config)};

    const auto new_result_config{std::get<ConfigPackFDM>(new_result->config)};
    CHECK(!get_issue(new_result->issues, FDMConfigLocation::Filament, "retract_layer_change", 0));
    const auto result_item{new_result_config.filament.at(0).overrides.get("retract_layer_change")};
    REQUIRE(result_item);
    CHECK(result_item->get<bool>() == true);
}

TEST_CASE_METHOD(ConfigLoadFDMFixture, "Overrides can be set only partially", "[ConfigLoad]")
{
    json["toolprint_settings"] = {
        {"retract_length", ordered_json::array({1.4, 1.5})},
    };
    json["filament_settings"] = {{"retract_layer_change", ordered_json::array({nullptr, true})}};
    const auto result{load(json, fdm_hw_config)};
    const auto result_config{std::get<ConfigPackFDM>(result->config)};
    CHECK(!get_issue(result->issues, FDMConfigLocation::Filament, "retract_layer_change", 0));
    CHECK(!get_issue(result->issues, FDMConfigLocation::Filament, "retract_layer_change", 1));
    const auto first_item{result_config.filament.at(0).overrides.get("retract_layer_change")};
    const auto second_item{result_config.filament.at(1).overrides.get("retract_layer_change")};
    CHECK(!first_item);
    REQUIRE(second_item);
    CHECK(second_item->get<bool>() == true);
}
