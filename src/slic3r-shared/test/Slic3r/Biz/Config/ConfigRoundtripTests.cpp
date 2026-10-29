
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
using Slic3r::Domain::MacroOverridesLayerFDM;
using Slic3r::Domain::vase_mode_layer_id;

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
    MacroOverridesLayerFDM vase_mode;
    vase_mode.name = "Vase mode";
    vase_mode.params = {
        {"flag", {"Flag", true}},
        {"count", {"Count", 2}},
        {"ratio", {"Ratio", 0.5}},
        {"text", {"Text", std::string{"abc"}}}};
    vase_mode.overrides.overrides.set("spiral_vase", true);
    vase_mode.overrides.overrides.set("thin_walls", false);
    vase_mode.slot_overrides.resize(2);
    vase_mode.slot_overrides.at(1).overrides.set("perimeters", 1);
    config.macro_overrides.insert_or_assign(vase_mode_layer_id(), vase_mode);

    const ordered_json json = Slic3r::Biz::Config::to_json(config);
    REQUIRE(json.contains("macro_overrides"));
    REQUIRE(json["macro_overrides"].contains(vase_mode_layer_id()));
    const ordered_json& layer{json["macro_overrides"][vase_mode_layer_id()]};
    CHECK(layer["name"] == "Vase mode");
    CHECK(layer["params"]["flag"]["label"] == "Flag");
    CHECK(layer["params"]["flag"]["value"] == true);
    CHECK(layer["params"]["count"]["label"] == "Count");
    CHECK(layer["params"]["count"]["value"] == 2);
    CHECK(layer["params"]["ratio"]["value"] == 0.5);
    CHECK(layer["params"]["text"]["value"] == "abc");
    CHECK(layer["overrides"]["spiral_vase"] == true);
    CHECK(layer["overrides"]["thin_walls"] == false);
    CHECK(layer["overrides"].size() == 2);
    REQUIRE(layer["slot_overrides"].is_object());
    CHECK(layer["slot_overrides"].size() == 1);
    CHECK(layer["slot_overrides"]["perimeters"] == ordered_json({nullptr, 1}));
    CHECK(!json.contains("macro_slot_overrides"));

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
    json["macro_overrides"]["some_plugin_layer"]["overrides"]["spiral_vase"] = true;
    json["macro_overrides"]["some_plugin_layer"]["slot_overrides"]["perimeters"] = {nullptr, 1};

    const auto result{load(json, hw_config)};
    REQUIRE(result);
    const LoadResult& load_result{result.value()};
    CHECK(load_result.issues.empty());
    const auto& macro_overrides{std::get<ConfigPackFDM>(load_result.config).macro_overrides};
    REQUIRE(macro_overrides.contains("some_plugin_layer"));
    const MacroOverridesLayerFDM& layer{macro_overrides.at("some_plugin_layer")};
    CHECK(layer.name.empty());
    CHECK(layer.params.empty());
    CHECK(layer.overrides.overrides.get("spiral_vase")->get<bool>() == true);
    REQUIRE(layer.slot_overrides.size() == 2);
    CHECK(layer.slot_overrides.at(0).overrides.overridden_items().empty());
    CHECK(layer.slot_overrides.at(1).overrides.get("perimeters")->get<int>() == 1);
}

TEST_CASE("Macro overrides layer without slot overrides is loaded", "[ConfigRoundtrip]")
{
    const HwPrinterConfig hw_config{.technology = PrinterTechnology::FFF};
    const ConfigPackFDM config;
    ordered_json json = as_boxes(config);
    json["macro_overrides"]["some_plugin_layer"]["overrides"]["spiral_vase"] = true;

    const auto result{load(json, hw_config)};
    REQUIRE(result);
    CHECK(result.value().issues.empty());
    const auto& macro_overrides{std::get<ConfigPackFDM>(result.value().config).macro_overrides};
    REQUIRE(macro_overrides.contains("some_plugin_layer"));
    CHECK(macro_overrides.at("some_plugin_layer").slot_overrides.empty());
}
