#include "Slic3r/Biz/ProjectSettingsInteractor.hpp"

#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/Percentage.hpp"
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

// The technology specific part of the colors. They are handled as one color per material slot;
// each technology stores them in its own project settings.

// FFF stores a color per extruder (extruder_colour), SLA the one resin color (resin_colour).

bool is_fff(const Domain::ConfigContainer& cc)
{
    return cc.print_technology() == Domain::PrinterTechnology::FFF;
}

bool has_colors(const Domain::ConfigContainer& cc)
{
    return is_fff(cc) || cc.print_technology() == Domain::PrinterTechnology::SLA;
}

std::vector<std::string> stored_colors(const Domain::ConfigContainer& cc)
{
    ASSERT(has_colors(cc));
    if (is_fff(cc)) {
        return cc.project_settings().items.opt("extruder_colour").get<std::vector<std::string>>();
    }
    return {cc.sla_project_settings().items.opt("resin_colour").get<std::string>()};
}

void set_stored_colors(Domain::ConfigContainer& cc, std::vector<std::string> colors)
{
    ASSERT(has_colors(cc));
    if (is_fff(cc)) {
        cc.project_settings().items.opt("extruder_colour").set(std::move(colors));
        return;
    }
    ASSERT(colors.size() == 1);
    cc.sla_project_settings().items.opt("resin_colour").set(std::move(colors.front()));
}

size_t material_slot_count(const Domain::ConfigContainer& cc)
{
    ASSERT(has_colors(cc));
    if (is_fff(cc)) {
        const Domain::Preset::SelectedPresetConfigPack preset_config_pack(cc.selected_preset());
        return preset_config_pack.filament_size();
    }
    return cc.selected_preset().materials.size();
}

// The color of the material preset in the slot, which must exist.
std::string preset_material_color(const Domain::ConfigContainer& cc, size_t slot)
{
    ASSERT(has_colors(cc));
    if (is_fff(cc)) {
        const Domain::Preset::SelectedPresetConfigPack preset_config_pack(cc.selected_preset());
        return preset_config_pack.get_filament(slot).items.opt("filament_colour").get<std::string>();
    }
    return std::get<Domain::SLAMaterialSettings>(cc.selected_preset().materials.at(slot).values)
        .items.opt("material_colour")
        .get<std::string>();
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
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
) const
{
    const Domain::ConfigContainer* cc = find_config_container(project_id, config_container_id);
    if (!cc || !has_colors(*cc))
        return {};

    return hex_colors_to_rgb(stored_colors(*cc));
}

void ProjectSettingsInteractor::set_color_from_user(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    int slot,
    std::string color
)
{
    Domain::ConfigContainer* cc = find_config_container(project_id, config_container_id);
    if (!cc)
        return;

    if (!has_colors(*cc)) {
        return;
    }

    std::vector<std::string> colors = stored_colors(*cc);

    if (slot < 0 || slot >= static_cast<int>(colors.size()))
        return;

    if (color.empty()) {
        color = resolve_auto_color(
            project_id,
            config_container_id,
            slot,
            cc->selected_preset().material_matdb_uuid(slot)
        );
    }

    ASSERT(!color.empty());
    colors[slot] = std::move(color);
    store_and_notify(project_id, config_container_id, std::move(colors));
}

void ProjectSettingsInteractor::reset_color_to_auto(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    int slot
)
{
    // NOTE: This deliberately (ab)uses set_color_from_user even though the user did not pick
    // any color. Passing an empty color makes set_color_from_user re-resolve the slot via
    // resolve_auto_color, which is exactly what we want here. The misuse is kept private to
    // this class so that callers outside express their actual intent; if set_color_from_user
    // ever starts treating "from user" specially (e.g. locking the slot), this must be
    // reimplemented without it.
    set_color_from_user(project_id, config_container_id, slot, {});
}

void ProjectSettingsInteractor::set_colors_from_connect(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    std::vector<std::string> incoming_colors
)
{
    Domain::ConfigContainer* cc = find_config_container(project_id, config_container_id);
    if (!cc)
        return;

    if (!has_colors(*cc)) {
        return;
    }

    std::vector<std::string> colors = stored_colors(*cc);

    const size_t count = std::min(colors.size(), incoming_colors.size());
    for (size_t i = 0; i < count; ++i) {
        if (incoming_colors[i].empty())
            continue;
        colors[i] = incoming_colors[i];
    }

    store_and_notify(project_id, config_container_id, std::move(colors));
}

