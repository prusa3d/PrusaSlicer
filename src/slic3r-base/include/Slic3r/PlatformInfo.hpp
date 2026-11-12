#pragma once

#include "Slic3r/Platform.hpp"
#include "Slic3r/Semver.hpp"

namespace Slic3r {

class PlatformInfo
{
public:
    PlatformInfo();

    [[nodiscard]] Semver os_version() const { return m_os_version; }
    [[nodiscard]] Slic3r::Platform platform() const { return m_platform; }
    [[nodiscard]] Slic3r::PlatformFlavor platform_flavor() const { return m_platform_flavor; }
    static PlatformInfo& instance();

private:
    Semver m_os_version;
    Slic3r::Platform m_platform;
    Slic3r::PlatformFlavor m_platform_flavor;

};

}
