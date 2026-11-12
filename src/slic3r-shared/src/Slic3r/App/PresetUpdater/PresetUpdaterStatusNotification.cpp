#include "Slic3r/App/PresetUpdater/PresetUpdaterStatusNotification.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/PopNotification/PopNotificationCenter.hpp"

#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterInteractor.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>

namespace Slic3r::App {

namespace {

using Activity = PresetUpdaterStatusNotification::Activity;
using Progress = Biz::PresetUpdater::PresetUpdaterProgress;
using Step     = Biz::PresetUpdater::PresetUpdaterStep;

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
    case Activity::RemovingVendor:
        // TRN Preset updater notification header.
        return Biz::_u8L("Removing presets...");
    }
    ASSERT(false, "Missing Activity handling.");
    return {};
}

std::string vendor_subject(const Progress& progress)
{
    const bool in_batch = progress.total > 1;
    switch (progress.step) {
    case Step::Downloading:
        if (in_batch) {
            return fmt::format(
                // TRN Preset updater notification. Vendor, version, then its place among the vendors.
                fmt::runtime(Biz::_u8L("Downloading {} {} ({} of {})")),
                progress.vendor_id, progress.version, progress.index, progress.total
            );
        }
        return fmt::format(
            // TRN Preset updater notification. {} is a vendor, {} the version being downloaded.
            fmt::runtime(Biz::_u8L("Downloading {} {}")), progress.vendor_id, progress.version
        );
    case Step::Installing:
        if (in_batch) {
            return fmt::format(
                // TRN Preset updater notification. Vendor, version, then its place among the vendors.
                fmt::runtime(Biz::_u8L("Installing {} {} ({} of {})")),
                progress.vendor_id, progress.version, progress.index, progress.total
            );
        }
        return fmt::format(
            // TRN Preset updater notification. {} is a vendor, {} the version being installed.
            fmt::runtime(Biz::_u8L("Installing {} {}")), progress.vendor_id, progress.version
        );
    case Step::Removing:
        if (in_batch) {
            return fmt::format(
                // TRN Preset updater notification. Vendor, then its place among the vendors.
                fmt::runtime(Biz::_u8L("Removing {} ({} of {})")),
                progress.vendor_id, progress.index, progress.total
            );
        }
        // TRN Preset updater notification. {} is the vendor whose presets are being deleted.
        return fmt::format(fmt::runtime(Biz::_u8L("Removing {}")), progress.vendor_id);
    case Step::None:
        return {};
    }
    ASSERT(false, "Missing PresetUpdaterStep handling.");
    return {};
}

std::string waiting_body(Activity activity)
{
    switch (activity) {
    case Activity::ListingSources:
        // TRN Preset updater notification.
        return Biz::_u8L("Downloading the list of sources");
    case Activity::ApplyingSelection:
        // TRN Preset updater notification. Body under "Saving source selection...".
        return Biz::_u8L("Writing the choice to the application configuration.");
    case Activity::AddingSource:
        // TRN Preset updater notification. Body under "Adding source...".
        return Biz::_u8L("Unpacking the archive and reading the presets it offers.");
    case Activity::RemovingSource:
        // TRN Preset updater notification. Body under "Removing source...".
        return Biz::_u8L("Deleting the presets unpacked from the archive.");
    case Activity::Checking:
        // TRN Preset updater notification. Body under "Checking for updates...".
        return Biz::_u8L("Comparing the installed presets with what the sources offer.");
    case Activity::Installing:
        // TRN Preset updater notification. Body under "Installing presets...".
        return Biz::_u8L("Copying the new presets into the configuration.");
    case Activity::RemovingVendor:
        // TRN Preset updater notification. Body under "Removing presets...".
        return Biz::_u8L("Deleting the presets that were installed.");
    }
    ASSERT(false, "Missing Activity handling.");
    return {};
}

std::string activity_body(Activity activity, const Progress& progress)
{
    const std::string subject = vendor_subject(progress);
    if (subject.empty()) {
        return waiting_body(activity);
    }
    return subject;
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
        m_activity_progress = {};
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
        m_activity_progress = {};
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
    Biz::PresetUpdater::JobId job_id, const Biz::PresetUpdater::PresetUpdaterProgress& progress
)
{
    if (!tracks(job_id)) {
        return;
    }
    m_activity_progress = progress;
    refresh();
}

void PresetUpdaterStatusNotification::reset()
{
    m_activities.clear();
    m_activity_progress = {};
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
        m_activity_progress = {};
    }

    if (m_activities.empty()) {
        center.close_notifications_of_type(PopNotificationType::PresetUpdaterStatus);
        return;
    }

    const Activity activity = m_activities.front().second;

    std::string body = activity_body(activity, m_activity_progress);
    if (m_activity_progress.attempt > 1) {
        body += "\n";
        // TRN Preset updater notification. {} is the retry number.
        const std::string retry = Biz::_u8L("(Attempt {}) The connection is taking longer than usual. Please wait.");
        body += fmt::format(fmt::runtime(retry), m_activity_progress.attempt);
    }

    center.upsert_notification(
        PopNotificationData{
            PopNotificationType::PresetUpdaterStatus,
            PopNotificationLevel::Regular,
            std::chrono::seconds{0},
            PopNotificationLayoutHeaderText{activity_header(activity), body}
        },
        always_equal_matcher
    );
}

} // namespace Slic3r::App
