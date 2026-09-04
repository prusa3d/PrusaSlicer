#include "Slic3r/App/PresetUpdater/PresetUpdaterProblemNotification.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/PopNotification/PopNotificationCenter.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace Slic3r::App {

namespace {

using Biz::PresetUpdater::PresetUpdaterReason;

constexpr std::chrono::seconds k_dialog_closed_timeout{60};

bool updates_available_shown()
{
    using namespace PopNotification;

    PopNotificationObservableList& list =
        AppServices::instance().pop_notification_center().observable_list();
    for (size_t index = 0; index < list.size(); ++index) {
        if (list.at(index).type == PopNotificationType::PresetUpdateAvailable) {
            return true;
        }
    }
    return false;
}

std::string problem_sentence(PresetUpdaterReason reason)
{
    switch (reason) {
    case PresetUpdaterReason::NoUsableVersion:
        // TRN Preset updater notification. The names of the presets follow underneath.
        return Biz::_u8L("These presets are offered in no version this application can use, and were not checked:");
    case PresetUpdaterReason::IndexUnreadable:
    case PresetUpdaterReason::DataUnreadable:
        // TRN Preset updater notification. The names of the presets follow underneath.
        return Biz::_u8L("These presets could not be read from their source and were not checked:");
    case PresetUpdaterReason::DataCorrupted:
        // TRN Preset updater notification. The names of the presets follow underneath.
        return Biz::_u8L("These presets failed their integrity check and were not used:");
    case PresetUpdaterReason::DataInconsistent:
        // TRN Preset updater notification. The names of the presets follow underneath.
        return Biz::_u8L("These presets disagree with the version their source lists, and were not offered:");
    case PresetUpdaterReason::LocalStorageFailed:
        // TRN Preset updater notification. The names of the presets follow underneath.
        return Biz::_u8L("These presets could not be written to disk:");
    case PresetUpdaterReason::InstallFailed:
        // TRN Preset updater notification. The names of the presets follow underneath.
        return Biz::_u8L("These presets were not installed. What is installed has not changed:");
    case PresetUpdaterReason::SourceUnreachable:
        // TRN Preset updater notification. The names of the sources follow underneath.
        return Biz::_u8L("These sources could not be read. Nothing they offer is known:");
    case PresetUpdaterReason::SourceDropped:
        // TRN Preset updater notification. The names of the sources follow underneath.
        return Biz::_u8L("These sources are no longer on this computer and were removed from the list:");
    case PresetUpdaterReason::SourceListUnavailable:
        // TRN Preset updater notification.
        return Biz::_u8L("The list of online sources could not be updated. The sources known from the last run were used.");
    case PresetUpdaterReason::ManifestUnusable:
        // TRN Preset updater notification.
        return Biz::_u8L("Part of the list of preset sources could not be read. Some sources may be missing.");
    case PresetUpdaterReason::ArchiveInvalid:
    case PresetUpdaterReason::SourceNotFound:
    case PresetUpdaterReason::DataDirUnusable:
    case PresetUpdaterReason::Internal:
        break;
    }
    // TRN Preset updater notification. The affected names follow underneath.
    return Biz::_u8L("These presets could not be dealt with:");
}

std::string error_sentence(PresetUpdaterReason reason, const std::string& subject)
{
    switch (reason) {
    case PresetUpdaterReason::ArchiveInvalid:
        if (!subject.empty()) {
            // TRN Preset updater error notification. {} is the file the user picked.
            const std::string format = Biz::_u8L("{} is not a usable preset source. The source was not added.");
            return fmt::format(fmt::runtime(format), subject);
        }
        // TRN Preset updater error notification.
        return Biz::_u8L("The archive is not a usable preset source. The source was not added.");
    case PresetUpdaterReason::SourceNotFound:
        if (!subject.empty()) {
            // TRN Preset updater error notification. {} is the name of a preset source.
            const std::string format = Biz::_u8L("{} is no longer there. Nothing was changed.");
            return fmt::format(fmt::runtime(format), subject);
        }
        // TRN Preset updater error notification.
        return Biz::_u8L("That preset source is no longer there. Nothing was changed.");
    case PresetUpdaterReason::ManifestUnusable:
        // TRN Preset updater error notification.
        return Biz::_u8L("The list of preset sources could not be read. It is being rebuilt from the presets that came with the application.");
    case PresetUpdaterReason::DataDirUnusable:
        // TRN Preset updater error notification.
        return Biz::_u8L("A folder the preset updater works in is missing. Preset updates cannot run.");
    case PresetUpdaterReason::SourceListUnavailable:
        // TRN Preset updater error notification.
        return Biz::_u8L("The list of online sources could not be read.");
    case PresetUpdaterReason::LocalStorageFailed:
        // TRN Preset updater error notification.
        return Biz::_u8L("Preset files could not be written to disk.");
    case PresetUpdaterReason::InstallFailed:
        // TRN Preset updater error notification.
        return Biz::_u8L("The presets could not be installed.");
    case PresetUpdaterReason::NoUsableVersion:
    case PresetUpdaterReason::IndexUnreadable:
    case PresetUpdaterReason::DataUnreadable:
    case PresetUpdaterReason::DataCorrupted:
    case PresetUpdaterReason::DataInconsistent:
    case PresetUpdaterReason::SourceUnreachable:
    case PresetUpdaterReason::SourceDropped:
        // TRN Preset updater error notification.
        return Biz::_u8L("The preset source could not be read.");
    case PresetUpdaterReason::Internal:
        break;
    }
    // TRN Preset updater error notification.
    return Biz::_u8L("The preset updater ran into an unexpected problem.");
}

std::string problem_name(const PresetUpdaterProblemNotification::Problem& problem)
{
    if (problem.vendor_id.empty()) {
        return problem.source_name;
    }
    return problem.source_name + " / " + problem.vendor_id;
}

} // namespace

