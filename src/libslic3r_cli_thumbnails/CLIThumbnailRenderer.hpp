///|/ Copyright (c) Prusa Research 2026 — CLI thumbnail renderer
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_cli_CLIThumbnailRenderer_hpp_
#define slic3r_cli_CLIThumbnailRenderer_hpp_

#include <string>

#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"

namespace Slic3r {
namespace CLIThumbnails {

// Render thumbnails of `model` at each size requested by `params` using an
// offscreen GL context. Returns an empty list if rendering is not possible
// (e.g. no offscreen GL backend, no GPU, init failed). Never throws.
//
// The `resources_dir` argument must point at PrusaSlicer's `resources/`
// directory so the renderer can load the `gouraud_light` shader source.
ThumbnailsList render_thumbnails(const Model              &model,
                                 const DynamicPrintConfig &print_config,
                                 const ThumbnailsParams   &params,
                                 const std::string        &resources_dir);

} // namespace CLIThumbnails
} // namespace Slic3r

#endif
