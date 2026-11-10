#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"
#include "../ConnectJsonTestUtils.hpp"
#include <fstream>

#include <catch2/catch_test_macros.hpp>
#include <boost/filesystem/path.hpp>

#include "Slic3r/Biz/Slicing/TestUtils.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

using namespace Slic3r;
std::string read_json(const std::string& file_name)
{
    auto file_path = Tests::get_datadir() / "hw_config_connect" / file_name;
    std::ifstream f{file_path.string()};
    if (!f.is_open()) {
        FAIL("Cannot open file " + file_path.string());
    }
    std::string content ((std::istreambuf_iterator(f)), std::istreambuf_iterator<char>());
    f.close();
    return content;
}

TEST_CASE("Load Connect HW Config Tests", "[preset]")
{

    for (const auto& [test_input, expected] : Biz::Preset::ConnectJsonTestUtils::create_test_desc())
    {
        INFO("Printer file: " << test_input.file_name);
        auto json = read_json(test_input.file_name);
        auto hw_config_result = Biz::PhysicalPrinter::hw_config_from_connect_json(json);

        REQUIRE(hw_config_result.has_value());

        const auto& hw_config = hw_config_result.value();
        REQUIRE(hw_config.model == expected.model);
        REQUIRE(hw_config.tool_count == expected.tool_count);
        REQUIRE(hw_config.tools.size() == expected.tool_count);
        REQUIRE(hw_config.tools.size() == expected.tools.size());
        REQUIRE(hw_config.technology == expected.technology);
        for (size_t i = 0; i < hw_config.tools.size(); ++i) {
            INFO("tool: " << i);
            const auto& exp = expected.tools.at(i);
            const auto& t = hw_config.tools.at(i);

            REQUIRE(t.features == exp.features);
        }

        for (const auto& [addr, feeder] : hw_config.feeders) {
            auto feeder_it = expected.feeders.find(addr);
            REQUIRE(feeder_it != expected.feeders.end());
            REQUIRE(feeder_it->second.model == feeder.model);
        }
    }
}

TEST_CASE("Load Connect HW Config from the slicer API printer list", "[preset]")
{
    SECTION("tools carry type instead of class, feeder slots are addressed 0.N") {
        const std::string json = R"({
            "uuid": "u", "name": "MK4 MMU", "model": "MK4", "base_model": "MK4", "tool_count": 1,
            "tools": {
                "0": {
                    "type": "FFF_PRINT_HEAD",
                    "features": {"nozzle_diameter": 0.4, "nozzle_high_flow": true, "nozzle_hardened": true, "feeder": true},
                    "feeder": {"type": "MMU", "model": "MMU3", "base_model": "MMU3", "num_slots": 5},
                    "material_package_instance": null
                },
                "0.0": {"type": "FFF_FEEDER_SLOT", "material_package_instance": {"package": {"material": {"type": "PLA"}}}},
                "0.1": {"type": "FFF_FEEDER_SLOT", "material_package_instance": {"package": {"material": {"type": "PLA"}}}},
                "0.2": {"type": "FFF_FEEDER_SLOT", "material_package_instance": null},
                "0.3": {"type": "FFF_FEEDER_SLOT", "material_package_instance": {"package": {"material": {"type": "PETG"}}}},
                "0.4": {"type": "FFF_FEEDER_SLOT", "telemetry": {"active": false, "odometer_e": 0}}
            }
        })";
        const auto hw_config = Biz::PhysicalPrinter::hw_config_from_connect_json(json);
        REQUIRE(hw_config.has_value());
        CHECK(hw_config->technology == Domain::PrinterTechnology::FFF);
        CHECK(hw_config->model.model == "MK4");
        CHECK(hw_config->tool_count == 1);
        REQUIRE(hw_config->tools.size() == 1);
        CHECK(
            hw_config->tools[0].features
            == Domain::Preset::FeatureValueMap{
                {"nozzle_diameter", 0.4}, {"nozzle_high_flow", true}, {"nozzle_hardened", true}, {"feeder", true}
            }
        );
        REQUIRE(hw_config->feeders.size() == 1);
        CHECK(hw_config->feeders.begin()->second.model.model == "MMU3");
    }

    SECTION("tool without features") {
        const std::string json = R"({
            "uuid": "u", "name": "MK3.9B", "model": "MK4", "base_model": "MK4", "tool_count": 1,
            "tools": {"0": {"type": "FFF_PRINT_HEAD", "telemetry": {"active": false}, "material_package_instance": null}}
        })";
        const auto hw_config = Biz::PhysicalPrinter::hw_config_from_connect_json(json);
        REQUIRE(hw_config.has_value());
        CHECK(hw_config->technology == Domain::PrinterTechnology::FFF);
        REQUIRE(hw_config->tools.size() == 1);
        CHECK(hw_config->tools[0].features.empty());
    }

    SECTION("empty tools is SLA") {
        const std::string json = R"({"uuid": "u", "model": "SL1", "base_model": "SL1", "tool_count": 1, "tools": {}})";
        const auto hw_config = Biz::PhysicalPrinter::hw_config_from_connect_json(json);
        REQUIRE(hw_config.has_value());
        CHECK(hw_config->technology == Domain::PrinterTechnology::SLA);
    }

    SECTION("printer without model is rejected") {
        const std::string json = R"({
            "uuid": "u", "name": "The Real Next", "connect_state": "OFFLINE", "supported_printer_models": ["TC"],
            "legacy_printer_model": "NEXT",
            "tools": {"0": {"type": "FFF_PRINT_HEAD", "telemetry": {"active": false}, "material_package_instance": null}}
        })";
        CHECK_FALSE(Biz::PhysicalPrinter::hw_config_from_connect_json(json).has_value());
    }

    SECTION("legacy class is still accepted") {
        const std::string json = R"({
            "uuid": "u", "model": "MK4", "base_model": "MK4", "tool_count": 1,
            "tools": {"0": {"class": "FFF", "type": "PRINT_HEAD", "features": {"nozzle_diameter": 0.4}}}
        })";
        const auto hw_config = Biz::PhysicalPrinter::hw_config_from_connect_json(json);
        REQUIRE(hw_config.has_value());
        CHECK(hw_config->technology == Domain::PrinterTechnology::FFF);
    }

    SECTION("tool with a non-string type is skipped") {
        const std::string json = R"({
            "uuid": "u", "model": "MK4", "base_model": "MK4", "tool_count": 2,
            "tools": {"0": {"type": null}, "1": {"type": "FFF_PRINT_HEAD"}}
        })";
        const auto hw_config = Biz::PhysicalPrinter::hw_config_from_connect_json(json);
        REQUIRE(hw_config.has_value());
        CHECK(hw_config->technology == Domain::PrinterTechnology::FFF);
    }
}
