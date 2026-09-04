#pragma once

#include "Slic3r/Biz/Platform/ISingleInstanceChecker.hpp"

#include <string>

namespace Slic3r::Biz::AppInstance {

/**
 * @brief Linux and Mac implementation of Instance checking via lock file. 
 * On Mac, message about lock being released is multicasted so other instance can aquire it.
 * Thats why this class is part of slic3r-shared instead of platform.
 */
class SingleInstanceCheckerLock : public Platform::ISingleInstanceChecker
{
public:

	/**
	 * Constructor only stores name and path value, call is_another_running to aquire the lock.
	 */
	SingleInstanceCheckerLock(const std::string& name, const std::string& path);

	/**
	 * Destructor releases the lockfile. On mac multicasts message to other instances.
	 */
	~SingleInstanceCheckerLock() override;

	/**
	 * @brief Tries to aquire the lockfile and returns the result.
	 * This is the only way how to aquire the lockfile.
	 */
	bool is_another_running() override;

	/**
    * @brief Returns if this is instance that holds the lockfile.
    */
   bool is_primary_instance() override { return m_lock_created; }

private:
	std::string m_name;
	std::string m_path;
	bool 		m_lock_created {false};
};
}