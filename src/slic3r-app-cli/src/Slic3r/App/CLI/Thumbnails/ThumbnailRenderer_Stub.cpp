#include "Slic3r/App/CLI/Thumbnails/ThumbnailRenderer.hpp"

namespace Slic3r::App::CLI::Thumbnails {
Domain::Images render_thumbnails(
    const Domain::Project&,
    const Biz::Slicing::ThumbnailImageRequest&,
    const std::string&
)
{
    return {};
}
} // namespace Slic3r::App::CLI::Thumbnails
