#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

#include <catch2/catch_test_macros.hpp>
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

#include "Slic3r/Biz/Preset/IO/PresetMetadataLegacyLoader.hpp"
#include "Slic3r/Domain/Workbench.hpp"
#include "Slic3r/Directories.hpp"

TEST_CASE("PresetMetadataLegacyLoader tests", "[preset][legacy]")
{
    using namespace Slic3r;
    using namespace Slic3r::Biz;
    using namespace Slic3r::Biz::Preset::IO;
    namespace fs = boost::filesystem;


    Domain::Workbench workbench;

    App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{dispatcher};
    Tests::JobManagerScope job_manager_scope{dispatcher};
    App::Plater::ThumbnailImageGenerator thumbnail_image_generator;
    ProjectInteractor project_interactor{workbench, dispatcher, thumbnail_image_generator};
    Scene::SceneInteractor& scene_interactor{project_interactor.scene_interactor()};

    fs::path data_dir{Tests::get_datadir()};

    set_data_dir(Tests::get_datadir().string());

    project_interactor.preset_interactor().load_preset_bundle(
        BundlePaths::make_test_runtime(data_dir)
    );



    const Domain::ConfigPack config;
    const Domain::Preset::Bundle& preset_bundle = project_interactor.workbench().preset_bundle();

    SECTION("FDM: XL will load successfully")
    {
        LegacyPresetMetadata legacy_preset = {
            .technology           = Domain::PrinterTechnology::FFF,
            .printer_model        = "XLIS",
            .printer_notes        = "",
            .tools                = {
                LegacyHwToolConfig{.nozzle_diameter = 0.6}
            },
            .printer_settings_id  = "Original Prusa XL 1T",
            .print_settings_id    = "10mm Fast",
            .material_settings_id = {"Generic PETG"}
        };
        auto result = load_legacy_preset_metadata(legacy_preset, config, preset_bundle);
        REQUIRE(result.has_value());
        auto v = result.value();
        REQUIRE(v.hw_config.technology == Domain::PrinterTechnology::FFF);
        REQUIRE(v.hw_config.tools.size() == 1);
        REQUIRE(v.hw_config.name == "Prusa XL 0.6");
        REQUIRE(v.printer.name == "Original Prusa XL 1T");
        REQUIRE(v.print.name == "10mm Fast");
        REQUIRE(v.materials.size() == 1);
        REQUIRE(v.materials.at(0).name == "Generic PETG");
    }

    SECTION("SLA: SL1S will load successfully")
    {
        LegacyPresetMetadata legacy_preset = {
            .technology           = Domain::PrinterTechnology::SLA,
            .printer_model        = "SL1S",
            .printer_notes        = "",
            .tools                = {
                LegacyHwToolConfig{.nozzle_diameter = 0.4}
            },
            .printer_settings_id  = "Original SL1 SPEED",
            .print_settings_id    = "0.10mm Fast",
            .material_settings_id = {"Generic Resin"}
        };
        auto result = load_legacy_preset_metadata(legacy_preset, config, preset_bundle);
        REQUIRE(result.has_value());
        auto v = result.value();
        REQUIRE(v.hw_config.technology == Domain::PrinterTechnology::SLA);
        REQUIRE(v.hw_config.tools.size() == 1);
        REQUIRE(v.hw_config.name == "SL1S SPEED");
        REQUIRE(v.printer.name == "Original SL1 SPEED");
        REQUIRE(v.print.name == "0.10mm Fast");
        REQUIRE(v.materials.size() == 1);
        REQUIRE(v.materials.at(0).name == "Generic Resin");
    }

    dispatcher.close();
}
