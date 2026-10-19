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
/// <param name="job_name">Name of the print job (jobDir and layer file prefix), usually the stem
/// of the final file name. The stem of file_path is used when empty.</param>
/// <param name="data">Already rasterized and encoded files with configurations and thumbnails</param>
void store_sl1(
    const std::string& file_path,
    const std::string& job_name,
    const Biz::Slicing::SLAResultData& data
);

} // namespace Slic3r::Biz::PrintHost::Sla
