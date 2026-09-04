#include "Slic3r/App/ProjectSaver.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/Wildcards.hpp"
#include "Slic3r/App/ThumbnailStore.hpp"

#include <boost/filesystem.hpp>

namespace Slic3r::App {

ProjectSaver::ProjectSaver(
    Biz::ProjectInteractor& project_interactor,
    ThumbnailStore& thumbnail_store
) :
    m_project_interactor(project_interactor),
    m_thumbnail_store(thumbnail_store)
{}

bool ProjectSaver::save_project(Domain::SelectionId project_id)
{
    const boost::filesystem::path path =
        m_project_interactor.project(project_id).loaded_file_path();
    if (!boost::filesystem::exists(path)) {
        return save_project_as(project_id);
    } else {
        save_project_internal(project_id, path);
        return true;
    }
}

bool ProjectSaver::save_selected_project()
{
    return save_project(m_project_interactor.selected_project_id());
}

bool ProjectSaver::save_selected_project_as()
{
    return save_project_as(m_project_interactor.selected_project_id());
}

bool ProjectSaver::save_unsaved_projects()
{
    for (const auto& [id, project] : m_project_interactor.workbench().projects()) {
        if (m_project_interactor.backup_store().is_project_unsaved(id)) {
            const boost::filesystem::path path =
                m_project_interactor.project(id).loaded_file_path();
            if (!boost::filesystem::exists(path)) {
                const bool success = save_project_as(id);
                if (!success) {
                    return false;
                }
            } else {
                save_project_internal(id, path);
            }
        }
    }

    return true;
}

void ProjectSaver::save_project_internal(
    Domain::SelectionId project_id,
    const boost::filesystem::path& path
)
{
    Store3mfParam params{
        .thumbnail = m_thumbnail_store.projects.project(project_id).thumbnail_3mf.get()
    };
    m_project_interactor.save_project(project_id, path, params);
}

bool ProjectSaver::save_project_as(Domain::SelectionId project_id)
{
    const std::string project_name = m_project_interactor.get_project_save_name(project_id);

    bool project_saved = false;

    // Saving a new project - show file save dialog.
    IDialogManager::FileCallback callback =
        [&](bool success, const std::vector<boost::filesystem::path>& file_paths)
    {
        if (success) {
            save_project_internal(project_id, file_paths.front());
            project_saved = true;
        }
    };
    auto& dlg_manager = AppServices::instance().dialog_manager();
    dlg_manager.show_file_dialog(
        FileDialogType::Save,
        Biz::_u8L("Save Project") + " " + project_name,
        m_project_interactor.project_dir(
            project_id,
            AppServices::instance().app_config().get<std::string>("last_used_directory")
        ),
        project_name,
        Wildcards::generate_wildcards(Wildcards::TypeFlag::Project3mf),
        callback
    );

    return project_saved;
}
} // namespace Slic3r::App
