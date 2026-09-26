#pragma once

#include "Slic3r/Domain/Image.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "libslic3r/ThumbnailImageRequest.hpp"
#include <string>

namespace Slic3r::CLIThumbnails {

// Uses the offscreen CGL/EGL/WGL renderer from #15355. Returns no images when
// rendering is unavailable. Images use the top-down Domain::Image convention.
Domain::Images render_thumbnails(const Domain::Project& project,
    const Biz::Slicing::ThumbnailImageRequest& request,
    const std::string& resources_dir);

} // namespace Slic3r::CLIThumbnails
