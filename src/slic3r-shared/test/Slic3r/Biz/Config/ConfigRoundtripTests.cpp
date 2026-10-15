
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Slic3r/Biz/Config/ConfigLoad.hpp"
#include "Slic3r/Biz/Config/ConfigSerialize.hpp"
#include "Slic3r/Domain/FullConfigFDM.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include <iostream>
#include <nlohmann/json.hpp>

using Slic3r::Domain::ConfigPackFDM;
using Slic3r::Domain::ConfigPackSLA;
using nlohmann::ordered_json;
using Slic3r::Domain::as_boxes;
using Slic3r::Biz::Config::load;
using Slic3r::Biz::Config::LoadResult;
using Slic3r::Domain::Preset::HwPrinterConfig;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::MacroOverridesFDM;
using Slic3r::Domain::vase_mode_layer;

TEST_CASE("Default fdm config pack roundtrip", "[ConfigRoundtrip]")
{
    const HwPrinterConfig hw_config{.technology = PrinterTechnology::FFF};
    const ConfigPackFDM config;
    const ordered_json json = as_boxes(config);
    const auto result{load(json, hw_config)};
    REQUIRE(result);
    const LoadResult& load_result{result.value()};
    const auto& result_config_pack{std::get<ConfigPackFDM>(load_result.config)};
    CHECK(load_result.issues.empty());
    CHECK(config == result_config_pack);
}

TEST_CASE("Default sla config pack roundtrip", "[ConfigRoundtrip]")
{
    const HwPrinterConfig hw_config{.technology = PrinterTechnology::SLA};
    const ConfigPackSLA config;
    const ordered_json json = as_boxes(config);
    CHECK(json.contains("sla_project_settings"));
    const auto result{load(json, hw_config)};
    REQUIRE(result);
    const LoadResult& load_result{result.value()};
    const auto& result_config_pack{std::get<ConfigPackSLA>(load_result.config)};
    CHECK(load_result.issues.empty());
    CHECK(config == result_config_pack);
}

TEST_CASE("Fdm config pack with macro overrides roundtrip", "[ConfigRoundtrip]")
{
    const HwPrinterConfig hw_config{.technology = PrinterTechnology::FFF};
    ConfigPackFDM config;
    MacroOverridesFDM vase_mode;
    vase_mode.overrides.set("perimeters", 1);
    vase_mode.overrides.set("thin_walls", false);
    config.macro_overrides.insert_or_assign(vase_mode_layer(), vase_mode);

    const ordered_json json = as_boxes(config);
    const std::string layer_id{vase_mode_layer().id};
    REQUIRE(json.contains("macro_overrides"));
    REQUIRE(json["macro_overrides"].contains(layer_id));
    CHECK(json["macro_overrides"][layer_id]["perimeters"] == 1);
    CHECK(json["macro_overrides"][layer_id]["thin_walls"] == false);
    CHECK(json["macro_overrides"][layer_id].size() == 2);

    const auto result{load(json, hw_config)};
    REQUIRE(result);
    const LoadResult& load_result{result.value()};
    const auto& result_config_pack{std::get<ConfigPackFDM>(load_result.config)};
    CHECK(load_result.issues.empty());
    CHECK(config == result_config_pack);
}

TEST_CASE("Any macro overrides layer is loaded", "[ConfigRoundtrip]")
{
    const HwPrinterConfig hw_config{.technology = PrinterTechnology::FFF};
    const ConfigPackFDM config;
    ordered_json json = as_boxes(config);
    json["macro_overrides"]["some_plugin_layer"]["perimeters"] = 1;

    const auto result{load(json, hw_config)};
    REQUIRE(result);
    const LoadResult& load_result{result.value()};
    CHECK(load_result.issues.empty());
    const auto& macro_overrides{std::get<ConfigPackFDM>(load_result.config).macro_overrides};
    REQUIRE(macro_overrides.contains(Slic3r::Domain::MacroOverridesLayer{"some_plugin_layer"}));
    CHECK(macro_overrides.begin()->second.overrides.get("perimeters")->get<int>() == 1);
}
