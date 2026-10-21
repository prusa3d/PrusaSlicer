#pragma once

#include <string>

namespace Slic3r::Domain {

/// Physical printer the config container is meant to be printed on.
struct PhysicalPrinterSelection
{
    std::string uuid;
    std::string name;
};

} // namespace Slic3r::Domain
