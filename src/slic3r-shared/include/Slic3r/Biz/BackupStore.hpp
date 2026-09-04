#pragma once

#include "Slic3r/Domain/SelectionId.hpp"

#include "Slic3r/Biz/Platform/IAppInstanceMessageHandler.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/IBackupStoreListener.hpp"
#include "Slic3r/Biz/IProjectsChangedListener.hpp"

#include <unordered_set>

namespace Slic3r::Biz {

class ProjectInteractor;

class BackupStore :
    public Platform::IAppInstanceMessageContentListener,
    public IProjectsChangedListener,
    public WithListeners<IBackupStoreListener>
{
    struct ProjectEntry
    {
        Domain::SelectionId project_id;
        std::string project_uuid;
        bool invalidated{true};
        bool unsaved{true};
    };

public:
    explicit BackupStore(ProjectInteractor& project_interactor);
    virtual ~BackupStore();

    void start_crash_detection();

    bool is_project_unsaved(Domain::SelectionId project_id) const;
    bool is_any_project_unsaved() const;

    void remove_backup(Domain::SelectionId project_id);
    void invalidate_backup(Domain::SelectionId project_id);

    void restore_backups(const std::vector<std::pair<boost::filesystem::path, bool>>& projects);

    void on_backup_id_requested() override;
    void on_backup_id_provided(const std::string& id) override;

    void on_project_removed(Domain::SelectionId project_id) override;
    void on_project_saved(Domain::SelectionId project_id) override;
    void on_project_loaded(Domain::SelectionId project_id) override;

private:
    void clear_backups();
    void write_backups();
    void scan_for_crashes();

    static boost::filesystem::path backup_directory();

    ProjectEntry& get_or_create_entry(Domain::SelectionId project_id);

private:
    ProjectInteractor& m_project_interactor;

    ListenerScope<
        Platform::IAppInstanceMessageContentListener,
        Platform::IAppInstanceMessageHandler,
        BackupStore>
        m_message_content_listener_scope;

    using ProjectEntries = std::vector<ProjectEntry>;
    ProjectEntries m_project_entries;
    bool m_queued_backup{false};
    bool m_job_running{false};

    std::unordered_set<std::string> m_running_slic3r_uuids;
    const std::string m_slic3r_uuid;
};

} // namespace Slic3r::Biz
