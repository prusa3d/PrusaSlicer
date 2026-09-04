#include "Slic3r/App/PresetUpdater/PresetUpdaterStatusNotification.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/PopNotification/PopNotificationCenter.hpp"

#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterInteractor.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterRepositoryDescriptor.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>

namespace Slic3r::App {

namespace {

using Activity = PresetUpdaterStatusNotification::Activity;

std::string activity_header(Activity activity)
{
    switch (activity) {
    case Activity::ListingSources:
        // TRN Preset updater notification header.
        return Biz::_u8L("Updating sources...");
    case Activity::ApplyingSelection:
        // TRN Preset updater notification header.
        return Biz::_u8L("Saving source selection...");
    case Activity::AddingSource:
        // TRN Preset updater notification header.
        return Biz::_u8L("Adding source...");
    case Activity::RemovingSource:
        // TRN Preset updater notification header.
        return Biz::_u8L("Removing source...");
    case Activity::Checking:
        // TRN Preset updater notification header.
        return Biz::_u8L("Checking for updates...");
    case Activity::Installing:
        // TRN Preset updater notification header.
        return Biz::_u8L("Installing presets...");
    }
    ASSERT(false, "Missing Activity handling.");
    return {};
}

std::string activity_body(Activity activity, const std::string& target)
{
    switch (activity) {
    case Activity::ListingSources:
        // TRN Preset updater notification.
        return Biz::_u8L("Downloading the list of sources");
    case Activity::ApplyingSelection:
        // TRN Preset updater notification. Body under "Saving source selection...".
        return Biz::_u8L("Writing the choice to the application configuration.");
    case Activity::AddingSource:
        if (target.empty()) {
            // TRN Preset updater notification. Body under "Adding source...".
            return Biz::_u8L("Unpacking the archive and reading the presets it offers.");
        }
        // TRN Preset updater notification. {} is the archive being unpacked.
        return fmt::format(fmt::runtime(Biz::_u8L("Unpacking {}.")), target);
    case Activity::RemovingSource:
        // TRN Preset updater notification. Body under "Removing source...".
        return Biz::_u8L("Deleting the presets unpacked from the archive.");
    case Activity::Checking:
        if (target.empty()) {
            // TRN Preset updater notification. Body under "Checking for updates...".
            return Biz::_u8L("Comparing the installed presets with what the sources offer.");
        }
        // TRN Preset updater notification. {} is the file being downloaded.
        return fmt::format(fmt::runtime(Biz::_u8L("Downloading {}.")), target);
    case Activity::Installing:
        if (target.empty()) {
            // TRN Preset updater notification. Body under "Installing presets...".
            return Biz::_u8L("Copying the new presets into the configuration.");
        }
        // TRN Preset updater notification. {} is the vendor being installed.
        return fmt::format(fmt::runtime(Biz::_u8L("Installing {}.")), target);
    }
    ASSERT(false, "Missing Activity handling.");
    return {};
}

bool is_cancelable(Activity activity)
{
    return activity == Activity::Checking;
}

} // namespace

PresetUpdaterStatusNotification::PresetUpdaterStatusNotification(
    Biz::PresetUpdater::PresetUpdaterInteractor& preset_updater_interactor
) :
    m_preset_updater_interactor(preset_updater_interactor)
{}

void PresetUpdaterStatusNotification::begin_activity(
    Biz::PresetUpdater::JobId job_id, Activity activity
)
{
    if (!m_preset_updater_interactor.is_job_pending(job_id)) {
        return;
    }
    if (m_activities.empty()) {
        m_activity_target.clear();
        m_activity_attempt = 0;
    }
    m_activities.emplace_back(job_id, activity);
    refresh();
}

void PresetUpdaterStatusNotification::end_activity(Biz::PresetUpdater::JobId job_id)
{
    const auto it = std::find_if(
        m_activities.begin(),
        m_activities.end(),
        [job_id](const std::pair<Biz::PresetUpdater::JobId, Activity>& entry)
        { return entry.first == job_id; }
    );
    if (it == m_activities.end()) {
        return;
    }

    const bool was_front = it == m_activities.begin();
    m_activities.erase(it);
    if (was_front) {
        m_activity_target.clear();
        m_activity_attempt = 0;
    }
    refresh();
}

bool PresetUpdaterStatusNotification::tracks(Biz::PresetUpdater::JobId job_id) const
{
    return std::any_of(
        m_activities.begin(),
        m_activities.end(),
        [job_id](const std::pair<Biz::PresetUpdater::JobId, Activity>& entry)
        { return entry.first == job_id; }
    );
}

void PresetUpdaterStatusNotification::report_progress(
    Biz::PresetUpdater::JobId job_id, const std::string& target, int attempt
)
{
    if (!tracks(job_id)) {
        return;
    }
    m_activity_target  = target;
    m_activity_attempt = attempt;
    refresh();
}

void PresetUpdaterStatusNotification::set_dialog_open(bool dialog_open)
{
    m_dialog_open = dialog_open;
    refresh();
}

void PresetUpdaterStatusNotification::reset()
{
    m_activities.clear();
    m_activity_target.clear();
    m_activity_attempt = 0;
}

void PresetUpdaterStatusNotification::refresh()
{
    using namespace PopNotification;

    PopNotificationCenter& center = AppServices::instance().pop_notification_center();

    const size_t tracked = m_activities.size();
    std::erase_if(
        m_activities,
        [this](const std::pair<Biz::PresetUpdater::JobId, Activity>& entry)
        { return !m_preset_updater_interactor.is_job_pending(entry.first); }
    );
    if (m_activities.size() != tracked) {
        m_activity_target.clear();
        m_activity_attempt = 0;
    }

    if (m_activities.empty()) {
        center.close_notifications_of_type(PopNotificationType::PresetUpdaterStatus);
        return;
    }

    const std::pair<Biz::PresetUpdater::JobId, Activity>& current = m_activities.front();

    // The manifest fetch reads as the source list being updated, not as a file download.
    Activity activity = current.second;
    if (activity == Activity::Checking
        && m_activity_target == Biz::PresetUpdater::k_manifest_download_target)
    {
        activity = Activity::ListingSources;
    }

    std::string body = activity_body(activity, m_activity_target);
    if (m_activity_attempt > 1) {
        body += "\n";
        // TRN Preset updater notification. {} is the retry number.
        const std::string retry = Biz::_u8L("(Attempt {}) The connection is taking longer than usual. Please wait.");
        body += fmt::format(fmt::runtime(retry), m_activity_attempt);
    }

    // A different layout type, not an empty button list: the view rebuilds only on a type change.
    PopNotificationLayout layout = PopNotificationLayoutHeaderText{activity_header(activity), body};
    if (!m_dialog_open && is_cancelable(current.second)) {
        const Biz::PresetUpdater::JobId job_id = current.first;
        layout = PopNotificationLayoutHeaderTextButtons{
            activity_header(activity),
            body,
            {
                // TRN Preset updater notification button. Stops the running job.
                {Biz::_u8L("Cancel"),
                 [this, job_id]()
                 {
                     m_preset_updater_interactor.cancel_job(job_id);
                     return true;
                 }},
            }
        };
    }

    center.upsert_notification(
        PopNotificationData{
            PopNotificationType::PresetUpdaterStatus,
            PopNotificationLevel::Regular,
            std::chrono::seconds{0},
            std::move(layout)
        },
        always_equal_matcher
    );
}

} // namespace Slic3r::App
