#pragma once

#include "Slic3r/Biz/PresetUpdater/PresetUpdaterReconfigurationList.hpp"
#include "Slic3r/Semver.hpp"

#include <string>
#include <vector>

namespace Slic3r::App {

class PresetUpdaterInstallNotification
{
public:
    struct InstalledVendor
    {
        std::string vendor_id;
        Slic3r::Semver from;
        Slic3r::Semver to;
        Biz::PresetUpdater::VendorReconfigurationState state;
    };

    void report_install_finished(const std::vector<InstalledVendor>& installed);

    /// Drops what was collected. For teardown only, so it never touches the notification center.
    void reset();

private:
    void refresh();

    std::vector<InstalledVendor> m_installed;
};

} // namespace Slic3r::App
