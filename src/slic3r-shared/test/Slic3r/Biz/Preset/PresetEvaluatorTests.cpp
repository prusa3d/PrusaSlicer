#include <iostream>
#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/Preset/PresetEvaluator.hpp"
#include "Slic3r/Biz/Preset/IO/PresetLoader.hpp"
#include "Slic3r/Biz/Yaml/Yaml.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

TEST_CASE("Preset Evaluator", "[preset]")
{
    using namespace Slic3r::Domain::Preset;
    using namespace Slic3r::Biz::Preset;
    using namespace Slic3r::Biz::Preset::IO;
    namespace Yaml = Slic3r::Biz::Yaml;

    PresetLoader loader;
    try {
        const std::string path = Tests::get_datadir().string()
            + "/presets/prusa-research-fff/PrusaResearch";
        loader.load_dir(path);

    } catch (const Yaml::ParseError& e) {
        std::cout << e.what() << std::endl;
        FAIL();
    }

    PresetEvaluator eval(loader.presets());

    HwPrinterConfig hw_config = {
        .technology = Slic3r::Domain::PrinterTechnology::FFF,
        .model      = {.model = "COREONE", .base_model = "COREONE"},
        .tool_count = 1,
        .features   = {},
        .tools      = {
            {.features = {
                 std::make_pair("nozzle_diameter", FeatureValue{0.4}),
                 std::make_pair("nozzle_high_flow", FeatureValue{false}),
             }}
        }
    };

    auto printer_presets = eval.evaluate(hw_config);
    auto printer_preset  = printer_presets[0];
    // REQUIRE(printer_preset.preset.values.empty() == false);
    auto values = std::get<Slic3r::Domain::PrinterSettings>(printer_preset.preset.values);
    REQUIRE(values.find("single_extruder_multi_material").item->value().get<bool>() == false);

    REQUIRE(printer_preset.prints.empty() == false);

    for (const auto& print_preset : printer_preset.prints) {
        REQUIRE(print_preset.tools.size() == hw_config.tool_count);
        REQUIRE(print_preset.materials.empty() == false);
        for (const auto& tool : print_preset.tools) {
            REQUIRE(tool.empty() == false);
        }
    }
}

TEST_CASE("Material cache produces same results as uncached evaluation", "[preset]")
{
    using namespace Slic3r::Domain::Preset;
    using namespace Slic3r::Biz::Preset;
    using namespace Slic3r::Biz::Preset::IO;
    namespace Yaml = Slic3r::Biz::Yaml;

    PresetLoader loader;
    try {
        const std::string path = Tests::get_datadir().string()
            + "/presets/prusa-research-fff/PrusaResearch";
        loader.load_dir(path);
    } catch (const Yaml::ParseError& e) {
        std::cout << e.what() << std::endl;
        FAIL();
    }

    PresetEvaluator eval(loader.presets());

    // Multi-tool config where all tools share the same id/features.
    // This exercises the material cache — tools after the first reuse cached results.
    HwToolConfig tool_04 = {
        .id = "0.4",
        .features = {
            std::make_pair("nozzle_diameter", FeatureValue{0.4}),
            std::make_pair("nozzle_high_flow", FeatureValue{false}),
        }
    };

    HwPrinterConfig hw_config = {
        .technology = Slic3r::Domain::PrinterTechnology::FFF,
        .model      = {.model = "COREONE", .base_model = "COREONE"},
        .tool_count = 3,
        .features   = {},
        .tools      = {tool_04, tool_04, tool_04},
    };

    auto cached   = eval.evaluate(hw_config, /*use_material_cache=*/true);
    auto uncached = eval.evaluate(hw_config, /*use_material_cache=*/false);

    REQUIRE(cached.size() == uncached.size());

    for (size_t p = 0; p < cached.size(); ++p) {
        const auto& cp = cached[p];
        const auto& up = uncached[p];

        REQUIRE(cp.preset.has_same_values(up.preset));
        REQUIRE(cp.prints.size() == up.prints.size());

        for (size_t pr = 0; pr < cp.prints.size(); ++pr) {
            const auto& c_print = cp.prints[pr];
            const auto& u_print = uncached[p].prints[pr];

            REQUIRE(c_print.preset.has_same_values(u_print.preset));
            REQUIRE(c_print.tools.size() == u_print.tools.size());
            REQUIRE(c_print.materials.size() == u_print.materials.size());

            for (size_t t = 0; t < c_print.tools.size(); ++t) {
                REQUIRE(c_print.tools[t].size() == u_print.tools[t].size());
                for (size_t i = 0; i < c_print.tools[t].size(); ++i) {
                    REQUIRE(c_print.tools[t][i].preset.has_same_values(
                        u_print.tools[t][i].preset));
                }
            }

            for (size_t t = 0; t < c_print.materials.size(); ++t) {
                REQUIRE(c_print.materials[t].size() == u_print.materials[t].size());
                for (size_t i = 0; i < c_print.materials[t].size(); ++i) {
                    REQUIRE(c_print.materials[t][i].preset.has_same_values(
                        u_print.materials[t][i].preset));
                }
            }
        }
    }
}
