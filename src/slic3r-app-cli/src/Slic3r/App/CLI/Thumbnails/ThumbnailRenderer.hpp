#pragma once

#include "Slic3r/Domain/Image.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "libslic3r/ThumbnailImageRequest.hpp"
#include <string>

namespace Slic3r::App::CLI::Thumbnails {

/**
 * @brief Render the requested project or bed using a native offscreen context.
 *
 * Returns no images when rendering is unavailable. Images use the top-down
 * Domain::Image convention. Based on the renderer introduced in #15355.
 */
Domain::Images render_thumbnails(
    const Domain::Project& project,
    const Biz::Slicing::ThumbnailImageRequest& request,
    const std::string& resources_dir
);

} // namespace Slic3r::App::CLI::Thumbnails
