#include "Slic3r/Biz/PrintModeInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Log.hpp"

namespace Slic3r::Biz {

PrintModeInteractor::PrintModeInteractor(ProjectInteractor& project_interactor) :
    m_project_interactor{project_interactor}, m_provider{&m_noop_provider}
{}

void PrintModeInteractor::set_provider(const IPrintModesProvider* provider)
{
    if (!provider) {
        // Cleared on teardown, when the project may already be gone. Nothing is read from it.
        m_provider = &m_noop_provider;
        m_modes.clear();
        return;
    }
    m_provider = provider;
    reload_provider();
}

void PrintModeInteractor::reload_provider()
{
    if (m_project_interactor.selected_config_container_id() != Domain::INVALID_ID) {
        remove_incompatible_modes();
    }
    reevaluate();
    reload_orphaned_modes();
    invoke_listeners<IPrintModesChangedListener>(
        [](auto* listener) { listener->on_print_modes_changed(); });
}

void
PrintModeInteractor::set_mode_params(const std::string& id, const Domain::PrintModeParams& params)
{
    m_mode_params[m_project_interactor.selected_config_container_id()][id] = params;

    if (m_config_values.empty()) {
        return;
    }

    m_modes[id] = m_provider->evaluate(id, m_config_values, params);
    apply_evaluated();

    invoke_listeners<IPrintModesChangedListener>(
        [](auto* listener) { listener->on_print_modes_changed(); });
}

void PrintModeInteractor::set_active(const std::string& id, const bool active)
{
    Preset::PresetInteractor& preset_interactor{m_project_interactor.preset_interactor()};
    if (!active) {
        preset_interactor.set_macro_overrides(id, std::nullopt);
        return;
    }
    if (const auto it{m_modes.find(id)}; it != m_modes.end()) {
        if (it->second.error) {
            SPDLOG_WARN("Print mode '{}' cannot be applied: {}", id, *it->second.error);
            return;
        }
        preset_interactor.set_macro_overrides(id, it->second.layer);
        return;
    }
    const Domain::MacroOverridesMapFDM& orphans{orphaned_modes()};
    if (const auto it{orphans.find(id)}; it != orphans.end()) {
        preset_interactor.set_macro_overrides(id, it->second);
        return;
    }
    SPDLOG_ERROR("Print mode '{}' is not available", id);
}

static std::map<std::string, Domain::ConfigValue> get_config_values(
    const Biz::ProjectInteractor& project_interactor)
{
    const Biz::Slicing::BackgroundProcessState* state{
        project_interactor.slicing_interactor().find_process_state(
            project_interactor.selected_bed_slicing_id())};
    if (!state) {
        return {};
    }
    const Domain::ConfigContainer& container{project_interactor.selected_config_container()};
    Domain::ConfigPack pack{container.build_print_config()};
    Domain::ConfigPackFDM* fdm{std::get_if<Domain::ConfigPackFDM>(&pack)};
    if (!fdm) {
        return {};
    }
    fdm->macro_overrides.clear();
    const Domain::FullConfigPtr full_config{std::make_shared<const Domain::FullConfigFDM>(
        *fdm, state->extruder_candidates, container.selected_preset().metadata().hw_config)};
    Domain::ConfigView view{full_config, {}};
    view.finalize();
    return view.values();
}

const Domain::MacroOverridesMapFDM& PrintModeInteractor::orphaned_modes() const
{
    auto it{m_orphaned_modes.find(m_project_interactor.selected_config_container_id())};
    if (it == m_orphaned_modes.end()) {
        static Domain::MacroOverridesMapFDM empty;
        return empty;
    }
    return it->second;
}

void PrintModeInteractor::reevaluate()
{
    m_modes.clear();

    if (m_config_values.empty()) {
        return;
    }

    const ModeParams& mode_params{
        m_mode_params[m_project_interactor.selected_config_container_id()]};
    for (const PrintModeAvailability& mode : provider_modes()) {
        if (!mode.compatible) {
            continue;
        }
        Domain::PrintModeParams params;
        const auto it{mode_params.find(mode.id)};
        if (it != mode_params.end()) {
            params = it->second;
        }
        m_modes[mode.id] = m_provider->evaluate(mode.id, m_config_values, params);
    }
    apply_evaluated();
}

PrintModeAvailabilities PrintModeInteractor::provider_modes() const
{
    ASSERT(m_project_interactor.selected_config_container_id() != Domain::INVALID_ID);
    return m_provider->modes(
        m_project_interactor.preset_interactor().selected_printer_preset().hw_config);
}

bool PrintModeInteractor::remove_incompatible_modes()
{
    Preset::PresetInteractor& preset_interactor{m_project_interactor.preset_interactor()};
    const Domain::MacroOverridesMapFDM* applied{std::get_if<Domain::MacroOverridesMapFDM>(
        &preset_interactor.selected_printer_preset().macro_overrides)};
    if (!applied || applied->empty()) {
        return false;
    }

    std::vector<std::string> incompatible;
    for (const PrintModeAvailability& mode : provider_modes()) {
        if (!mode.compatible && applied->contains(mode.id)) {
            incompatible.push_back(mode.id);
        }
    }
    for (const std::string& id : incompatible) {
        SPDLOG_INFO("Print mode '{}' is not compatible with the selected printer, removing", id);
        preset_interactor.set_macro_overrides(id, std::nullopt);
    }
    return !incompatible.empty();
}

bool PrintModeInteractor::drop_modes()
{
    if (m_project_interactor.selected_config_container_id() == Domain::INVALID_ID) {
        return false;
    }
    Preset::PresetInteractor& preset_interactor{m_project_interactor.preset_interactor()};
    const Domain::MacroOverridesMapFDM* applied{std::get_if<Domain::MacroOverridesMapFDM>(
        &preset_interactor.selected_printer_preset().macro_overrides)};
    if (!applied || applied->empty()) {
        return false;
    }
    std::vector<std::string> ids;
    for (const auto& [id, layer] : *applied) {
        ids.push_back(id);
    }
    for (const std::string& id : ids) {
        SPDLOG_INFO("Printer selected, dropping print mode '{}'", id);
        preset_interactor.set_macro_overrides(id, std::nullopt);
    }
    return true;
}

void PrintModeInteractor::apply_evaluated()
{
    if (m_project_interactor.selected_config_container_id() == Domain::INVALID_ID) {
        return;
    }
    Preset::PresetInteractor& preset_interactor{m_project_interactor.preset_interactor()};
    const Domain::MacroOverridesMapFDM* applied{std::get_if<Domain::MacroOverridesMapFDM>(
        &preset_interactor.selected_printer_preset().macro_overrides)};
    if (!applied) {
        return;
    }

    std::vector<std::pair<std::string, std::optional<Domain::MacroOverridesLayerFDM>>> changed;
    for (const auto& [id, layer] : *applied) {
        const auto it{m_modes.find(id)};
        if (it == m_modes.end()) {
            continue;
        }
        if (it->second.error) {
            changed.emplace_back(id, std::nullopt);
        } else if (it->second.layer != layer) {
            changed.emplace_back(id, it->second.layer);
        }
    }
    for (auto& [id, layer] : changed) {
        preset_interactor.set_macro_overrides(id, std::move(layer));
    }
}

bool PrintModeInteractor::reload_config_values() {
    const std::map<std::string, Domain::ConfigValue> config_values{
        get_config_values(m_project_interactor)};

    if (m_config_values != config_values) {
        m_config_values = config_values;
        return true;
    }
    return false;
}

bool PrintModeInteractor::reload_mode_params()
{
    if (m_project_interactor.selected_config_container_id() == Domain::INVALID_ID) {
        return false;
    }
    const Domain::MacroOverridesMapFDM* applied{std::get_if<Domain::MacroOverridesMapFDM>(
        &m_project_interactor.preset_interactor().selected_printer_preset().macro_overrides)};
    if (!applied) {
        return false;
    }

    ModeParams& mode_params{m_mode_params[m_project_interactor.selected_config_container_id()]};
    bool changed{false};
    for (const auto& [id, layer] : *applied) {
        const auto it{mode_params.find(id)};
        if (it == mode_params.end() || it->second != layer.params) {
            mode_params[id] = layer.params;
            changed = true;
        }
    }
    return changed;
}

bool PrintModeInteractor::reload_orphaned_modes()
{
    const Domain::SelectionId config_container_id{
        m_project_interactor.selected_config_container_id()};
    if (config_container_id == Domain::INVALID_ID) {
        return false;
    }
    const Domain::MacroOverridesMapFDM* all_print_modes{std::get_if<Domain::MacroOverridesMapFDM>(
        &m_project_interactor.preset_interactor().selected_printer_preset().macro_overrides)};
    if (!all_print_modes) {
        return false;
    }

    std::set<std::string> mode_ids_set;
    for (const PrintModeAvailability& mode : provider_modes()) {
        mode_ids_set.insert(mode.id);
    }

    Domain::MacroOverridesMapFDM new_modes;
    auto it{m_orphaned_modes.find(config_container_id)};
    if (it != m_orphaned_modes.end()) {
        new_modes = it->second;
    }

    for (const auto& [id, layer] : *all_print_modes) {
        if (!mode_ids_set.contains(id)) {
            new_modes[id] = layer;
        }
    }
    // Orphans are additive, erase anything tham might have been an orphan before, but is loaded now.
    for (const std::string& id : mode_ids_set) {
        new_modes.erase(id);
    }

    if (it == m_orphaned_modes.end() || new_modes != it->second) {
        m_orphaned_modes[config_container_id] = new_modes;
        return true;
    }
    return false;
}

void PrintModeInteractor::reload_all(const bool modes_dropped)
{
    bool removed_incompatible{false};
    if (m_project_interactor.selected_config_container_id() != Domain::INVALID_ID) {
        removed_incompatible = remove_incompatible_modes();
    }
    const bool reloaded_config_values{reload_config_values()};
    const bool reloaded_mode_params{reload_mode_params()};
    const bool reloaded_orphaned_modes{reload_orphaned_modes()};
    if (modes_dropped || removed_incompatible || reloaded_config_values
        || reloaded_mode_params || reloaded_orphaned_modes) {
        reevaluate();
        invoke_listeners<IPrintModesChangedListener>(
            [](auto* listener) { listener->on_print_modes_changed(); });
    }
}

void
PrintModeInteractor::on_config_container_selection_changed(Domain::SelectionId, Domain::SelectionId)
{
    reload_all();
}

void PrintModeInteractor::on_preset_selection_changed(
    Domain::SelectionId, Domain::SelectionId, const Preset::PresetItemType type)
{
    // The printer decides which modes are available and the orphans.
    if (type == Preset::PresetItemType::PrinterPreset) {
        const bool dropped{drop_modes()};
        reload_all(dropped);
    }
}

void PrintModeInteractor::on_hw_item_selection_changed(
    Domain::SelectionId, Domain::SelectionId, Preset::HwItemType)
{
    reload_all();
}

void PrintModeInteractor::on_background_process_state_changed(const Domain::SlicingId id)
{
    if (id != m_project_interactor.selected_bed_slicing_id()) {
        return;
    }
    reload_all();
}

} // namespace Slic3r::Biz