void PresetUpdaterProblemNotification::report_problems(std::vector<Problem> problems)
{
    if (m_problems == problems) {
        return;
    }
    m_problems = std::move(problems);
    m_timeout  = std::chrono::seconds{0};
    refresh_problems();
}

void PresetUpdaterProblemNotification::report_error(PresetUpdaterReason reason, std::string subject)
{
    m_error         = reason;
    m_error_subject = std::move(subject);
    m_timeout       = std::chrono::seconds{0};
    refresh_error();
}

void PresetUpdaterProblemNotification::rearm()
{
    m_problems.clear();
    m_error.reset();
    m_error_subject.clear();
    m_timeout = std::chrono::seconds{0};
    refresh();
}

void PresetUpdaterProblemNotification::set_dialog_open(bool dialog_open)
{
    m_dialog_open = dialog_open;
    m_timeout     = m_dialog_open ? std::chrono::seconds{0} : k_dialog_closed_timeout;
    refresh();
}

void PresetUpdaterProblemNotification::set_show_dialog_callback(std::function<void()> callback)
{
    m_show_dialog_callback = std::move(callback);
}

void PresetUpdaterProblemNotification::reset()
{
    m_problems.clear();
    m_error.reset();
    m_error_subject.clear();
}

void PresetUpdaterProblemNotification::refresh()
{
    refresh_problems();
    refresh_error();
}

void PresetUpdaterProblemNotification::refresh_problems()
{
    using namespace PopNotification;

    PopNotificationCenter& center = AppServices::instance().pop_notification_center();

    if (m_problems.empty()) {
        center.close_notifications_of_type(PopNotificationType::PresetUpdaterProblem);
        return;
    }

    const bool install_failed = std::any_of(
        m_problems.begin(),
        m_problems.end(),
        [](const Problem& problem)
        { return problem.reason == PresetUpdaterReason::InstallFailed; }
    );

    const std::string header = install_failed ?
        // TRN Preset updater notification header. An install did not go through.
        Biz::_u8L("Some presets were not installed") :
        // TRN Preset updater notification header.
        Biz::_u8L("Update check incomplete");

    // Grouped by reason: one sentence covers however many presets went the same way.
    std::string body;
    std::vector<PresetUpdaterReason> reported;
    for (const Problem& problem : m_problems) {
        if (std::find(reported.begin(), reported.end(), problem.reason) != reported.end()) {
            continue;
        }
        reported.push_back(problem.reason);

        std::string names;
        for (const Problem& listed : m_problems) {
            if (listed.reason != problem.reason || listed.source_name.empty()) {
                continue;
            }
            names += "\n";
            names += problem_name(listed);
        }

        std::string sentence = problem_sentence(problem.reason);
        // Nothing to name it by: the sentence stands on its own rather than trailing into a list.
        if (names.empty() && !sentence.empty() && sentence.back() == ':') {
            sentence.back() = '.';
        }

        if (!body.empty()) {
            body += "\n\n";
        }
        body += sentence;
        body += names;
    }

    // A different layout type, not an empty button list: the view rebuilds only on a type change.
    PopNotificationLayout layout = PopNotificationLayoutHeaderText{header, body};
    if (!m_dialog_open && m_show_dialog_callback && !updates_available_shown()) {
        layout = PopNotificationLayoutHeaderTextButtons{
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
        };
    }

    center.upsert_notification(
        PopNotificationData{
            PopNotificationType::PresetUpdaterProblem,
            PopNotificationLevel::Warning,
            m_timeout,
            std::move(layout)
        },
        always_equal_matcher
    );
}

void PresetUpdaterProblemNotification::refresh_error()
{
    using namespace PopNotification;

    PopNotificationCenter& center = AppServices::instance().pop_notification_center();

    if (!m_error.has_value()) {
        center.close_notifications_of_type(PopNotificationType::PresetUpdaterError);
        return;
    }

    center.upsert_notification(
        PopNotificationData{
            PopNotificationType::PresetUpdaterError,
            PopNotificationLevel::Error,
            m_timeout,
            PopNotificationLayoutHeaderText{
                // TRN Preset updater error notification header.
                Biz::_u8L("Preset updater error"),
                error_sentence(*m_error, m_error_subject)
            }
        },
        always_equal_matcher
    );
}

} // namespace Slic3r::App
