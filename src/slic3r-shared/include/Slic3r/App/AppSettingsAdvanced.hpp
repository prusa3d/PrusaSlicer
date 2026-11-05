#pragma once

#include "Slic3r/App/Yoga/DialogGeometry.hpp"

#include <nlohmann/json_fwd.hpp>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace Slic3r::App {

struct AppSettingsAdvanced : public Yoga::IDialogGeometryStore
{
    // This should be set of pairs, but due to hw_printer_config_id being unique each run, we are only storing id
    // using PrinterFavoritePresets  = std::set<std::pair<std::string, std::string>>;
    using PrinterFavoritePresets  = std::set<std::string>;
    using MaterialFavoritePresets = std::set<std::string>;
    using RecentProjects          = std::vector<std::string>;
    using PinnedDialogs           = std::map<std::string, Yoga::DialogGeometry>;

    void toggle_printer_favorite_preset(const std::string& id, const std::string& hw_config_id);
    void toggle_material_favorite_preset(const std::string& id);
    void push_recent_project(const std::string& recent_project);

    bool
    contains_printer_favorite_preset(const std::string& id, const std::string& hw_config_id) const;
    bool contains_material_favorite_preset(const std::string& id) const;

    std::optional<Yoga::DialogGeometry> dialog_geometry(const std::string& name) const override;
    void set_dialog_geometry(const std::string& name, const Yoga::DialogGeometry& geometry) override;
    void remove_dialog_geometry(const std::string& name) override;

    PrinterFavoritePresets printer_favorite_presets;
    MaterialFavoritePresets material_favorite_presets;
    RecentProjects recent_projects;
    PinnedDialogs pinned_dialogs;
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
