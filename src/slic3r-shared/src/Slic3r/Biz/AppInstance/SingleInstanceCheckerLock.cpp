#include "Slic3r/Biz/AppInstance/SingleInstanceCheckerLock.hpp"

#include "Slic3r/Biz/AppInstance/AppInstanceMessageHandlerFactory.hpp"
#include "Slic3r/Biz/AppInstance/AbstractAppInstanceMessageHandler.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Log.hpp"

#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

namespace Slic3r::Biz::AppInstance {

namespace {

#ifdef __APPLE__
	// This function will come handy once there will be drag&drop implemented on mac.
	// Than once Slicer turns into Gcode viewer -> we need to call unlock_lockfile
	// to prevent false detection of running app.

	bool unlock_lockfile(const std::string& name, const std::string& path)
	{
		std::string dest_dir = path + name;
		struct      flock fl;
		int         fdlock;
		fl.l_type = F_UNLCK;
		fl.l_whence = SEEK_SET;
		fl.l_start = 0;
		fl.l_len = 1;
		if ((fdlock = open(dest_dir.c_str(), O_WRONLY | O_CREAT, 0666)) == -1)
			return false;

		if (fcntl(fdlock, F_SETLK, &fl) == -1)
			return false;

		return true;
	}
#endif //__APPLE__


}

SingleInstanceCheckerLock::SingleInstanceCheckerLock(const std::string& name, const std::string& path)
	: m_name(name)
	, m_path(path)
{

}

SingleInstanceCheckerLock::~SingleInstanceCheckerLock()
{
	SPDLOG_INFO(__FUNCTION__);
	if (m_lock_created) {
		std::string dest_file = m_path + m_name;
		SPDLOG_INFO("Removing lockfile {}", dest_file);
		if( remove( dest_file.c_str() ) != 0 ) {
			SPDLOG_ERROR("Failed to delete lockfile {}", dest_file);
		}
//#ifdef __APPLE__
	// Partial fix of #7583
	// On price of incorrect working of single instances on older OSX
	//if (wxPlatformInfo::Get().GetOSMajorVersion() > 12) {
		std::unique_ptr<Biz::AppInstance::AbstractAppInstanceMessageSender> sender = Biz::AppInstance::create_app_instance_message_sender();
		sender->multicast_message("OTHER_CLOSING", std::string(), Biz::Platform::PlatformServices::instance().app_hash(), nullptr);
	//}
//#endif
	}
}

bool SingleInstanceCheckerLock::is_another_running()
{
	SPDLOG_INFO(__FUNCTION__);
	std::string dest_file = m_path + m_name;
	struct      flock fl;
	int         fdlock;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 1;
	boost::system::error_code ec;

    if (! boost::filesystem::is_directory(m_path, ec) || ec) {
        ec.clear();
        if (! boost::filesystem::create_directories(m_path, ec) || ec) {
            SPDLOG_ERROR("Failed to get lock: failed to create a directory {}", m_path);
            return false;
        }
    }
	if ((fdlock = open(dest_file.c_str(), O_WRONLY | O_CREAT, 0666)) == -1) {
		SPDLOG_ERROR("Failed to create a lockfile {}", dest_file);
		return true;
	}
	if (fcntl(fdlock, F_SETLK, &fl) == -1) {
		SPDLOG_ERROR("Failed to create a lockfile {}", dest_file);
		return true;
	}
	SPDLOG_INFO("Lockfile created at {}", dest_file);
	m_lock_created = true;
	return false;	
}
}