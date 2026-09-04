#include "Slic3r/Biz/ProjectSettingsInteractor.hpp"

#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/Preset/SelectedPresetConfigPack.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/Workbench.hpp"
#include "Slic3r/Assert.hpp"

namespace Slic3r::Biz {

namespace {

// 16-color hardcoded palette used as the last-resort fallback.
// Indexed by slot number modulo palette size.
constexpr std::array<std::string_view, 16> PALETTE{
    "#FF8000", // orange
    "#DB5182", // rose
    "#3EC0FF", // sky blue
    "#FF4F4F", // red
    "#FBEB7D", // yellow
    "#7ACC00", // lime
    "#00CC55", // green
    "#00BBB0", // teal
    "#4455EE", // blue
    "#8833EE", // violet
    "#EE33BB", // magenta
    "#FFFFFF", // white
    "#C0C0C0", // silver
    "#888888", // gray
    "#444444", // dark gray
    "#C8923A", // amber
};

std::vector<Domain::ColorRGB> hex_colors_to_rgb(const std::vector<std::string>& hex_colors)
{
    std::vector<Domain::ColorRGB> result;
    result.reserve(hex_colors.size());
    for (const auto& hex : hex_colors) {
        Domain::ColorRGB clr;
        if (!Biz::Algorithms::Color::decode_color(hex, clr))
            clr = {0.f, 0.f, 0.f};
        result.push_back(clr);
    }
    return result;
}

} // namespace

ProjectSettingsInteractor::ProjectSettingsInteractor(
    Domain::Workbench& workbench,
    const IMdb& mdb
) :
    m_workbench(workbench),
    m_mdb(mdb)
{}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<Domain::ColorRGB> ProjectSettingsInteractor::get_colors(
    Domain::SelectionId config_container_id
) const
{
    for (const auto& [proj_id, project] : m_workbench.projects()) {
        const Domain::ConfigContainer* cc = project.find_config_container(config_container_id);
        if (!cc)
            continue;

        const Domain::ConfigItem* hex_colors_item =
            cc->project_settings().items.find("extruder_colour");

        return hex_colors_item ?
            hex_colors_to_rgb(hex_colors_item->value().get<std::vector<std::string>>()) :
            std::vector<Domain::ColorRGB>{};
    }
    return {};
}

void ProjectSettingsInteractor::set_color_from_user(
    Domain::SelectionId config_container_id,
    int slot,
    std::string color
)
{
    for (auto& [proj_id, project] : m_workbench.projects()) {
        Domain::ConfigContainer* cc = project.find_config_container(config_container_id);
        if (!cc)
            continue;

        if (cc->print_technology() != Domain::PrinterTechnology::FFF) {
            return;
        }

        auto colors =
            cc->project_settings().items.opt("extruder_colour").get<std::vector<std::string>>();

        if (slot < 0 || slot >= static_cast<int>(colors.size()))
            return;

        if (color.empty()) {
            color = resolve_auto_color(proj_id, config_container_id, slot);
        }

        ASSERT(!color.empty());
        colors[slot] = std::move(color);
        store_and_notify(config_container_id, std::move(colors));
        return;
    }
}

void ProjectSettingsInteractor::set_colors_from_connect(
    Domain::SelectionId config_container_id,
    std::vector<std::string> incoming_colors
)
{
    for (auto& [proj_id, project] : m_workbench.projects()) {
        Domain::ConfigContainer* cc = project.find_config_container(config_container_id);
        if (!cc)
            continue;

        if (cc->print_technology() != Domain::PrinterTechnology::FFF) {
            return;
        }

        auto colors =
            cc->project_settings().items.opt("extruder_colour").get<std::vector<std::string>>();

        const size_t count = std::min(colors.size(), incoming_colors.size());
        for (size_t i = 0; i < count; ++i) {
            if (incoming_colors[i].empty())
                continue;
            colors[i] = incoming_colors[i];
        }

        store_and_notify(config_container_id, std::move(colors));
        return;
    }
}

// ---------------------------------------------------------------------------
// Listener implementations
// ---------------------------------------------------------------------------

void ProjectSettingsInteractor::on_selected_config_container_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId container_id
)
{
    if (project_id == Domain::INVALID_ID || container_id == Domain::INVALID_ID)
        return;

    load_and_reconcile(project_id, container_id);
}

