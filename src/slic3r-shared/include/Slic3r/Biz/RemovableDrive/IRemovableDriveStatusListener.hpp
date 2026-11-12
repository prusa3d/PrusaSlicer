#pragma once

namespace boost::filesystem {
class path;
} // namespace boost::filesystem

namespace Slic3r::Biz::RemovableDrive {

enum class RemovableDriveStatus
{
    Inserted,
    Ejecting,
    Removed,
    Failed,
};

class IRemovableDriveStatusListener
{
public:
    virtual void on_removable_drive_status_changed(const boost::filesystem::path& drive_path, RemovableDriveStatus status) = 0;
};
} // namespace Slic3r::Biz::RemovableDrive
