#pragma once

#include "Slic3r/Biz/BackupStore.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"

#include "Slic3r/App/Yoga/Dialog.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class ScrollArea;
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class CrashedProjectEntry;
class Navigator;

class CrashedProjectsDialog : public Yoga::Dialog, public Biz::IBackupStoreListener
{
public:
    CrashedProjectsDialog(Biz::ProjectInteractor& project_interactor, Navigator& navigator);

    void on_crashed_projects_detected(
        const std::vector<boost::filesystem::path>& crashed_projects
    ) override;

    void on_project_restore_completed() override;

private:
    void update_button_label();

private:
    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;

    Biz::ListenerScope<Biz::IBackupStoreListener, Biz::BackupStore, CrashedProjectsDialog>
        m_backup_store_listener_scope;

    std::vector<CrashedProjectEntry*> m_projects;

    Yoga::ScrollArea* m_scroll_area{nullptr};
    Yoga::LayoutButton* m_recover_selected_button{nullptr};
};

} // namespace Slic3r::App
