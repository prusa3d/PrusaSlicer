#include "Slic3r/App/PresetUpdater/PresetUpdaterActivityReporter.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/PopNotification/PopNotificationCenter.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterInteractor.hpp"

#include <fmt/format.h>

#include <chrono>
#include <utility>

namespace Slic3r::App {

PresetUpdaterActivityReporter::PresetUpdaterActivityReporter(
    Biz::PresetUpdater::PresetUpdaterInteractor& preset_updater_interactor
) :
    m_status(preset_updater_interactor)
{}

void PresetUpdaterActivityReporter::begin_activity(
    Biz::PresetUpdater::JobId job_id, Activity activity
)
{
    if (job_id == Biz::PresetUpdater::k_invalid_job_id) {
        return;
    }
    if (activity == Activity::Checking) {
        m_problems.rearm();
    }
    m_status.begin_activity(job_id, activity);
}

void PresetUpdaterActivityReporter::begin_silent_check()
{
    m_problems.rearm();
}

void PresetUpdaterActivityReporter::end_activity(Biz::PresetUpdater::JobId job_id)
{
    m_status.end_activity(job_id);
}

bool PresetUpdaterActivityReporter::tracks(Biz::PresetUpdater::JobId job_id) const
{
    return m_status.tracks(job_id);
}

void PresetUpdaterActivityReporter::report_progress(
    Biz::PresetUpdater::JobId job_id, const std::string& target, int attempt
)
{
    m_status.report_progress(job_id, target, attempt);
}

void PresetUpdaterActivityReporter::report_install_finished(
    const std::vector<InstalledVendor>& installed
)
{
    m_installed.report_install_finished(installed);
}

void PresetUpdaterActivityReporter::report_problems(std::vector<Problem> problems)
{
    m_problems.report_problems(std::move(problems));
}

void PresetUpdaterActivityReporter::report_updates_available(size_t update_count)
{
    using namespace PopNotification;

    if (m_dialog_open || update_count == 0 || !m_show_dialog_callback) {
        return;
    }

    // TRN Preset updater notification header.
    const std::string header = Biz::_u8L("Configuration updates available");
    // TRN Preset updater notification. {} counts the updates found.
    const std::string format = Biz::_L_PLURAL_u8(
        "{} preset update is ready to install.",
        "{} preset updates are ready to install.",
        static_cast<int>(update_count)
    );
    const std::string body = fmt::format(fmt::runtime(format), update_count);

    AppServices::instance().pop_notification_center().upsert_notification(
        PopNotificationData{
            PopNotificationType::PresetUpdateAvailable,
            PopNotificationLevel::Regular,
            std::chrono::seconds{0},
            PopNotificationLayoutHeaderTextButtons{
                header,
                body,
                {
                    // TRN Preset updater notification button. Opens the preset updater dialog.
                    {Biz::_u8L("Show"),
                     [callback = m_show_dialog_callback]()
                     {
                         callback();
                         return true;
                     }},
                }
            }
        },
        always_equal_matcher
    );

    // This one now carries the way into the dialog, so the problem notification gives its own up.
    m_problems.refresh();
}

void PresetUpdaterActivityReporter::report_check_finished(size_t update_count)
{
    if (m_dialog_open) {
        return;
    }
    if (update_count > 0) {
        report_updates_available(update_count);
    }
}

void PresetUpdaterActivityReporter::set_dialog_open(bool dialog_open)
{
    if (m_dialog_open == dialog_open) {
        return;
    }
    m_dialog_open = dialog_open;

    if (m_dialog_open) {
        AppServices::instance().pop_notification_center().close_notifications_of_type(
            PopNotification::PopNotificationType::PresetUpdateAvailable
        );
    }

    m_status.set_dialog_open(dialog_open);
    m_problems.set_dialog_open(dialog_open);
}

void PresetUpdaterActivityReporter::report_error(
    Biz::PresetUpdater::PresetUpdaterReason reason, std::string subject
)
{
    m_problems.report_error(reason, std::move(subject));
}

void PresetUpdaterActivityReporter::set_show_dialog_callback(std::function<void()> callback)
{
    m_show_dialog_callback = std::move(callback);
    m_problems.set_show_dialog_callback(m_show_dialog_callback);
}

void PresetUpdaterActivityReporter::reset()
{
    m_status.reset();
    m_installed.reset();
    m_problems.reset();
}

} // namespace Slic3r::App
