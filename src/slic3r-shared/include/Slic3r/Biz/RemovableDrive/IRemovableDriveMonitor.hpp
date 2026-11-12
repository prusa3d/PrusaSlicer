#pragma once

#include "Slic3r/Biz/RemovableDrive/RemovableDriveData.hpp"
#include "Slic3r/Biz/RemovableDrive/IRemovableDriveStatusListener.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"

#include <string>
#include <vector>
#include <boost/filesystem/path.hpp>

namespace Slic3r::Biz::RemovableDrive {

class IRemovableDriveMonitor : public WithListeners<IRemovableDriveStatusListener>
{
public:
    IRemovableDriveMonitor()                                         = default;
    IRemovableDriveMonitor(const IRemovableDriveMonitor&)            = default;
    IRemovableDriveMonitor(IRemovableDriveMonitor&&)                 = default;
    IRemovableDriveMonitor& operator=(const IRemovableDriveMonitor&) = default;
    IRemovableDriveMonitor& operator=(IRemovableDriveMonitor&&)      = default;

    virtual ~IRemovableDriveMonitor() = default;

    /**
     * @brief Called from outside to notify Monitor to re-enumerate drives.
     */
    virtual void on_volumes_changed() = 0;

    /**
     * @brief Returns path to removable drive if any exists. Prefarably one with preferred_path.
     */
    virtual boost::filesystem::path get_path_on_removable_drive(const boost::filesystem::path& preferred_path) const = 0;

    /**
     * @brief Returns path to removable drive if "path" is on removable.
     */
    virtual boost::filesystem::path get_removable_drive_path_from_path(const boost::filesystem::path& path) const = 0;

    /**
     * @brief Returns number of removable drives.
     */
    virtual size_t removable_drives_count() const = 0;
};
} // namespace Slic3r::Biz::RemovableDrive
