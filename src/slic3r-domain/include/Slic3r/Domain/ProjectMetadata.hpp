#pragma once

#include <string>

namespace Slic3r::Domain {

struct ProjectMetadata
{
    std::string id;
    size_t version{0};

    void increment_version()
    {
        version++;
    }
};

} // namespace Slic3r::Domain
