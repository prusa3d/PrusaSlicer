#pragma once

#include "Slic3r/Biz/RemovableDrive/IRemovableDriveMonitor.hpp"
#include "Slic3r/Biz/RemovableDrive/IRemovableDriveStatusListener.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"

#include <jthread/JThread.hpp>
#include <memory>
#include <boost/filesystem/path.hpp>
#include <libassert/assert.hpp>

namespace Slic3r::Biz::RemovableDrive {

class RemovableDriveService : public WithListeners<IRemovableDriveStatusListener>
{
public:
    RemovableDriveService(Platform::IMainThreadDispatcher& dispatcher);
    RemovableDriveService(const RemovableDriveService&)            = delete;
    RemovableDriveService(RemovableDriveService&&)                 = delete;
    RemovableDriveService& operator=(const RemovableDriveService&) = delete;
    RemovableDriveService& operator=(RemovableDriveService&&)      = delete;

    ~RemovableDriveService() = default;

    void add_status_listener(IRemovableDriveStatusListener* listener)
    {
        add_listener<IRemovableDriveStatusListener>(listener);
        m_monitor->add_listener<IRemovableDriveStatusListener>(listener);
    }

    void remove_status_listener(IRemovableDriveStatusListener* listener)
    {
        remove_listener<IRemovableDriveStatusListener>(listener);
        m_monitor->remove_listener<IRemovableDriveStatusListener>(listener);
    }

    /**
     * @brief Returns path to removable drive if any exists. Prefarably one with preferred_path.
     */
    boost::filesystem::path get_path_on_removable_drive(const boost::filesystem::path& preferred_path) const
    {
        return m_monitor->get_path_on_removable_drive(preferred_path);
    }

    bool is_path_on_removable_drive(const boost::filesystem::path& path) const
    {
        return !m_monitor->get_removable_drive_path_from_path(path).empty();
    }

    /**
     * @brief Performs eject of removable drive in worker thread.
     * Fail is dispatch from the worker thread.
     * Success must be first detected by m_monitor (takes up to 2 second on Linux).
     */
    void eject_drive(const boost::filesystem::path& path)
    {
        ASSERT(!path.empty());

        boost::filesystem::path drive_path = m_monitor->get_removable_drive_path_from_path(path);
        if (drive_path.empty()) {
            return;
        }
        dispatch_status(drive_path, RemovableDriveStatus::Ejecting);
        eject_in_thread(drive_path);
    }

    bool has_removable_drives() const
    {
        return m_monitor->removable_drives_count() > 0;
    }

    /**
     * @brief Called from outside to notify m_monitor to re-enumerate drives.
     */
    void handle_volumes_changed_event()
    {
        m_monitor->on_volumes_changed();
    }

private:
    std::unique_ptr<IRemovableDriveMonitor> m_monitor;
    Platform::IMainThreadDispatcher& m_dispatcher;
    JThread::JThread m_eject_thread;

    void eject_in_thread(const boost::filesystem::path& path);

    void dispatch_status(const boost::filesystem::path& drive_path, RemovableDriveStatus status)
    {
        invoke_listeners<IRemovableDriveStatusListener>(
            [drive_path, status](auto* listener)
            { listener->on_removable_drive_status_changed(drive_path, status); }
        );
    }

    void dispatch_status_on_main_thread(const boost::filesystem::path& drive_path, RemovableDriveStatus status)
    {
        m_dispatcher.dispatch_on_main_thread(
            [this, drive_path, status]() mutable
            {
                this->invoke_listeners<IRemovableDriveStatusListener>(
                    [drive_path, status](auto* listener) mutable
                    { listener->on_removable_drive_status_changed(drive_path, status); }
                );
            }
        );
    }
};
} // namespace Slic3r::Biz::RemovableDrive
