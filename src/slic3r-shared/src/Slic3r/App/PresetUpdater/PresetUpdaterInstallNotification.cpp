#include "Slic3r/App/PresetUpdater/PresetUpdaterInstallNotification.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/PopNotification/PopNotificationCenter.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>

namespace Slic3r::App {

namespace {

using Biz::PresetUpdater::VendorReconfigurationState;

constexpr std::chrono::seconds k_summary_timeout{20};

constexpr size_t k_summary_limit{5};

bool summary_shown()
{
    using namespace PopNotification;

    PopNotificationObservableList& list =
        AppServices::instance().pop_notification_center().observable_list();
    for (size_t index = 0; index < list.size(); ++index) {
        if (list.at(index).type == PopNotificationType::PresetUpdaterInstalled) {
            return true;
        }
    }
    return false;
}

} // namespace

void PresetUpdaterInstallNotification::report_install_finished(
    const std::vector<InstalledVendor>& installed
)
{
    if (installed.empty()) {
        return;
    }

    if (!summary_shown()) {
        m_installed.clear();
    }

    for (const InstalledVendor& vendor : installed) {
        const auto listed = std::find_if(
            m_installed.begin(),
            m_installed.end(),
            [&vendor](const InstalledVendor& entry) { return entry.vendor_id == vendor.vendor_id; }
        );
        if (listed != m_installed.end()) {
            *listed = vendor;
            continue;
        }
        m_installed.push_back(vendor);
    }

    refresh();
}

void PresetUpdaterInstallNotification::reset()
{
    m_installed.clear();
}

void PresetUpdaterInstallNotification::refresh()
{
    using namespace PopNotification;

    PopNotificationCenter& center = AppServices::instance().pop_notification_center();

    if (m_installed.empty()) {
        center.close_notifications_of_type(PopNotificationType::PresetUpdaterInstalled);
        return;
    }

    std::string body;
    for (size_t index = 0; index < m_installed.size() && index < k_summary_limit; ++index) {
        const InstalledVendor& vendor = m_installed[index];
        if (!body.empty()) {
            body += "\n";
        }
        if (vendor.state == VendorReconfigurationState::RemoveVendor) {
            // TRN Preset updater notification. {} is a vendor name, its presets were deleted.
            const std::string removed = Biz::_u8L("{} removed");
            body += fmt::format(fmt::runtime(removed), vendor.vendor_id);
        } else if (vendor.state == VendorReconfigurationState::NewVendor) {
            body += fmt::format("{} {}", vendor.vendor_id, vendor.to.to_string());
        } else {
            body += fmt::format(
                "{} {} -> {}", vendor.vendor_id, vendor.from.to_string(), vendor.to.to_string()
            );
        }
    }
    if (m_installed.size() > k_summary_limit) {
        body += "\n";
        // TRN Preset updater notification. {} counts the vendors not listed above.
        const std::string more = Biz::_u8L("...and {} more.");
        body += fmt::format(fmt::runtime(more), m_installed.size() - k_summary_limit);
    }

    center.upsert_notification(
        PopNotificationData{
            PopNotificationType::PresetUpdaterInstalled,
            PopNotificationLevel::Regular,
            k_summary_timeout,
            PopNotificationLayoutHeaderText{
                // TRN Preset updater notification header.
                Biz::_u8L("Presets installed"),
                body
            }
        },
        always_equal_matcher
    );
}

} // namespace Slic3r::App
