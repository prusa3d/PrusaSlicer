#ifndef SLAARCHIVE_HPP
#define SLAARCHIVE_HPP

#include <string>
#include "libslic3r/SLAResult.hpp"

namespace Slic3r::sla {
class ISlaStore
{
public:
    virtual ~ISlaStore() = default;
    /// <summary>
    /// Store slicing result into sl1 file
    /// Raster Image for each layer + slicing Configuration 
    /// NOTE: Throw exception in case can't write to file_path
    /// e.g. no space, no privilege, bad network, ...
    /// </summary>
    /// <param name="file_path">File path for store result</param>
    /// <param name="data">Already rasterized and encoded files with configurations and thumbnails</param>
    virtual void store(const std::string& file_path, const ::Slic3r::Biz::Slicing::SLAResult& data) = 0;
};
} // namespace Slic3r::sla

#endif // SLAARCHIVE_HPP
