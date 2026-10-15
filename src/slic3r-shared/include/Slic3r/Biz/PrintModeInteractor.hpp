#pragma once

#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Preset/IMacroOverridesChangedListener.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"
#include "Slic3r/Domain/MacroOverrides.hpp"

#include <map>
#include <string>
#include <vector>

namespace Slic3r::Biz {

class ProjectInteractor;

/// An evaluated print mode. When the evaluation failed, `error` holds the reason, the layer
/// still carries the name and the parameters, and the overrides are empty.
struct PrintMode
{
    Domain::MacroOverridesLayerFDM layer;
    std::optional<std::string> error;
};

using PrintModes = std::map<std::string, PrintMode>;

class IPrintModesProvider
{
public:
    virtual ~IPrintModesProvider() = default;

    virtual std::vector<std::string> modes() const = 0;

    virtual PrintMode evaluate(
        const std::string& id,
        const std::map<std::string, Domain::ConfigValue>& inputs,
        const Domain::PrintModeParams& params
    ) const = 0;
};

class NoopPrintModesProvider : public IPrintModesProvider
{
public:
    std::vector<std::string> modes() const override
    {
        return {};
    }

    PrintMode evaluate(
        const std::string& id,
        const std::map<std::string, Domain::ConfigValue>& inputs,
        const Domain::PrintModeParams& params) const override
    {
        return {{}, "No print modes provider"};
    }
};

class IPrintModesChangedListener
{
public:
    virtual ~IPrintModesChangedListener() = default;
    virtual void on_print_modes_changed() = 0;
};

class PrintModeInteractor :
    public WithListeners<IPrintModesChangedListener>,
    public Preset::IPresetChangedListener,
    public Slicing::IBackgroundProcessStateListener
{
public:
    explicit PrintModeInteractor(ProjectInteractor& project_interactor);

    void set_provider(const IPrintModesProvider* provider);
    /// Re-reads the modes from the provider, e.g. after plugins were rescanned.
    void reload_provider();

    void set_mode_params(const std::string& id, const Domain::PrintModeParams& params);

    /// Applies the evaluated layer of the mode (or the stored one of an orphan) to the selected
    /// preset, or removes it. A mode whose evaluation failed is not applied.
    void set_active(const std::string& id, bool active);

    void on_config_container_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;
    void on_background_process_state_changed(Domain::SlicingId id) override;

    const PrintModes& modes() const
    {
        return m_modes;
    }

    const Domain::MacroOverridesMapFDM& orphaned_modes() const;

private:
    void reevaluate();
    void apply_evaluated();
    bool reload_config_values();
    bool reload_mode_params();
    bool reload_orphaned_modes();

    ProjectInteractor& m_project_interactor;
    const IPrintModesProvider* m_provider{nullptr};
    NoopPrintModesProvider m_noop_provider;
    using ModeParams = std::map<std::string, Domain::PrintModeParams>;
    std::map<Domain::SelectionId, ModeParams> m_mode_params;
    PrintModes m_modes;
    std::map<Domain::SelectionId, Domain::MacroOverridesMapFDM> m_orphaned_modes;
    std::map<std::string, Domain::ConfigValue> m_config_values;
};

} // namespace Slic3r::Biz
