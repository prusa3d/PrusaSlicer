#pragma once

#include <nlohmann/json_fwd.hpp>

#include <set>
#include <string>

namespace Slic3r::App {

struct AppSettingsAdvanced
{
    // This should be set of pairs, but due to hw_printer_config_id being unique each run, we are only storing id
    // using PrinterFavoritePresets  = std::set<std::pair<std::string, std::string>>;
    using PrinterFavoritePresets  = std::set<std::string>;
    using MaterialFavoritePresets = std::set<std::string>;
    using RecentProjects          = std::vector<std::string>;

    void toggle_printer_favorite_preset(const std::string& id, const std::string& hw_config_id);
    void toggle_material_favorite_preset(const std::string& id);
    void push_recent_project(const std::string& recent_project);

    bool
    contains_printer_favorite_preset(const std::string& id, const std::string& hw_config_id) const;
    bool contains_material_favorite_preset(const std::string& id) const;

    PrinterFavoritePresets printer_favorite_presets;
    MaterialFavoritePresets material_favorite_presets;
    RecentProjects recent_projects;
};

void to_json(
    nlohmann::ordered_json& json_value,
    const Slic3r::App::AppSettingsAdvanced& app_settings_advanced
);
void from_json(
    const nlohmann::ordered_json& json_value,
    Slic3r::App::AppSettingsAdvanced& app_settings_advanced
);

} // namespace Slic3r::App
