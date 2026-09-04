#pragma once

#include "Slic3r/Biz/PresetUpdater/IPresetUpdaterResultListener.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterInteractor.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterReason.hpp"
#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "Slic3r/App/Init.hpp"

#include <set>

namespace Slic3r::Biz::PresetUpdater {

enum class ReconfigurationResult
{
    Update, 
    ForcedUpdate,
    ForcedDowngrade,
    NotInIndex,
    NewVendor,
    None,
    Error,
};

struct ExpectedReconfiguration
{
    ReconfigurationResult state;
    Semver version;
    std::set<PresetUpdaterReason> reasons;
};

class TestPresetUpdaterListener : public IPresetUpdaterResultListener
{
public:
    TestPresetUpdaterListener(PresetUpdaterInteractor& preset_updater_interactor);

    /// The listener dies before the interactor, and the dispatcher still runs what it has queued.
    ~TestPresetUpdaterListener() override;

    TestPresetUpdaterListener(const TestPresetUpdaterListener&)            = delete;
    TestPresetUpdaterListener& operator=(const TestPresetUpdaterListener&) = delete;

    void start(ExpectedReconfiguration expected);

    void on_preset_updater_error(
        JobId job_id, const std::string& body, PresetUpdaterReason reason
    ) override;

    void on_preset_updater_forced_reconfigurations_list(
        JobId job_id,
        const PresetUpdaterReconfigurationList& reconfigurations,
        const std::vector<PresetUpdaterWarning>& warnings
    ) override;

    void on_preset_updater_reconfigurations_list(
        JobId job_id,
        const PresetUpdaterReconfigurationList& reconfigurations,
        const std::vector<PresetUpdaterWarning>& warnings,
        Biz::PresetUpdater::VerboseStyle verbose
    ) override;

    void on_preset_updater_reconfigurations_performed(
        JobId job_id,
        const std::vector<PresetUpdaterWarning>& warnings
    ) override;

    void on_preset_updater_status(JobId job_id, const std::string& target, int attempt, unsigned delay, Biz::PresetUpdater::VerboseStyle verbose) override;

    void on_preset_updater_repository_info_vector(
        JobId job_id,
        const SharedPresetUpdaterRepositoryInfoVector& descriptor,
        const std::vector<PresetUpdaterWarning>& warnings
    ) override;

    bool has_result()
    {
        return m_has_result;
    }

private:
    void check_reasons(const std::vector<PresetUpdaterWarning>& warnings);

    PresetUpdaterInteractor& m_preset_updater_interactor;
    bool m_has_result{false};
    ExpectedReconfiguration m_expected;
};

} // namespace Slic3r::Biz::PresetUpdater