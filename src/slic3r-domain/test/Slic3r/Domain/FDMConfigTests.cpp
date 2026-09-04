#include "Slic3r/Domain/ConfigBoxesFDM.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"
#include "Slic3r/Domain/FullConfigFDM.hpp"
#include "Slic3r/Domain/TestUtils.hpp"
#include "Slic3r/Domain/Config.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using namespace Slic3r::Domain;
using Slic3r::Test::build_fff_printer_config;

struct FullConfigFDMFixture
{
    int extruder_count{3};
    ConfigPackFDM config_pack{extruder_count};
    std::vector<unsigned> extruder_candidates{0, 2};
    Preset::HwPrinterConfig hw_config{build_fff_printer_config(extruder_count, {})};
};

TEST_CASE_METHOD(FullConfigFDMFixture, "Simple no special handling config value", "[FDMConfig]")
{
    // Simple in, scalar out.

    config_pack.print.items.opt("arc_fitting").set(ArcFittingType::EmitCenter);
    FullConfigFDM full_config{config_pack, extruder_candidates, hw_config};
    CHECK(full_config.get<ArcFittingType>("arc_fitting") == ArcFittingType::EmitCenter);
}

TEST_CASE_METHOD(FullConfigFDMFixture, "Print config value with override in tool", "[FDMConfig]")
{
    // Scalar in print, override in tool => vector as a result.

    config_pack.print.items.opt("bottom_solid_layers").set(11);
    config_pack.tool.at(2).overrides.set("bottom_solid_layers", 22);

    FullConfigFDM full_config{config_pack, extruder_candidates, hw_config};
    CHECK(full_config.get<std::vector<int>>("bottom_solid_layers") == std::vector<int>{11, 11, 22});
}

TEST_CASE_METHOD(
    FullConfigFDMFixture,
    "Print config value with override in just Object",
    "[FDMConfig]")
{
    // Scalar, but can be overriden.

    config_pack.print.items.opt("brim_width").set(4.2);
    FullConfigFDM full_config{config_pack, extruder_candidates, hw_config};
    CHECK(full_config.get<double>("brim_width") == 4.2);

    ObjectSettings object_settings;
    object_settings.overrides.set("brim_width", 3.0);
    auto partial_config{std::make_shared<
        PartialObjectConfigFDM>(object_settings, hw_config)};

    ConfigView config_view{std::make_shared<FullConfigFDM>(full_config), {partial_config}};
    config_view.finalize();

    CHECK(config_view.get<double>("brim_width") == 3.0);
}

TEST_CASE_METHOD(FullConfigFDMFixture, "Tool parity is ensured", "[FDMConfig]")
{
    config_pack.printer.items.opt("extruder_offset").set(std::vector<Vec2d>{Vec2d{42.0, 42.0}});
    FullConfigFDM full_config{config_pack, extruder_candidates, hw_config};
    CHECK(full_config.get<std::vector<Vec2d>>("extruder_offset").size() == 3);
}

TEST_CASE_METHOD(FullConfigFDMFixture, "Compatibility rule application - average", "[FDMConfig]")
{
    // Scalar in print, override in tool with compatibility rule => scalar as a result.
    // Extruders 0, 2 are used.
    // The compatibility rule is 'average'.

    config_pack.print.items.opt("brim_separation").set(10.0);
    // Extruder 1 is also overriden, but as unused, it is ignored.
    config_pack.tool.at(1).overrides.set("brim_separation", 1000.0);
    config_pack.tool.at(2).overrides.set("brim_separation", 20.0);

    auto full_config{std::make_shared<const FullConfigFDM>(config_pack, extruder_candidates, hw_config)};
    ConfigView view{full_config, {}};
    view.finalize();

    CHECK(view.get<double>("brim_separation") == Catch::Approx(15.0));
}