void ProjectSettingsInteractor::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Preset::PresetItemType type
)
{
    if (type != Preset::PresetItemType::MaterialPreset)
        return;
    if (project_id == Domain::INVALID_ID || config_container_id == Domain::INVALID_ID)
        return;

    load_and_reconcile(project_id, config_container_id);
}

void ProjectSettingsInteractor::on_config_container_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
)
{
    if (project_id == Domain::INVALID_ID || config_container_id == Domain::INVALID_ID)
        return;

    load_and_reconcile(project_id, config_container_id);
}

// ---------------------------------------------------------------------------
// Static public helpers
// ---------------------------------------------------------------------------

std::string ProjectSettingsInteractor::palette_color(int slot)
{
    return std::string(PALETTE[static_cast<size_t>(slot) % PALETTE.size()]);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::string ProjectSettingsInteractor::resolve_auto_color(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    int slot,
    std::string_view filament_uuid
) const
{
    if (!filament_uuid.empty()) {
        if (auto c = m_mdb.get_color(std::string(filament_uuid))) {
            ASSERT(!c->empty());
            return *c;
        }
    }

    if (std::string c = preset_color(project_id, config_container_id, slot); !c.empty())
        return c;

    return palette_color(slot);
}

std::string ProjectSettingsInteractor::preset_color(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    int slot
) const
{
    const Domain::ConfigContainer* cc =
        m_workbench.project(project_id).find_config_container(config_container_id);
    if (!cc)
        return {};

    if (cc->print_technology() != Domain::PrinterTechnology::FFF) {
        return 0;
    }
    Domain::Preset::SelectedPresetConfigPack preset_config_pack(cc->selected_preset());

    if (slot < 0 || slot >= static_cast<int>(preset_config_pack.filament_size()))
        return {};

    return preset_config_pack.get_filament(slot).items.opt("filament_colour").get<std::string>();
}

int ProjectSettingsInteractor::extruder_count(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
) const
{
    const Domain::ConfigContainer* cc =
        m_workbench.project(project_id).find_config_container(config_container_id);
    if (!cc)
        return 0;

    if (cc->print_technology() != Domain::PrinterTechnology::FFF) {
        return 0;
    }
    Domain::Preset::SelectedPresetConfigPack preset_config_pack(cc->selected_preset());

    return static_cast<int>(preset_config_pack.filament_size());
}

void ProjectSettingsInteractor::load_and_reconcile(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
)
{
    Domain::ConfigContainer* cc =
        m_workbench.project(project_id).find_config_container(config_container_id);
    if (!cc)
        return;

    if (cc->print_technology() != Domain::PrinterTechnology::FFF) {
        return;
    }

    auto colors =
        cc->project_settings().items.opt("extruder_colour").get<std::vector<std::string>>();

    Domain::Preset::SelectedPresetConfigPack preset_config_pack(cc->selected_preset());

    colors.resize(preset_config_pack.filament_size());
    for (size_t slot = 0; slot < colors.size(); ++slot) {
        if (colors[slot].empty()) {
            colors[slot] = resolve_auto_color(project_id, config_container_id, slot);
        }
    }

    // Always store and notify, even if colors haven't changed.
    // UI listeners rely on this notification as their sole source of color
    // updates (e.g. after preset switches or 3MF loading).
    store_and_notify(config_container_id, std::move(colors));
}

void ProjectSettingsInteractor::store_and_notify(
    Domain::SelectionId config_container_id,
    std::vector<std::string> colors
)
{
    for (auto& [proj_id, project] : m_workbench.projects()) {
        Domain::ConfigContainer* cc = project.find_config_container(config_container_id);
        if (!cc)
            continue;

        cc->project_settings().items.opt("extruder_colour").set(colors);

        const auto rgb_colors = hex_colors_to_rgb(colors);

        invoke_listeners<IColorsChangedListener>(
            [&](auto* listener)
            { listener->on_colors_changed(proj_id, config_container_id, rgb_colors); }
        );
        return;
    }
}

} // namespace Slic3r::Biz
