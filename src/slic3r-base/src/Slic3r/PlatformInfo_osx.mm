#include "Slic3r/PlatformInfo.hpp"

#import <Foundation/Foundation.h>

namespace Slic3r {

PlatformInfo::PlatformInfo()
    : m_platform(Slic3r::platform()), m_platform_flavor(Slic3r::platform_flavor())
{
    NSOperatingSystemVersion os = [NSProcessInfo processInfo].operatingSystemVersion;
    m_os_version = Semver(os.majorVersion, os.minorVersion, os.patchVersion);
}


}
