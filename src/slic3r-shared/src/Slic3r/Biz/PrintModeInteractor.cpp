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
    for (const std::string& id : m_provider->modes()) {
        Domain::PrintModeParams params;
        const auto it{mode_params.find(id)};
        if (it != mode_params.end()) {
            params = it->second;
        }
        m_modes[id] = m_provider->evaluate(id, m_config_values, params);
    }
    apply_evaluated();
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

    const std::vector<std::string> mode_ids{m_provider->modes()};
    std::set<std::string> mode_ids_set{mode_ids.begin(), mode_ids.end()};

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
    for (const std::string& id : mode_ids) {
        new_modes.erase(id);
    }

    if (it == m_orphaned_modes.end() || new_modes != it->second) {
        m_orphaned_modes[config_container_id] = new_modes;
        return true;
    }
    return false;
}

void
PrintModeInteractor::on_config_container_selection_changed(Domain::SelectionId, Domain::SelectionId)
{
    const bool reloaded_config_values{reload_config_values()};
    const bool reloaded_mode_params{reload_mode_params()};
    const bool reloaded_orphaned_modes{reload_orphaned_modes()};
    if (reloaded_config_values || reloaded_mode_params || reloaded_orphaned_modes) {
        reevaluate();
        invoke_listeners<IPrintModesChangedListener>(
            [](auto* listener) { listener->on_print_modes_changed(); });
    }
}

void PrintModeInteractor::on_background_process_state_changed(const Domain::SlicingId id)
{
    if (id != m_project_interactor.selected_bed_slicing_id()) {
        return;
    }
    if (reload_config_values()) {
        reevaluate();
        invoke_listeners<IPrintModesChangedListener>(
            [](auto* listener) { listener->on_print_modes_changed(); });
    }
}

} // namespace Slic3r::Biz
