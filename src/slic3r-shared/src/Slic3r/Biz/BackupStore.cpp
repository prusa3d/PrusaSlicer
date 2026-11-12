#include "Slic3r/Biz/BackupStore.hpp"

#include "Slic3r/Biz/Platform/TimerQueue.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Format/3mf.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Directories.hpp"

#include <random>

namespace Slic3r::Biz {

namespace {
/**
 * @warning these generated ids are not guarenteed to be unique between several Slicers
 */
[[nodiscard]] std::string generate_id(std::size_t length = 6)
{
    static constexpr std::string_view chars = "abcdefghijklmnopqrstuvwxyz0123456789";

    thread_local std::mt19937 generator{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> distribution{0, chars.size() - 1};

    std::string uid(length, '\0');
    for (char& c : uid) {
        c = chars[distribution(generator)];
    }

    static std::unordered_set<std::string> s_generated_ids;
    if (s_generated_ids.contains(uid)) {
        return generate_id(length);
    } else {
        s_generated_ids.insert(uid);
        return uid;
    }
}
} // namespace

constexpr const char* JOB_NAME = "BackupJob";

struct BackupProjectsJobData
{
    std::string slic3r_id;
    std::unordered_set<std::string> running_slic3r_uuids;
    boost::filesystem::path dest_directory;
    std::vector<std::pair<std::string, Domain::Project>> projects;
    std::vector<std::string> unsaved_project_prefixes;
};

BackupProjectsJobData
perform_job(Biz::JThread::StopToken stop_token, BackupProjectsJobData&& job_data)
{
    if (!boost::filesystem::is_directory(job_data.dest_directory)) {
        SPDLOG_WARN(
            "Cannot backup, directory {} does not exists",
            job_data.dest_directory.string()
        );
        return std::move(job_data);
    }

    std::list<std::string> filenames;

    for (const std::pair<std::string, Domain::Project>& project : std::as_const(job_data.projects))
    {
        if (stop_token.stop_requested()) {
            break;
        }

        std::string project_filename = job_data.slic3r_id
            + "_"
            + project.first
            + "_"
            + (project.second.file_name().empty() ? "unnamed_project" : project.second.file_name());

        if (!project_filename.ends_with(".3mf")) {
            project_filename += ".3mf";
        }

        filenames.emplace_back(project_filename);

        project_filename += ".part";

        store_3mf(
            boost::filesystem::path(job_data.dest_directory / project_filename).string(),
            project.second,
            {}
        );
    }

    // remove all saved files handled by this project
    for (boost::filesystem::directory_iterator file_it(job_data.dest_directory), end;
         file_it != end;
         ++file_it)
    {
        if (!boost::filesystem::is_regular_file(file_it->path())) {
            continue;
        }

        const std::string filename = file_it->path().filename().string();
        if (!filename.starts_with(job_data.slic3r_id) || filename.ends_with(".part")) {
            // ignore not our project and part files for now
            continue;
        }

        // Is project unsaved?
        auto it = std::ranges::find_if(
            job_data.unsaved_project_prefixes,
            [&](const std::string& unsaved_prefix) { return filename.starts_with(unsaved_prefix); }
        );
        if (it != job_data.unsaved_project_prefixes.end()) {
            continue;
        }

        // only our saved project files remain, remove them
        if (!boost::filesystem::remove(file_it->path())) {
            SPDLOG_WARN("Could not remove our own project from backups");
        }
    }

    // rename .part files to their normal names
    for (const std::string& filename : std::as_const(filenames)) {
        boost::filesystem::path project_part_path(job_data.dest_directory / (filename + ".part"));

        if (!boost::filesystem::exists(project_part_path)) {
            SPDLOG_WARN(
                "Cannot found partially saved backup project! {}",
                project_part_path.string()
            );
            continue;
        }

        boost::filesystem::rename(project_part_path, job_data.dest_directory / filename);
    }

    return std::move(job_data);
}

BackupStore::BackupStore(ProjectInteractor& project_interactor) :
    m_project_interactor(project_interactor),
    m_message_content_listener_scope(
        Platform::PlatformServices::instance().app_instance_message_handler(),
        *this
    ),
    m_slic3r_uuid(generate_id())
{
    m_project_interactor.add_listener<IProjectsChangedListener>(this);

    boost::filesystem::path backup_dir = backup_directory();
    if (!boost::filesystem::exists(backup_dir)) {
        boost::filesystem::create_directories(backup_dir);
    }
}

void BackupStore::start_crash_detection()
{
    // Request backup ids from running instances
    Platform::PlatformServices::instance().app_instance_message_handler().multicast_message(
        "BACKUP_ID_REQUEST",
        std::string{}
    );

    Platform::PlatformServices::instance().timer_queue().set_timer(
        std::chrono::seconds{5}, // Grace period for everyone to send us their Backup id
        [this] { scan_for_crashes(); }
    );
}

BackupStore::~BackupStore()
{
    m_project_interactor.remove_listener<IProjectsChangedListener>(this);

    clear_backups();
}

bool BackupStore::is_project_unsaved(Domain::SelectionId project_id) const
{
    ProjectEntries::const_iterator it = std::ranges::find_if(
        m_project_entries,
        [&](const ProjectEntry& entry) { return entry.project_id == project_id; }
    );

    return it == m_project_entries.cend() ? false : it->unsaved;
}

bool BackupStore::is_any_project_unsaved() const
{
    return std::ranges::any_of(m_project_entries, &ProjectEntry::unsaved);
}

void BackupStore::remove_backup(Domain::SelectionId project_id)
{
    ProjectEntry& entry = get_or_create_entry(project_id);
    entry.invalidated   = false;
    entry.unsaved       = false;

    write_backups();

    invoke_listeners<IBackupStoreListener>([project_id](auto* l)
                                           { l->on_project_invalidation_changed(project_id); });
}

void BackupStore::invalidate_backup(Domain::SelectionId project_id)
{
    ProjectEntry& entry = get_or_create_entry(project_id);
    entry.invalidated   = true;
    entry.unsaved       = true;

    write_backups();

    invoke_listeners<IBackupStoreListener>([project_id](auto* l)
                                           { l->on_project_invalidation_changed(project_id); });
}

void BackupStore::restore_backups(
    const std::vector<std::pair<boost::filesystem::path, bool>>& projects
)
{
    std::vector<boost::filesystem::path> recovery_projects;
    recovery_projects.reserve(projects.size());
    for (const auto& [path, recover] : projects) {
        if (recover) {
            recovery_projects.push_back(path);
        } else {
            // Only discard this project (remove from filesystem)
            if (!boost::filesystem::remove(path)) {
                SPDLOG_WARN("Could not remove found crashed project");
            }
        }
    }

    if (!recovery_projects.empty()) {
        // attempt to load all selected projects for recovery
        m_project_interactor.load_projects(recovery_projects, true);
    }

    invoke_listeners<IBackupStoreListener>([](auto* l) { l->on_project_restore_completed(); });
}

void BackupStore::clear_backups()
{
    if (m_job_running) {
        Platform::JobManager::JobManager& job_manager =
            Platform::PlatformServices::instance().job_manager();
        job_manager.cancel_job(JOB_NAME);
        // Probably add some sleep here
    }

    if (!boost::filesystem::exists(backup_directory())) {
        return;
    }

    for (boost::filesystem::directory_iterator file_it(backup_directory()), end; file_it != end;
         ++file_it)
    {
        if (!boost::filesystem::is_regular_file(file_it->path())) {
            continue;
        }

        const boost::filesystem::path path = file_it->path();
        if (!path.filename().string().starts_with(m_slic3r_uuid)) {
            continue;
        }

        if (!boost::filesystem::remove(path)) {
            SPDLOG_WARN("Project backup {} could not be removed", path.string());
        }
    }
}

void BackupStore::write_backups()
{
    if (m_job_running) {
        m_queued_backup = true;
    } else {
        m_job_running   = true;
        m_queued_backup = false;

        BackupProjectsJobData data;
        data.slic3r_id            = m_slic3r_uuid;
        data.running_slic3r_uuids = m_running_slic3r_uuids;
        data.dest_directory       = backup_directory();
        data.projects.reserve(m_project_entries.size());

        for (ProjectEntry& entry : m_project_entries) {
            if (entry.unsaved) {
                data.unsaved_project_prefixes.emplace_back(
                    m_slic3r_uuid + "_" + entry.project_uuid
                );
                if (entry.invalidated) {
                    data.projects.emplace_back(
                        entry.project_uuid,
                        m_project_interactor.project(entry.project_id)
                    );
                    entry.invalidated = false;
                }
            }
        }

        Platform::JobManager::JobManager& job_manager =
            Platform::PlatformServices::instance().job_manager();

        job_manager.create_job(JOB_NAME, perform_job, std::move(data))
            .on_result(
                [this](const BackupProjectsJobData& job_data)
                {
                    m_job_running = false;
                    if (m_queued_backup) {
                        write_backups();
                    }
                }
            )
            .start();

        m_job_running = true;
    }
}

void BackupStore::scan_for_crashes()
{
    std::vector<boost::filesystem::path> detected_crashes;
    for (boost::filesystem::directory_iterator file_it(backup_directory()), end; file_it != end;
         ++file_it)
    {
        if (!boost::filesystem::is_regular_file(file_it->path())) {
            continue;
        }

        const std::string filename = file_it->path().filename().string();
        if (filename.starts_with(m_slic3r_uuid) || !filename.ends_with(".3mf")) {
            continue;
        }
        bool found = false;
        for (const std::string& ids : m_running_slic3r_uuids) {
            if (filename.starts_with(ids)) {
                found = true;
                break;
            }
        }
        if (found) {
            continue;
        }
        detected_crashes.push_back(file_it->path());
    }

    if (!detected_crashes.empty()) {
        invoke_listeners<IBackupStoreListener>(
            [&](auto* listener) { listener->on_crashed_projects_detected(detected_crashes); }
        );
    }
}

void BackupStore::on_backup_id_requested()
{
    // somebody requested backup ids, it could be us or a new arrival
    // send them ours
    Biz::Platform::PlatformServices::instance().app_instance_message_handler().multicast_message(
        "BACKUP_ID_ANSWER",
        m_slic3r_uuid
    );
}

void BackupStore::on_backup_id_provided(const std::string& id)
{
    m_running_slic3r_uuids.insert(id);
}

void BackupStore::on_project_removed(Domain::SelectionId project_id)
{
    remove_backup(project_id);
}

void BackupStore::on_project_saved(Domain::SelectionId project_id)
{
    remove_backup(project_id);
}

void BackupStore::on_project_loaded(Domain::SelectionId project_id)
{
    remove_backup(project_id);
}

boost::filesystem::path BackupStore::backup_directory()
{
    return boost::filesystem::path(Slic3r::data_dir()) / "backup_projects";
}

BackupStore::ProjectEntry& BackupStore::get_or_create_entry(Domain::SelectionId project_id)
{
    ProjectEntries::iterator it = std::ranges::find_if(
        m_project_entries,
        [&](const ProjectEntry& entry) { return entry.project_id == project_id; }
    );

    if (it == m_project_entries.end()) {
        return m_project_entries.emplace_back(project_id, generate_id());
    } else {
        return *it;
    }
}

} // namespace Slic3r::Biz
