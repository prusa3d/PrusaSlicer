#include <catch2/catch_test_macros.hpp>
#include "Slic3r/Biz/Preset/HwConfigEvaluator.hpp"
#include "Slic3r/Biz/Preset/IO/HwConfigLoader.hpp"
#include "Slic3r/Biz/Yaml/Yaml.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

using namespace Slic3r::Biz::Preset;

TEST_CASE("HwConfigEvaluator", "[preset]")
{
    const std::string filename = Tests::get_datadir().string() + "/presets/hw-config.yaml";
    IO::HwConfigLoader loader;
    try {
        loader.load(filename);
    } catch (const std::runtime_error& e) {
        INFO("Exception while loading yaml from '" << filename << ": " << e.what());
        FAIL(e.what());
    }
    const auto& vendor_data = loader.result();
    const auto& hw_defs     = vendor_data.defs.find(Slic3r::Domain::PrinterTechnology::FFF)->second;
    HwConfigEvaluator cfg_eval;

    SECTION("iterate tools")
    {
        for (const auto& [base_model, model, expected_tool_ids] : {
                 std::make_tuple("MK4", "MK4S", std::vector<std::string>{"0.4"}),
                 std::make_tuple("C1", "C1", std::vector<std::string>{"0.4", "0.4HF"}),
             })
        {
            std::vector<std::string> ids;

            Slic3r::Domain::Preset::HwPrinterConfig config = {
                .model = {.model = model, .base_model = base_model}
            };
            try {
                for (const auto& t : cfg_eval.iterate_tools(config, hw_defs.tools)) {
                    ids.push_back(t.id);
                }
            } catch (const std::runtime_error& e) {
                INFO("Exception: " << e.what());
                FAIL(e.what());
            }

            REQUIRE(ids == expected_tool_ids);
        }
    }

    SECTION("iterate feeders")
    {
        for (const auto& [base_model, model, expected_feeder_id] : {
                 std::make_tuple("MK4", "MK4S", "MMU3S @ MK4"),
                 std::make_tuple("C1", "C1", "MMU3S @ C1"),
             })
        {
            std::vector<std::string> ids;

            Slic3r::Domain::Preset::HwPrinterConfig config = {
                .model      = {.model = model, .base_model = base_model},
                .tool_count = 1,
                .tools = {{.features = {{"nozzle_diameter", 0.4}, {"nozzle_high_flow", false}}}}
            };
            try {
                for (const auto& t :
                     cfg_eval.iterate_feeders(config, config.tools[0], hw_defs.feeders))
                {
                    ids.push_back(t.id);
                }
            } catch (const std::runtime_error& e) {
                INFO("Exception: " << e.what());
                FAIL(e.what());
            }

            REQUIRE(ids == std::vector<std::string>{expected_feeder_id});
        }
    }

    SECTION("empty iterator")
    {
        Slic3r::Domain::Preset::HwPrinterConfig config = {
            .model      = {.model = "MK1", .base_model = "MK1"},
            .tool_count = 1,
            .tools = {{.features = {{"nozzle_diameter", 0.4}, {"nozzle_high_flow", false}}}}
        };
        std::vector<std::string> ids;

        try {
            std::map<std::string, Slic3r::Domain::Preset::HwFeederConfigDef> feeders;
            for (const auto& t :
                 cfg_eval.iterate_feeders(config, config.tools[0], feeders))
            {
                ids.push_back(t.id);
            }
        } catch (const std::runtime_error& e) {
            INFO("Exception: " << e.what());
            FAIL(e.what());
        }

        REQUIRE(ids == std::vector<std::string>{});
    }

    SECTION("Evaluate printer_config templates")
    {
        auto printer_config = cfg_eval.create_printer_config(
            *vendor_data.find_printer_config_template_by_id("MK4S MMU3S"),
            vendor_data
        );
        REQUIRE(printer_config.id.empty() == false);
        REQUIRE(printer_config.printer_id == "MK4S");
        REQUIRE(printer_config.tools.size() == 1);
        REQUIRE(printer_config.tools[0].id == "0.4");
        REQUIRE(std::get<double>(printer_config.tools[0].features["nozzle_diameter"]) == 0.4);
        REQUIRE(std::get<bool>(printer_config.tools[0].features["nozzle_high_flow"]) == false);
        REQUIRE(printer_config.feeders.size() == 1);
        REQUIRE(printer_config.feeders[Slic3r::Domain::Preset::Address{0}].id == "MMU3S @ MK4");
        REQUIRE(printer_config.visual.thumbnail == "MK4S_MMU_thumbnail.png");
        REQUIRE(printer_config.visual.bed_model == "mk4s_bed.stl");
    }
}
