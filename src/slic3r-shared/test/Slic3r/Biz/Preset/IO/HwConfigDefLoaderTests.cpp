#include <iostream>
#include <catch2/catch_test_macros.hpp>
#include "Slic3r/TestUtils/TestData.hpp"
#include "Slic3r/Biz/Preset/IO/HwConfigLoader.hpp"
#include "Slic3r/Biz/Yaml/Yaml.hpp"

TEST_CASE("Load HW Config", "[preset]")
{
    using namespace Slic3r::Domain;
    using namespace Slic3r::Biz::Preset;
    namespace Yaml = Slic3r::Biz::Yaml;

    const std::string filename = Tests::get_datadir().string() + "/presets/hw-config.yaml";
    IO::HwConfigLoader loader;
    try {
        loader.load(filename);
    } catch (const Yaml::ParseError & e) {
        std::cout << e.what() << std::endl;
        FAIL_CHECK(e.what());
    }
    auto& result = loader.result();
    auto& fff_hw_defs = result.defs[PrinterTechnology::FFF];
    REQUIRE(fff_hw_defs.technology == PrinterTechnology::FFF);
    REQUIRE(fff_hw_defs.printers.size() == 2);
    REQUIRE(fff_hw_defs.tools.size() == 2);
    REQUIRE(fff_hw_defs.feeders.size() == 2);
    REQUIRE(result.printer_configs.size() == 4);
    REQUIRE(result.printer_configs[1].feeders.size() == 1);
    REQUIRE(result.printer_configs[1].feeders[0].address == std::vector<uint8_t>{0});
}