TEST_CASE_METHOD(FullConfigFDMFixture, "Compatibility rule application - ignore overrides", "[FDMConfig]")
{
    // Scalar in print, override in tool with compatibility rule => scalar as a result.
    // Extruders 0, 2 are used.

    // The value is the same for both used tool, no rule is applied.
    config_pack.print.items.opt("seam_position").set(SeamPosition::spRandom);
    config_pack.tool.at(0).overrides.set("seam_position", SeamPosition::spNearest);
    config_pack.tool.at(2).overrides.set("seam_position", SeamPosition::spNearest);
    auto full_config{std::make_shared<const FullConfigFDM>(config_pack, extruder_candidates, hw_config)};
    ConfigView view{full_config, {}};
    view.finalize();
    CHECK(view.get<SeamPosition>("seam_position") == SeamPosition::spNearest);

    // There is a conflict and the resolution is to ignore overrides.
    config_pack.print.items.opt("seam_position").set(SeamPosition::spRandom);
    config_pack.tool.at(0).overrides.set("seam_position", SeamPosition::spRear);
    config_pack.tool.at(2).overrides.set("seam_position", SeamPosition::spAligned);
    full_config = std::make_shared<const FullConfigFDM>(config_pack, extruder_candidates, hw_config);
    view = ConfigView{full_config, {}};
    view.finalize();
    CHECK(view.get<SeamPosition>("seam_position") == SeamPosition::spRandom);
}

TEST_CASE_METHOD(
    FullConfigFDMFixture,
    "FDM config spreads the values over slot_count for the mmu use case",
    "[FDMConfig]")
{

    // Tool 0: 8 materials
    // Tool 1: 1 material
    // Tool 2: 6 materials
    // Total 15 materials.

    hw_config.feeders = {
        {Preset::Address{0}, Preset::HwFeederConfig{.slot_count = 4}},
        {Preset::Address{0, 3}, Preset::HwFeederConfig{.slot_count = 5}},
        {Preset::Address{2}, Preset::HwFeederConfig{.slot_count = 6}},
    };
    config_pack.tool.at(0).overrides.set("perimeter_speed", 42.0);
    config_pack.tool.at(1).overrides.set("perimeter_speed", 33.0);
    config_pack.tool.at(2).overrides.set("perimeter_speed", 66.0);
    config_pack.filament.resize(hw_config.material_slot_count());
    config_pack.printer.items.opt("extruder_offset").set(std::vector{Vec2d{0, 0}});

    FullConfigFDM full_config{config_pack, extruder_candidates, hw_config};
    const auto nozzle_diameters{full_config.get<std::vector<double>>("nozzle_diameter")};
    REQUIRE(nozzle_diameters.size() == hw_config.material_slot_count());
    CHECK(nozzle_diameters[14] == Catch::Approx(0.4));
    const auto perimeter_speeds{full_config.get<std::vector<double>>("perimeter_speed")};
    REQUIRE(perimeter_speeds.size() == hw_config.material_slot_count());
    CHECK(perimeter_speeds[7] == Catch::Approx(42.0));
    CHECK(perimeter_speeds[8] == Catch::Approx(33.0));
    CHECK(perimeter_speeds[14] == Catch::Approx(66.0));
}

TEST_CASE_METHOD(
    FullConfigFDMFixture,
    "FloatOrPercentage over nozzle diameter is resolved",
    "[FDMConfig]")
{
    config_pack.print.items.opt("automatic_infill_combination_max_layer_height")
        .set(FloatOrPercentage{Percentage{50}});
    auto full_config{std::make_shared<const FullConfigFDM>(config_pack, extruder_candidates, hw_config)};
    ConfigView view{full_config, {}};
    view.finalize();
    const auto result{
        view.get<std::vector<FloatOrPercentage>>("automatic_infill_combination_max_layer_height")};
    for (FloatOrPercentage value : result) {
        REQUIRE(!value.is_percentage());
        CHECK(value.float_value() == Catch::Approx(0.2));
    }
}

