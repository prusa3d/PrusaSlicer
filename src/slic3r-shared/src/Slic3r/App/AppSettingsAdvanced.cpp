#include "Slic3r/App/AppSettingsAdvanced.hpp"

#include "Slic3r/Log.hpp"

#include <nlohmann/json.hpp>

#include <boost/filesystem.hpp>

namespace Slic3r::App {

namespace {

nlohmann::ordered_json pinned_dialogs_to_json(const AppSettingsAdvanced::PinnedDialogs& pinned_dialogs)
{
    nlohmann::ordered_json json_value = nlohmann::ordered_json::object();
    for (const auto& [name, geometry] : pinned_dialogs) {
        json_value[name] = {
            {"left", geometry.left},
            {"top", geometry.top},
            {"width", geometry.width},
            {"height", geometry.height},
            {"maximized", geometry.maximized}
        };
    }
    return json_value;
}

AppSettingsAdvanced::PinnedDialogs pinned_dialogs_from_json(const nlohmann::ordered_json& json_value)
{
    AppSettingsAdvanced::PinnedDialogs pinned_dialogs;
    for (const auto& [name, value] : json_value.items()) {
        pinned_dialogs[name] = Yoga::DialogGeometry{
            value.value("left", 0.f),
            value.value("top", 0.f),
            value.value("width", 0.f),
            value.value("height", 0.f),
            value.value("maximized", false)
        };
    }
    return pinned_dialogs;
}

} // namespace

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

std::optional<Yoga::DialogGeometry> AppSettingsAdvanced::dialog_geometry(const std::string& name) const
{
    const PinnedDialogs::const_iterator it = pinned_dialogs.find(name);
    if (it == pinned_dialogs.cend()) {
        return std::nullopt;
    }
    return it->second;
}

void AppSettingsAdvanced::set_dialog_geometry(
    const std::string& name,
    const Yoga::DialogGeometry& geometry
)
{
    pinned_dialogs[name] = geometry;
}

void AppSettingsAdvanced::remove_dialog_geometry(const std::string& name)
{
    pinned_dialogs.erase(name);
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
        {"pinned_dialogs", pinned_dialogs_to_json(app_settings_advanced.pinned_dialogs)},
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
    if (json_value.contains("pinned_dialogs")) {
        try {
            app_settings_advanced.pinned_dialogs =
                pinned_dialogs_from_json(json_value.at("pinned_dialogs"));
        } catch (const nlohmann::json::exception& e) {
            SPDLOG_WARN("Dropping invalid pinned_dialogs: {}", e.what());
        }
    }
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
