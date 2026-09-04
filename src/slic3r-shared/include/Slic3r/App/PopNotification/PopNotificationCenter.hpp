#pragma once

#include "Slic3r/App/PopNotification/PopNotificationObservableList.hpp"
#include "Slic3r/Biz/IArrangeEventsListener.hpp"
#include "Slic3r/Biz/ObservableListSortFilter.hpp"
#include "Slic3r/Biz/Platform/JobManager/IJobManagerStatusChangedListener.hpp"
#include "Slic3r/Biz/RemovableDrive/IRemovableDriveStatusListener.hpp"
#include "Slic3r/Biz/RemovableDrive/RemovableDriveService.hpp"
#include "Slic3r/Biz/StatusCache.hpp"
#include "Slic3r/Biz/UserAccount/IUserAccountListener.hpp"
#include "Slic3r/Biz/IProjectsChangedListener.hpp"
#include "Slic3r/Biz/PrintHost/PrintHostJobInfoTag.hpp"
#include "Slic3r/Biz/Connect/IConnectHandlerListener.hpp"
#include "Slic3r/App/Platform/IFileExplorerErrorListener.hpp"
#include "Slic3r/App/LeftBarTabs.hpp"
#include "Slic3r/App/Lua/IPluginInstallationListener.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
}

namespace Slic3r::App::PopNotification {

struct JobNotificationSpec;

class PopNotificationCenter :
    public Biz::Platform::JobManager::IJobManagerStatusChangedListener,
    public Biz::IStatusCacheChangedListener,
    public Biz::RemovableDrive::IRemovableDriveStatusListener,
    public Biz::UserAccount::IUserAccountListener,
    public Biz::IProjectsChangedListener,
    public Biz::IArrangeEventsListener,
    public Biz::Connect::IConnectHandlerListener,
    public Platform::IFileExplorerErrorListener,
    public Lua::IPluginInstallationListener
{
public:
    PopNotificationCenter(Biz::ProjectInteractor& project_interactor);

    void upsert_notification(PopNotificationData data, PopNotificationObservableList::Matcher matcher);

    void close_notifications_of_type(PopNotificationType type);

    // Job
    void on_job_manager_status_changed(const Biz::Platform::JobManager::JobManagerStatus& status) override;

    // Slicing
    void on_status_cache_status_code_changed(const Domain::SlicingId slicing_id) override;
    void on_status_cache_progress_changed(const Domain::SlicingId slicing_id) override;
    void on_status_cache_errors_changed(const Domain::SlicingId slicing_id) override;
    void on_status_cache_warnings_changed(const Domain::SlicingId slicing_id) override;

    // Export / Upload
    void on_print_host_progress(size_t id, int progress);
    void on_print_host_error(size_t id, const std::string& msg);
    void on_print_host_cancel(size_t id);
    void on_print_host_done(size_t id);
    void on_print_host_info(size_t id, const Biz::PrintHost::PrintHostJobInfoTag& tag, const std::string& msg);

    // Removable Drive
    void on_removable_drive_status_changed(const boost::filesystem::path& drive_path, Biz::RemovableDrive::RemovableDriveStatus status) override;

    // User account
    void on_user_account_id_success(bool is_refresh, const std::string& username) override;
    void on_avatar_downloaded() override;
    void on_user_account_logged_out() override;
    void on_user_account_will_refresh() override;
    void on_user_account_action_retry(const Biz::Network::IHttp::Retry& retry, std::function<void(void)> cancel_callback) override;
    void on_user_account_action_retry_finished() override;

    // Projects Changed
    void on_project_load_failed(const std::string& error) override;
    void on_project_will_be_removed(Domain::SelectionId project_id) override;

    // Arrange
    void on_elements_not_arranged(
        Domain::SelectionId project_id,
        const Domain::ElementRefs& elements
    ) override;

    void on_fatal_arrange_error(
        Domain::SelectionId project_id
    ) override;

    PopNotificationObservableList& observable_list() {return m_notification_list;}
    Biz::ObservableListSortFilter<PopNotificationData>& source_list() { return m_list_sort_filter; }

    void set_switch_left_tab_fn(std::function<void(LeftBarTabs, const std::string&)> switch_left_tab_fn)
    {
        m_switch_left_tab_fn = switch_left_tab_fn;
    }

    void set_open_invalid_data_dialog_fn(std::function<void()> open_dialog_fn)
    {
        m_open_invalid_data_dialog_fn = open_dialog_fn;
    }

    // Connect Message Hander
    void on_connect_handler_error(const std::string& msg) override;

    // File explorer
    void on_file_explorer_error(
        const boost::filesystem::path& path,
        Platform::FileExplorerError reason
    ) override;

    // Plugin Error Listener
    void on_plugin_installation_error(const std::string& error_message) override;
    void on_plugin_installation_succeeded(const Lua::PluginBundleMeta& plugin_bundle_meta) override;
private:
    void on_job_progress(
        const JobNotificationSpec& spec,
        const std::string& job_key,
        const Biz::Platform::JobManager::Progress& progress
    );

    void on_arrange_job_progress(
        const std::string& job_key,
        const Biz::Platform::JobManager::Progress& progress
    );

    void on_job_print_host(const std::string& job_key, const Biz::Platform::JobManager::Progress& progress);
    void on_download_job_status_changed(const std::string& string, const Biz::Platform::JobManager::Progress& progress);

    PopNotificationLayout download_finished_layout(
        const std::string& body,
        const boost::filesystem::path& dest_path,
        const std::string& printables_url,
        bool is_loaded
    );

    PopNotificationObservableList m_notification_list;
    Biz::ObservableListSortFilter<PopNotificationData> m_list_sort_filter;
    Biz::RemovableDrive::RemovableDriveService& m_removable_drive_service;
    Biz::ProjectInteractor& m_project_interactor;
    std::function<void(LeftBarTabs, const std::string&)> m_switch_left_tab_fn;
    std::function<void()> m_open_invalid_data_dialog_fn;
};

} // namespace Slic3r::App::PopNotification