TEST_CASE_METHOD(
    FullConfigFDMFixture,
    "FloatOrPercentage over another config value is resolved, including overrides",
    "[FDMConfig]")
{
    config_pack.print.items.opt("external_perimeter_speed").set(FloatOrPercentage{Percentage{50}});
    config_pack.tool.at(0).overrides.set("external_perimeter_speed", FloatOrPercentage{42.0});
    config_pack.tool.at(2)
        .overrides.set("external_perimeter_speed", FloatOrPercentage{Percentage{25}});

    config_pack.print.items.opt("perimeter_speed").set(100.0);
    config_pack.tool.at(0).overrides.set("perimeter_speed", 1000.0);
    config_pack.tool.at(1).overrides.set("perimeter_speed", 10000.0);

    auto full_config{std::make_shared<const FullConfigFDM>(config_pack, extruder_candidates, hw_config)};
    ConfigView view{full_config, {}};
    view.finalize();

    const auto result{view.get<std::vector<FloatOrPercentage>>("external_perimeter_speed")};

    for (const FloatOrPercentage& value : result) {
        REQUIRE(!value.is_percentage());
    }
    REQUIRE(result.size() == 3);
    CHECK(result[0].float_value() == Catch::Approx(42.0)); // Just the value.
    CHECK(result[1].float_value() == Catch::Approx(5000.0)); // 50% out of 100000.0
    CHECK(result[2].float_value() == Catch::Approx(25.0)); // 25% out of 100.0
}

TEST_CASE_METHOD(
    FullConfigFDMFixture,
    "Print view respect partial config overrides for items with compatibility rule",
    "[FDMConfig]")
{
    // Tools 0 and 2 are used.
    config_pack.print.items.opt("brim_separation").set(10.0);
    config_pack.tool.at(2).overrides.set("brim_separation", 20.0);

    auto full_config{std::make_shared<const FullConfigFDM>(config_pack, extruder_candidates, hw_config)};
    ConfigView view{full_config, {}};
    view.finalize();

    auto result{view.get<double>("brim_separation")};
    CHECK(result == Catch::Approx(15.0)); // Average

    ObjectSettings object_settings;
    object_settings.overrides.set("brim_separation", 30.0);
    auto partial_config{std::make_shared<PartialObjectConfigFDM>(object_settings, hw_config)};

    view = ConfigView{full_config, {partial_config}};
    view.finalize();

    result = view.get<double>("brim_separation");
    CHECK(result == Catch::Approx(30.0)); // The overriden value.
}

TEST_CASE_METHOD(
    FullConfigFDMFixture,
    "Print view respect partial config overrides and resolves float or percentage",
    "[FDMConfig]")
{
    // Tools 0 and 2 are used.

    config_pack.tool.at(0).overrides.set("perimeter_speed", 100.0);
    config_pack.print.items.opt("perimeter_speed").set(1000.0);

    config_pack.print.items.opt("external_perimeter_speed").set(FloatOrPercentage{Percentage{50}});
    config_pack.tool.at(2).overrides.set("external_perimeter_speed", FloatOrPercentage{Percentage{25}});

    auto full_config{std::make_shared<const FullConfigFDM>(config_pack, extruder_candidates, hw_config)};
    ConfigView view{full_config, {}};
    view.finalize();

    auto result{view.get<std::vector<FloatOrPercentage>>("external_perimeter_speed")};
    REQUIRE(result.size() == 3);
    CHECK(!result[0].is_percentage());
    CHECK(!result[2].is_percentage());
    CHECK(result[0].float_value() == Catch::Approx(50.0));
    CHECK(result[2].float_value() == Catch::Approx(250.0));

    ObjectSettings object_settings;
    object_settings.overrides.set("external_perimeter_speed", FloatOrPercentage{Percentage{75}});
    auto partial_config{std::make_shared<PartialObjectConfigFDM>(object_settings, hw_config)};

    view = ConfigView{full_config, {partial_config}};
    view.finalize();

    result = view.get<std::vector<FloatOrPercentage>>("external_perimeter_speed");
    CHECK(!result.at(0).is_percentage());
    CHECK(!result.at(2).is_percentage());
    CHECK(result.at(0).float_value() == Catch::Approx(75));
    CHECK(result.at(2).float_value() == Catch::Approx(750));
}

