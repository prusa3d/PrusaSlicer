#include <string>
#include <memory>

#include "libslic3r/SLAResult.hpp"

namespace Slic3r::Biz::PrintHost::Sla {

/// <summary>
/// File writer for the '.sl1' file format, store slicing result into file
/// NOTE: Throw exception in case can't write to file_path
/// e.g. no space, no privilege, bad network, ...
/// </summary>
/// <param name="file_path">File path for store result</param>
/// <param name="data">Already rasterized and encoded files with configurations and thumbnails</param>
void store_sl1(const std::string& file_path, const Biz::Slicing::SLAResultData& data);

} // namespace Slic3r::Biz::PrintHost::Sla
