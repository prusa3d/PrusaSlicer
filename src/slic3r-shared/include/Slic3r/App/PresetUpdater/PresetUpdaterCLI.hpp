#pragma once

#include "Slic3r/Biz/PresetUpdater/IPresetUpdaterResultListener.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterInteractor.hpp"
#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "Slic3r/App/Init.hpp"

namespace Slic3r::App {

class PresetUpdaterCLI : public Biz::PresetUpdater::IPresetUpdaterResultListener
{
public:
    PresetUpdaterCLI(Biz::PresetUpdater::PresetUpdaterInteractor& preset_updater_interactor);

    void start(const ActionParams& action, const std::string data);

    void on_preset_updater_error(
        Biz::PresetUpdater::JobId job_id,
        const std::string& body,
        Biz::PresetUpdater::PresetUpdaterReason reason
    ) override;

    void on_preset_updater_forced_reconfigurations_list(
        Biz::PresetUpdater::JobId job_id,
        const Biz::PresetUpdater::PresetUpdaterReconfigurationList& reconfigurations,
        const std::vector<Biz::PresetUpdater::PresetUpdaterWarning>& warnings
    ) override;

    void on_preset_updater_reconfigurations_list(
        Biz::PresetUpdater::JobId job_id,
        const Biz::PresetUpdater::PresetUpdaterReconfigurationList& reconfigurations,
        const std::vector<Biz::PresetUpdater::PresetUpdaterWarning>& warnings,
        Biz::PresetUpdater::VerboseStyle verbose
    ) override;

    void on_preset_updater_reconfigurations_performed(
        Biz::PresetUpdater::JobId job_id,
        const std::vector<Biz::PresetUpdater::PresetUpdaterWarning>& warnings
    ) override;

    void on_preset_updater_status(Biz::PresetUpdater::JobId job_id, const std::string& target, int attempt, unsigned delay, Biz::PresetUpdater::VerboseStyle verbose) override;

    void on_preset_updater_repository_info_vector(
        Biz::PresetUpdater::JobId job_id,
        const Biz::PresetUpdater::SharedPresetUpdaterRepositoryInfoVector& descriptor,
        const std::vector<Biz::PresetUpdater::PresetUpdaterWarning>& warnings
    ) override;

    void on_preset_updater_repository_selection_performed(
        Biz::PresetUpdater::JobId job_id,
        const Biz::PresetUpdater::SharedPresetUpdaterRepositoryInfoVector& descriptor,
        const std::vector<Biz::PresetUpdater::PresetUpdaterWarning>& warnings
    ) override;

    void on_preset_updater_job_finished(
        Biz::PresetUpdater::JobId job_id, Biz::PresetUpdater::JobState state
    ) override;

    bool has_result()
    {
        return m_has_result;
    }

private:
    Biz::PresetUpdater::PresetUpdaterInteractor& m_preset_updater_interactor;
    bool m_has_result{false};
    bool m_perform_after_sync{false};
    std::string m_repo_to_switch;
};

} // namespace Slic3r::App
