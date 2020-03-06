#ifndef slic3r_GUI_RemovableDriveManager_hpp_
#define slic3r_GUI_RemovableDriveManager_hpp_

#include <vector>
#include <string>

#include <boost/thread.hpp>
#include <tbb/mutex.h>
#include <condition_variable>

// Custom wxWidget events
#include "Event.hpp"

namespace Slic3r {
namespace GUI {

struct DriveData
{
	std::string name;
	std::string path;

	void clear() {
		name.clear();
		path.clear();
	}
	bool empty() const {
		return path.empty();
	}
};

inline bool operator< (const DriveData &lhs, const DriveData &rhs) { return lhs.path < rhs.path; }
inline bool operator> (const DriveData &lhs, const DriveData &rhs) { return lhs.path > rhs.path; }
inline bool operator==(const DriveData &lhs, const DriveData &rhs) { return lhs.path == rhs.path; }

using RemovableDriveEjectEvent = Event<DriveData>;
wxDECLARE_EVENT(EVT_REMOVABLE_DRIVE_EJECTED, RemovableDriveEjectEvent);

using RemovableDrivesChangedEvent = SimpleEvent;
wxDECLARE_EVENT(EVT_REMOVABLE_DRIVES_CHANGED, RemovableDrivesChangedEvent);

#if __APPLE__
	// Callbacks on device plug / unplug work reliably on OSX.
	#define REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
#endif // __APPLE__

class RemovableDriveManager
{
public:
	RemovableDriveManager() = default;
	RemovableDriveManager(RemovableDriveManager const&) = delete;
	void operator=(RemovableDriveManager const&) = delete;
	~RemovableDriveManager() { assert(! m_initialized); }

	// Start the background thread and register this window as a target for update events.
	// Register for OSX notifications.
	void 		init(wxEvtHandler *callback_evt_handler);
	// Stop the background thread of the removable drive manager, so that no new updates will be sent out.
	// Deregister OSX notifications.
	void 		shutdown();

	// Returns path to a removable media if it exists, prefering the input path.
	std::string get_removable_drive_path(const std::string &path);
	bool        is_path_on_removable_drive(const std::string &path) { return this->get_removable_drive_path(path) == path; }

	// Verify whether the path provided is on removable media. If so, save the path for further eject and return true, otherwise return false.
	bool 		set_and_verify_last_save_path(const std::string &path);
	// Eject drive of a file set by set_and_verify_last_save_path().
	void 		eject_drive();

	struct RemovableDrivesStatus {
		bool 	has_removable_drives { false };
		bool 	has_eject { false };
	};
	RemovableDrivesStatus status();

	// Enumerates current drives and sends out wxWidget events on change or eject.
	// Called by each public method, by the background thread and from RemovableDriveManagerMM::on_device_unmount OSX notification handler.
	// Not to be called manually.
	// Public to be accessible from RemovableDriveManagerMM::on_device_unmount OSX notification handler.
	// It would be better to make this method private and friend to RemovableDriveManagerMM, but RemovableDriveManagerMM is an ObjectiveC class.
	void 		update();

private:
	bool 			 		m_initialized { false };
	wxEvtHandler*			m_callback_evt_handler { nullptr };

#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	// Worker thread, worker thread synchronization and callbacks to the UI thread.
	void 					thread_proc();
	boost::thread 			m_thread;
	std::condition_variable m_thread_stop_condition;
	mutable std::mutex 		m_thread_stop_mutex;
	bool 					m_stop { false };
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS

	// Called from update() to enumerate removable drives.
	std::vector<DriveData> 	search_for_removable_drives() const;

	// m_current_drives is guarded by m_drives_mutex
	// sorted ascending by path
	std::vector<DriveData> 	m_current_drives;
	// When user requested an eject, the drive to be forcefuly ejected is stored here, so the next update will
	// recognize that the eject was finished with success and an eject event is sent out.
	// guarded with m_drives_mutex
	DriveData 				m_drive_data_last_eject;
	mutable tbb::mutex 		m_drives_mutex;

	// Returns drive path (same as path in DriveData) if exists otherwise empty string.
	std::string 			get_removable_drive_from_path(const std::string& path);
	// Returns iterator to a drive in m_current_drives with path equal to m_last_save_path or end().
	std::vector<DriveData>::const_iterator find_last_save_path_drive_data() const;
	// Set with set_and_verify_last_save_path() to a removable drive path to be ejected.
	std::string 			m_last_save_path;

#if _WIN32
	//registers for notifications by creating invisible window
	//void register_window_msw();
#elif __APPLE__
    void register_window_osx();
    void unregister_window_osx();
    void list_devices(std::vector<DriveData> &out) const;
    // not used as of now
    void eject_device(const std::string &path);
    // Opaque pointer to RemovableDriveManagerMM
    void *m_impl_osx;
#endif
};

}}

#endif // slic3r_GUI_RemovableDriveManager_hpp_
