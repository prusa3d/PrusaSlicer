
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
