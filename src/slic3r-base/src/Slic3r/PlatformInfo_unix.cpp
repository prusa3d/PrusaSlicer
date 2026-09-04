#include "Slic3r/PlatformInfo.hpp"

#include <sys/utsname.h>

namespace Slic3r {

PlatformInfo::PlatformInfo()
    : m_platform(Slic3r::platform()), m_platform_flavor(Slic3r::platform_flavor())
{
    struct utsname name;
    if (uname(&name) == 0)
        m_os_version = Semver::parse(name.version).value_or_eval([](){ return Semver::invalid(); });
    else
        m_os_version = Semver::invalid();
}


}

