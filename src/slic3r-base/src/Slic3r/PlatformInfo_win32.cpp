#include "Slic3r/PlatformInfo.hpp"

#include <windows.h>

namespace Slic3r {

PlatformInfo::PlatformInfo()
    : m_platform(Slic3r::platform()), m_platform_flavor(Slic3r::platform_flavor())
{
    DWORD dwVersion = 0;
    DWORD dwMajorVersion = 0;
    DWORD dwMinorVersion = 0;
    DWORD dwBuild = 0;

    dwVersion = GetVersion();

    // Get the Windows version.

    dwMajorVersion = (DWORD)(LOBYTE(LOWORD(dwVersion)));
    dwMinorVersion = (DWORD)(HIBYTE(LOWORD(dwVersion)));

    // Get the build number.

    if (dwVersion < 0x80000000)
        dwBuild = (DWORD)(HIWORD(dwVersion));

    m_os_version = Semver(dwMajorVersion, dwMinorVersion, dwBuild);
}


}

