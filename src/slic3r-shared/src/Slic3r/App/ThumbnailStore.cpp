#include "Slic3r/App/ThumbnailStore.hpp"

namespace Slic3r::App {

void ThumbnailStore::update(Domain::SelectionId project_id, const Plater::BedThumbnailTextures& thumbnails)
{
    projects.project(project_id).thumbnails = thumbnails;
}

void ThumbnailStore::update(Domain::SelectionId project_id, Domain::Image&& thumbnail_3mf)
{
    projects.project(project_id).thumbnail_3mf = std::make_unique<Domain::Image>(
        std::move(thumbnail_3mf)
    );
}

} // namespace Slic3r::App
