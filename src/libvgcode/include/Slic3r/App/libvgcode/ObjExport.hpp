#pragma once

#include "Slic3r/App/libvgcode/Types.hpp"

#include <stdio.h>

namespace Slic3r::App::libvgcode {

class FdmViewer;

bool export_toolpaths_to_obj(FILE& obj_file, FILE& mtl_file, const ObjExportParams& params, const FdmViewer& viewer);

} // namespace Slic3r::App::libvgcode