TEST_CASE_METHOD(
    FullConfigFDMFixture,
    "Check that all float or percentage values are resolved",
    "[FDMConfig]")
{
    MutBoxOrBoxesVector boxes{as_mut_boxes(config_pack)};
    for (auto& box_or_boxes : boxes) {
        std::visit(
            overloaded{
                [](const MutBoxRef& box)
                {
                    for (ConfigItem& item : box.get().items.all_items()) {
                        if (item.holds_alternative<FloatOrPercentage>()) {
                            item.set(FloatOrPercentage{Percentage{50}});
                        }
                    }
                },
                [](const MutBoxRefs& boxes)
                {
                    for (const MutBoxRef& box : boxes) {
                        for (ConfigItem& item : box.get().items.all_items()) {
                            if (item.holds_alternative<FloatOrPercentage>()) {
                                item.set(FloatOrPercentage{Percentage{50}});
                            }
                        }
                    }
                },
            },
            box_or_boxes);
    }

    auto full_config{std::make_shared<const FullConfigFDM>(config_pack, extruder_candidates, hw_config)};
    ConfigView view{full_config, {}};

    for (const auto& [name, config_value] : view.values()) {
        INFO(name);
        if (config_value.holds_alternative<FloatOrPercentage>()) {
            REQUIRE(!config_value.get<FloatOrPercentage>().is_percentage());
        }

        if (config_value.holds_alternative<std::vector<FloatOrPercentage>>()) {
            for (const FloatOrPercentage& value :
                 config_value.get<std::vector<FloatOrPercentage>>())
            {
                REQUIRE(!value.is_percentage());
            }
        }
    }
}

TEST_CASE_METHOD(
    FullConfigFDMFixture,
    "Parameter expansion works as expected",
    "[FDMConfig]")
{
    config_pack.tool.at(0).overrides.set("external_perimeter_speed", FloatOrPercentage{100});
    config_pack.print.items.opt("first_layer_speed").set(FloatOrPercentage{Percentage{75}});
    config_pack.tool.at(0).overrides.set("first_layer_speed", FloatOrPercentage{Percentage{50}});
    config_pack.print.items.opt("perimeter_speed").set(1000.0);
    config_pack.print.items.opt("external_perimeter_speed").set(FloatOrPercentage{Percentage{100}});

    auto full_config{std::make_shared<const FullConfigFDM>(config_pack, extruder_candidates, hw_config)};
    ConfigView view{full_config, {}};
    view.finalize();
    auto result{view.get<std::vector<FloatOrPercentage>>("first_layer_external_perimeter_speed")};

    REQUIRE(result.size() == 3);
    REQUIRE(!result[0].is_percentage());
    REQUIRE(!result[1].is_percentage());
    CHECK(result[0].float_value() == Catch::Approx(50));
    CHECK(result[1].float_value() == Catch::Approx(750));

    config_pack.tool.at(0).overrides.set("first_layer_external_perimeter_speed", FloatOrPercentage{Percentage{25}});
    config_pack.print.items.opt("first_layer_external_perimeter_speed").set(FloatOrPercentage{Percentage{10}});

    full_config = std::make_shared<const FullConfigFDM>(config_pack, extruder_candidates, hw_config);
    view = ConfigView{full_config, {}};
    view.finalize();
    result = view.get<std::vector<FloatOrPercentage>>("first_layer_external_perimeter_speed");

    REQUIRE(result.size() == 3);
    REQUIRE(!result[0].is_percentage());
    REQUIRE(!result[1].is_percentage());
    CHECK(result[0].float_value() == Catch::Approx(25));
    CHECK(result[1].float_value() == Catch::Approx(100));
}
