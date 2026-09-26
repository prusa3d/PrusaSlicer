#include "CLIThumbnailRenderer.hpp"

namespace Slic3r::CLIThumbnails {
Domain::Images render_thumbnails(const Domain::Project&,
    const Biz::Slicing::ThumbnailImageRequest&, const std::string&)
{
    return {};
}
} // namespace Slic3r::CLIThumbnails