WipingVolumesData ProjectSettingsInteractor::get_wiping_volumes_data(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
) const
{
    WipingVolumesData data;

    const Domain::ConfigContainer* cc = find_config_container(project_id, config_container_id);
    if (!cc)
        return data;

    const Domain::ConfigItem* hex_colors_item =
        cc->project_settings().items.find("extruder_colour");
    data.extruder_colours = hex_colors_item ?
        hex_colors_to_rgb(hex_colors_item->value().get<std::vector<std::string>>()) :
        std::vector<Domain::ColorRGB>{};
    data.use_custom_matrix =
        cc->project_settings().items.opt("wiping_volumes_use_custom_matrix").get<bool>();

    if (cc->print_technology() != Domain::PrinterTechnology::FFF)
        return data;

    const size_t num_extruders = data.extruder_colours.size();

    // Resolve the selected preset pack once; it feeds both the printer purging volume and the
    // per-filament multipliers below, and (if needed) the matrix reconciliation.
    Domain::Preset::SelectedPresetConfigPack preset_config_pack(cc->selected_preset());
    ASSERT(preset_config_pack.filament_size() == num_extruders);

    data.printer_purging_volume =
        preset_config_pack.get_printer().items.opt("multimaterial_purging").get<double>();

    data.filament_purging_multipliers.reserve(num_extruders);
    for (size_t i = 0; i < num_extruders; ++i) {
        data.filament_purging_multipliers.push_back(
            preset_config_pack.get_filament(i)
                .items.opt("filament_purge_multiplier")
                .get<Domain::Percentage>()
                .value
        );
    }

    const std::vector<double> old_matrix =
        cc->project_settings().items.opt("wiping_volumes_matrix").get<std::vector<double>>();

    // Verify if wiping_volumes_matrix has proper size
    // (it is used to deduce number of extruders in wipe tower generator):
    const size_t old_num_extruders = size_t(std::sqrt(old_matrix.size()) + Domain::EPSILON);
    if (num_extruders == old_num_extruders) {
        data.matrix = old_matrix;
        return data;
    }

    // Extend/shrink the matrix to match the current extruder count, filling new entries from
    // the (possibly modified) printer/filament settings resolved above.
    data.matrix.reserve(num_extruders * num_extruders);
    for (unsigned int i = 0; i < num_extruders; ++i) {
        for (unsigned int j = 0; j < num_extruders; ++j) {
            // append the value for this pair from the old matrix (if it's there):
            if (i < old_num_extruders && j < old_num_extruders)
                data.matrix.push_back(old_matrix[i * old_num_extruders + j]);
            else
                data.matrix.push_back(
                    i == j ?
                        0. :
                        data.printer_purging_volume * data.filament_purging_multipliers[j] / 100.
                );
        }
    }
    return data;
}

void ProjectSettingsInteractor::set_wiping_volumes_data(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    std::vector<double> matrix,
    bool use_custom_matrix
)
{
    if (Domain::ConfigContainer* cc = find_config_container(project_id, config_container_id)) {
        cc->project_settings().items.opt("wiping_volumes_matrix").set(std::move(matrix));
        cc->project_settings().items.opt("wiping_volumes_use_custom_matrix").set(use_custom_matrix);

        for (const auto& instance : cc->bed_instances()) {
            Domain::BedRef bed(cc->id().id, instance->id().id);
            this->invoke_listeners<ISlicingInputChangedListener>(
                [&](auto* l) { l->on_slicing_input_changed(bed); }
            );
        }
    }
}

Domain::ConfigContainer* ProjectSettingsInteractor::
    find_config_container(Domain::SelectionId project_id, Domain::SelectionId config_container_id)
{
    Domain::Project* project = m_workbench.find_project_by_id(project_id);
    return project ? project->find_config_container(config_container_id) : nullptr;
}

const Domain::ConfigContainer* ProjectSettingsInteractor::find_config_container(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
) const
{
    const Domain::Project* project = m_workbench.find_project_by_id(project_id);
    return project ? project->find_config_container(config_container_id) : nullptr;
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

    if (!has_colors(*cc)) {
        PANIC("Invalid print technology");
        return {};
    }

    if (slot < 0 || slot >= static_cast<int>(material_slot_count(*cc)))
        return {};

    return preset_material_color(*cc, slot);
}

int ProjectSettingsInteractor::
    extruder_count(Domain::SelectionId project_id, Domain::SelectionId config_container_id) const
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

void ProjectSettingsInteractor::
    load_and_reconcile(Domain::SelectionId project_id, Domain::SelectionId config_container_id)
{
    Domain::ConfigContainer* cc =
        m_workbench.project(project_id).find_config_container(config_container_id);
    if (!cc)
        return;

    if (!has_colors(*cc)) {
        return;
    }

    std::vector<std::string> colors = stored_colors(*cc);

    colors.resize(material_slot_count(*cc));
    // Only fill empty slots. This runs on every container switch and material preset change,
    // so it must not overwrite colors chosen by the user, loaded from 3MF or synced from
    // Connect. Explicit color overrides (e.g. MDB material selection) go through
    // reset_color_to_auto.
    for (size_t slot = 0; slot < colors.size(); ++slot) {
        if (colors[slot].empty()) {
            colors[slot] = resolve_auto_color(
                project_id,
                config_container_id,
                slot,
                cc->selected_preset().material_matdb_uuid(slot)
            );
        }
    }

    // Always store and notify, even if colors haven't changed.
    // UI listeners rely on this notification as their sole source of color
    // updates (e.g. after preset switches or 3MF loading).
    store_and_notify(project_id, config_container_id, std::move(colors));
}

void ProjectSettingsInteractor::store_and_notify(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    std::vector<std::string> colors
)
{
    Domain::ConfigContainer* cc = find_config_container(project_id, config_container_id);
    if (!cc)
        return;

    set_stored_colors(*cc, colors);

    const auto rgb_colors = hex_colors_to_rgb(colors);

    invoke_listeners<IColorsChangedListener>(
        [&](auto* listener)
        { listener->on_colors_changed(project_id, config_container_id, rgb_colors); }
    );
}

} // namespace Slic3r::Biz
