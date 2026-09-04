#pragma once

#include "Slic3r/Domain/SelectionId.hpp"

#include <boost/filesystem/path.hpp>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

struct ThumbnailStore;

class ProjectSaver
{
public:
    ProjectSaver(Biz::ProjectInteractor& project_interactor, ThumbnailStore& thumbnail_store);

    [[nodiscard]] bool save_project(Domain::SelectionId project_id);
    [[nodiscard]] bool save_selected_project();

    [[nodiscard]] bool save_project_as(Domain::SelectionId project_id);
    [[nodiscard]] bool save_selected_project_as();

    [[nodiscard]] bool save_unsaved_projects();

private:
    void save_project_internal(Domain::SelectionId project_id, const boost::filesystem::path& path);

private:
    Biz::ProjectInteractor& m_project_interactor;
    ThumbnailStore& m_thumbnail_store;
};

} // namespace Slic3r::App
