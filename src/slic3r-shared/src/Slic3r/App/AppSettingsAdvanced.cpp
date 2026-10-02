#include "Slic3r/App/AppSettingsAdvanced.hpp"

#include <nlohmann/json.hpp>

#include <boost/filesystem.hpp>

namespace Slic3r::App {

void AppSettingsAdvanced::toggle_printer_favorite_preset(
    const std::string& id,
    const std::string& hw_config_id
)
{
    // const std::pair<std::string, std::string> preset_id{id, hw_config_id};
    if (printer_favorite_presets.contains(id)) {
        printer_favorite_presets.erase(id);
    } else {
        printer_favorite_presets.insert(id);
    }
}

void AppSettingsAdvanced::toggle_material_favorite_preset(const std::string& id)
{
    if (material_favorite_presets.contains(id)) {
        material_favorite_presets.erase(id);
    } else {
        material_favorite_presets.insert(id);
    }
}

void AppSettingsAdvanced::push_recent_project(const std::string& recent_project)
{
    constexpr size_t MaxRecentProjects = 15;

    if (!boost::filesystem::exists(recent_project)) {
        return;
    }

    RecentProjects::iterator project_it = std::ranges::find(recent_projects, recent_project);
    if (project_it != recent_projects.end()) {
        recent_projects.erase(project_it);
    }

    recent_projects.insert(recent_projects.cbegin(), recent_project);

    if (recent_projects.size() > MaxRecentProjects) {
        recent_projects.pop_back();
    }

    // validate recent_projects
    std::erase_if(
        recent_projects,
        [](const std::string& filepath) { return !boost::filesystem::exists(filepath); }
    );
}

bool AppSettingsAdvanced::contains_printer_favorite_preset(
    const std::string& id,
    const std::string& hw_config_id
) const
{
    return printer_favorite_presets.contains(id);
}

bool AppSettingsAdvanced::contains_material_favorite_preset(const std::string& id) const
{
    return material_favorite_presets.contains(id);
}

void to_json(
    nlohmann::ordered_json& json_value,
    const Slic3r::App::AppSettingsAdvanced& app_settings_advanced
)
{
    json_value = {
        {"printer_favorite_presets", app_settings_advanced.printer_favorite_presets},
        {"material_favorite_presets", app_settings_advanced.material_favorite_presets},
        {"recent_projects", app_settings_advanced.recent_projects},
        {"printer_material_selections", app_settings_advanced.printer_material_selections},
        {"printer_material_colors", app_settings_advanced.printer_material_colors},
        {"last_printer_preset_id", app_settings_advanced.last_printer_preset_id}
    };
}

void from_json(
    const nlohmann::ordered_json& json_value,
    Slic3r::App::AppSettingsAdvanced& app_settings_advanced
)
{
    json_value.at("printer_favorite_presets")
        .get_to(app_settings_advanced.printer_favorite_presets);
    json_value.at("material_favorite_presets")
        .get_to(app_settings_advanced.material_favorite_presets);
    json_value.at("recent_projects").get_to(app_settings_advanced.recent_projects);
    if (json_value.contains("printer_material_selections")) {
        const auto& selections = json_value.at("printer_material_selections");
        for (const auto& [key, value] : selections.items()) {
            if (value.is_array()) {
                value.get_to(app_settings_advanced.printer_material_selections[key]);
            } else {
                // Older versions nested selections under a transient hardware-config ID.
                for (const auto& [printer_preset_id, material_ids] : value.items()) {
                    material_ids.get_to(
                        app_settings_advanced.printer_material_selections[printer_preset_id]
                    );
                }
            }
        }
    }
    if (json_value.contains("last_printer_preset_id"))
        json_value.at("last_printer_preset_id").get_to(app_settings_advanced.last_printer_preset_id);
    if (json_value.contains("printer_material_colors"))
        json_value.at("printer_material_colors").get_to(app_settings_advanced.printer_material_colors);

    // validate recent_projects
    std::erase_if(
        app_settings_advanced.recent_projects,
        [](const std::string& filepath) { return !boost::filesystem::exists(filepath); }
    );
}

} // namespace Slic3r::App
