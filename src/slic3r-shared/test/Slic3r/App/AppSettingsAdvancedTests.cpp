#include <catch2/catch_test_macros.hpp>

#include <Slic3r/App/AppSettingsAdvanced.hpp>

#include <nlohmann/json.hpp>

using namespace Slic3r::App;

TEST_CASE("AppSettingsAdvanced: pinned dialogs JSON round trip")
{
    AppSettingsAdvanced settings;
    const Yoga::DialogGeometry geometry{10.f, 20.f, 300.f, 400.f, true};
    settings.set_dialog_geometry("TestDialog", geometry);

    const nlohmann::ordered_json json_value = settings;
    const AppSettingsAdvanced loaded        = json_value.get<AppSettingsAdvanced>();

    REQUIRE(loaded.dialog_geometry("TestDialog") == geometry);
    REQUIRE_FALSE(loaded.dialog_geometry("Other").has_value());
}

TEST_CASE("AppSettingsAdvanced: JSON without pinned dialogs still loads")
{
    const nlohmann::ordered_json json_value = {
        {"printer_favorite_presets", nlohmann::ordered_json::array()},
        {"material_favorite_presets", nlohmann::ordered_json::array()},
        {"recent_projects", nlohmann::ordered_json::array()}
    };

    AppSettingsAdvanced loaded;
    REQUIRE_NOTHROW(loaded = json_value.get<AppSettingsAdvanced>());
    REQUIRE(loaded.pinned_dialogs.empty());
}

TEST_CASE("AppSettingsAdvanced: invalid pinned dialogs are dropped")
{
    const nlohmann::ordered_json json_value = {
        {"printer_favorite_presets", nlohmann::ordered_json::array({"printer"})},
        {"material_favorite_presets", nlohmann::ordered_json::array()},
        {"recent_projects", nlohmann::ordered_json::array()},
        {"pinned_dialogs", {{"TestDialog", {{"left", nullptr}}}}}
    };

    AppSettingsAdvanced loaded;
    REQUIRE_NOTHROW(loaded = json_value.get<AppSettingsAdvanced>());
    REQUIRE(loaded.pinned_dialogs.empty());
    REQUIRE(loaded.printer_favorite_presets.contains("printer"));
}
