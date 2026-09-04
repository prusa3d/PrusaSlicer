#pragma once

#include "Slic3r/App/PresetUpdater/PresetUpdaterInstallNotification.hpp"
#include "Slic3r/App/PresetUpdater/PresetUpdaterProblemNotification.hpp"
#include "Slic3r/App/PresetUpdater/PresetUpdaterStatusNotification.hpp"

#include "Slic3r/Biz/PresetUpdater/PresetUpdaterJob.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterWarning.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Slic3r::Biz::PresetUpdater {
class PresetUpdaterInteractor;
} // namespace Slic3r::Biz::PresetUpdater

namespace Slic3r::App {

class PresetUpdaterActivityReporter
{
public:
    using Activity        = PresetUpdaterStatusNotification::Activity;
    using InstalledVendor = PresetUpdaterInstallNotification::InstalledVendor;
    using Problem         = PresetUpdaterProblemNotification::Problem;

    explicit PresetUpdaterActivityReporter(
        Biz::PresetUpdater::PresetUpdaterInteractor& preset_updater_interactor
    );

    /// Registers a job as work the user asked for and refreshes the progress notification.
    void begin_activity(Biz::PresetUpdater::JobId job_id, Activity activity);

    /// Same as begin_activity for a check, without the progress notification.
    void begin_silent_check();

    /// Retires a job. The notification closes once nothing the user asked for is left.
    void end_activity(Biz::PresetUpdater::JobId job_id);

    bool tracks(Biz::PresetUpdater::JobId job_id) const;

    /// Names what the running job is working on. Ignored for an untracked job.
    void report_progress(Biz::PresetUpdater::JobId job_id, const std::string& target, int attempt);

    void report_install_finished(const std::vector<InstalledVendor>& installed);

    void report_updates_available(size_t update_count);

    void report_check_finished(size_t update_count);

    /// Reports everything the last check or install ran into. An empty list takes it down.
    void report_problems(std::vector<Problem> problems);

    void set_dialog_open(bool dialog_open);

    /// @param subject what the job was about, when its failure has something of its own to name.
    void report_error(Biz::PresetUpdater::PresetUpdaterReason reason, std::string subject);

    void set_show_dialog_callback(std::function<void()> callback);

    /// Forgets everything in flight. For teardown only, so it never touches the notifications.
    void reset();

private:
    PresetUpdaterStatusNotification m_status;
    PresetUpdaterInstallNotification m_installed;
    PresetUpdaterProblemNotification m_problems;

    bool m_dialog_open{false};

    std::function<void()> m_show_dialog_callback;
};

} // namespace Slic3r::App
