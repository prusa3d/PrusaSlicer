///|/ Copyright (c) 2024 Felix Reißmann @Felix-Rm
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "GOO.hpp"

namespace Slic3r {

std::unique_ptr<sla::RasterBase> GOOWriter::create_raster() const {
   return {};
}

sla::RasterEncoder GOOWriter::get_encoder() const { return {}; }

void GOOWriter::export_print(
    const std::string filename,
    const SLAPrint &print,
    const ThumbnailsList &thumbnails,
    const std::string &project_name
) {
}

ConfigSubstitutions GOOReader::read(
    std::vector<ExPolygons> &slices, DynamicPrintConfig &profile_out
) {
    return {};
}

} // namespace Slic3r