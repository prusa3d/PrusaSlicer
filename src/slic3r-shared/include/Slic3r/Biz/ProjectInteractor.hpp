#pragma once

#include "Slic3r/Biz/ArrangeInteractor.hpp"
#include "Slic3r/Biz/ClipboardInteractor.hpp"
#include "Slic3r/Biz/FDMResultCache.hpp"
#include "Slic3r/Biz/GeneratedSupportPointsCache.hpp"
#include "Slic3r/Biz/IMdb.hpp"
#include "Slic3r/Biz/ProjectSettingsInteractor.hpp"
#include "Slic3r/Biz/VirtualExtruderInteractor.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"
#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "Slic3r/Biz/StatusCache.hpp"
#include "Slic3r/Biz/ISlicingInputChangedListener.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/InvokeLaterBag.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/ISelectedBedInstanceChangedListener.hpp"
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"
#include "Slic3r/Biz/ResultExport/ResultExportInteractor.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"
#include "Slic3r/Biz/UserAccount/IUserAccountListener.hpp"
#include "Slic3r/Biz/Platform/IAppInstanceMessageContentListener.hpp"
#include "Slic3r/Biz/ObservableProjectList.hpp"
#include "Slic3r/Biz/Format/3mf.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterInteractor.hpp"
#include "Slic3r/Biz/RemovableDrive/RemovableDriveService.hpp"
#include "Slic3r/Biz/FileDownloader/FileDownloaderInteractor.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterInteractor.hpp"
#include "Slic3r/Biz/IUndoProvider.hpp"
#include "Slic3r/Biz/Connect/ConnectMessageHandler.hpp"
#include "Slic3r/Biz/BackupStore.hpp"

namespace Slic3r::Domain {
class Model;
class Project;
class Workbench;
} // namespace Slic3r::Domain

namespace Slic3r::Biz {

class ISelectedProjectChangedListener;
class ISelectedConfigContainerChangedListener;
class IProjectsChangedListener;
class IMessageDialogProvider;

class NoopUndoProvider : public IUndoProvider
{
    void take_snapshot(UndoSnapshotType type) {}

    void select_snapshot(UndoSnapshotSelection::Variant snapshot_variant) {}

    bool is_undo_possible() const
    {
        return false;
    }

    bool is_redo_possible() const
    {
        return false;
    }
};

/**
 * @brief Top level interactor managing list of projects and their bed selection.
 *
 * Use this interactor to:
 * - manipulate project add/close/select,
 * - get access to PresetInteractor (to modify config container of bed),
 * - get access to SceneInteractor (to manipulate 3D volumes)
 * .
 */

class ProjectInteractor final :
    public ISelectedBedInstancesChangedListener,
    public ISlicingInputChangedListener,
    public IColorsChangedListener,
    public IVirtualExtrudersChangedListener,
    public UserAccount::IUserAccountListener,
    public Platform::IAppInstanceMessageContentListener,
    public FileDownloader::IFileDownloaderListener,
    public WithListeners<ISelectedProjectChangedListener, IProjectsChangedListener, ISelectedConfigContainerChangedListener>
{
public:
    ProjectInteractor(Domain::Workbench& workbench, Platform::IMainThreadDispatcher& dispatcher, Biz::Slicing::IThumbnailImageGenerator& thumbnail_image_generator) :
        m_workbench(workbench),
        m_scene_interactor(workbench),
        m_backup_store(*this),
        m_preset_interactor(workbench, m_scene_interactor, m_backup_store),
        m_arrange_interactor(m_scene_interactor, workbench),
        m_clipboard_interactor(m_scene_interactor, m_arrange_interactor, workbench),
        m_project_settings_interactor(workbench, m_null_mdb),
        m_virtual_extruder_interactor(workbench),
        m_slicing_interactor(dispatcher, thumbnail_image_generator),
        m_result_export_interactor(dispatcher),
        m_user_account_interactor(dispatcher),
        m_preset_updater_interactor(dispatcher),
        m_removable_drive_service(dispatcher),
        m_file_downloader_interactor(dispatcher),
        m_physical_printer_interactor(dispatcher, m_preset_interactor, m_user_account_interactor),
        m_connect_message_handler(dispatcher, m_preset_interactor, m_user_account_interactor, m_physical_printer_interactor),
        m_project_list(*this),
        m_undo_provider(std::make_unique<NoopUndoProvider>())
    {
        m_scene_interactor.set_preset_visual_getter(&m_preset_interactor);
        add_listener<ISelectedConfigContainerChangedListener>(&m_preset_interactor);
        add_listener<ISelectedConfigContainerChangedListener>(&m_scene_interactor);
        add_listener<ISelectedConfigContainerChangedListener>(&m_project_settings_interactor);
        add_listener<ISelectedProjectChangedListener>(&m_scene_interactor);
        add_listener<ISelectedProjectChangedListener>(&m_preset_interactor);
        m_scene_interactor.add_listener<ISelectedBedInstancesChangedListener>(this);
        add_listener<ISelectedProjectChangedListener>(&m_slicing_interactor);
        m_scene_interactor.add_listener<ISlicingInputChangedListener>(this);
        m_preset_interactor.add_listener<ISlicingInputChangedListener>(this);
        m_preset_interactor.add_listener<Preset::IPresetChangedListener>(&m_scene_interactor);
        m_preset_interactor.add_listener<Preset::IPresetChangedListener>(&m_project_settings_interactor);
        m_preset_interactor.add_listener<Preset::IPresetChangedListener>(&m_virtual_extruder_interactor);
        m_project_settings_interactor.add_listener<IColorsChangedListener>(this);
        m_virtual_extruder_interactor.add_listener<IVirtualExtrudersChangedListener>(this);
        m_slicing_interactor.set_listener<Slicing::IFDMResultListener>(&m_fdm_result_cache);
        m_slicing_interactor.set_listener<Slicing::ISLAResultListener>(&m_sla_result_cache);
        m_slicing_interactor.set_listener<Slicing::ISLAObjectListener>(&m_sla_object_cache);
        m_slicing_interactor.set_listener<Slicing::IGeneratedSupportPointsListener>(
            &m_generated_support_points_cache
        );
        m_slicing_interactor.add_listener<Slicing::IStatusListener>(&m_status_cache);
        m_slicing_interactor.add_listener<Slicing::IWipeTowerGeometryListener>(&m_scene_interactor);
        m_slicing_interactor.add_listener<Slicing::IExtruderCandidatesListener>(&m_scene_interactor);
        m_user_account_interactor.add_listener<UserAccount::IUserAccountListener>(this);
        Platform::PlatformServices::instance()
            .app_instance_message_handler()
            .add_listener<Platform::IAppInstanceMessageContentListener>(this);
        m_file_downloader_interactor.add_listener<FileDownloader::IFileDownloaderListener>(this);
        add_listener<ISelectedConfigContainerChangedListener>(
            &m_preset_interactor.object_settings_interactor()
        );
        add_listener<ISelectedConfigContainerChangedListener>(&m_physical_printer_interactor);
    }

    const Domain::Workbench& workbench() const
    {
        return m_workbench;
    }

    void initialize_bed(Domain::SelectionId project_id, Domain::SelectionId config_container_id, Domain::BedContainer& bed_container);

    /**
     * @name Project manipulation
     * @{
     */
    /**
     * @brief Create new project
     * @return Returns ID of newly created project
     */
    Domain::SelectionId new_project();
    Domain::SelectionId new_project_with_modification(
        const std::function<void(Domain::Project&)>& modifier
    );

    /**
     * @brief Create new project from a preset built from metadata and configuration
     *
     * @return ID of the created project, or the error message on failure
     */
    tl::expected<Domain::SelectionId, std::string> new_project_with_preset(
        const Domain::Preset::SelectedPresetMetadata& preset_metadata,
        const Domain::ConfigPack& config_pack
    );

    /**
     * @name Project manipulation
     * @{
     */
    /**
     * @brief Load project from the file
     */
    void load_project(const boost::filesystem::path& file_path);
    /**
     * @brief Load projects from files
     * @param restored project - if set to true, will remove files after sucessfull loading,
     * and adjust the project name
     */
    void load_projects(
        const std::vector<boost::filesystem::path>& file_paths,
        bool restored_projects = false
    );

    /**
     * @name Project manipulation
     * @{
     */
    /**
     * @brief Save project into the file
     */
    void save_selected_project(const boost::filesystem::path& file_path, const Store3mfParam& params);
    /**
     * @brief Save project into the file
     */
    void save_project(
        Domain::SelectionId project_id,
        const boost::filesystem::path& file_path,
        const Store3mfParam& params
    );

    /**
     * Select already opened project. If the project is already selected, do nothing.
     * @param project_id An index of project to be selected.
     */
    void select_project(Domain::SelectionId project_id);

    /**
     * Add project to Workbench and select it.
     * @param p Project to move to workbench
     * @return A project_id / index of added project.
     */
    Domain::SelectionId add_project(Domain::Project&& p);

    /**
     * Remove given project from workbench and update selection if needed.
     * @param project_id
     */
    void remove_project(Domain::SelectionId project_id);

    /** @} */

    /**
     * @name Config container operations
     * @{
     */

    Domain::SelectionId add_config_container();

    Domain::SelectionId insert_config_container(
        Domain::SelectionId project_id,
        std::unique_ptr<Domain::ConfigContainer> config_container,
        std::size_t position
    );

    Domain::SelectionId duplicate_config_container(Domain::SelectionId config_container_id);
    void remove_config_container(Domain::SelectionId config_container_id);
    void select_config_container(Domain::SelectionId config_container_id);
    void reload_config_containers_after_undo(
        Domain::SelectionId project_id,
        Domain::Project::ConfigContainerList new_containers,
        InvokeLaterBag& listener_notifications
    );

    /** @} */

    /**
     * @name Selection ID getters
     * @{
     */

    /**
     * @brief Get selected project ID
     */
    Domain::SelectionId selected_project_id() const
    {
        return m_selection.project_id;
    }

    /**
     * @brief Get selected config container ID
     */
    Domain::SelectionId selected_config_container_id() const
    {
        return m_selection.config_container_id();
    }

    /** @} */
    /**
     * @name Quick selection getters
     * @{
     */
    const Domain::Project& selected_project() const;
    Domain::Project& selected_project();

    bool project_exists(size_t project_id) const;
    const Domain::Project& project(size_t project_id) const;
    Domain::Project& project(size_t project_id);

    const Domain::ConfigContainer& selected_config_container() const
    {
        DEBUG_ASSERT(m_selection.config_container_id() != Domain::INVALID_ID);
        return *DEBUG_ASSERT_VAL(selected_project().find_config_container(m_selection.config_container_id()));
    }

    Domain::ConfigContainer& selected_config_container()
    {
        DEBUG_ASSERT(m_selection.config_container_id() != Domain::INVALID_ID);
        return *DEBUG_ASSERT_VAL(selected_project().find_config_container(m_selection.config_container_id()));
    }

    /** @} */

    /**
     * @brief Get immutable preset interactor
     * @return Immutable preset interactor instance
     */
    const Preset::PresetInteractor& preset_interactor() const
    {
        return m_preset_interactor;
    }

    /**
     * @brief Get mutable preset interactor
     * @return Mutable preset interactor instance
     */
    Preset::PresetInteractor& preset_interactor()
    {
        return m_preset_interactor;
    }

    /**
     * @brief Get immutable scene interactor
     * @return Immutable scene interactor instance
     */
    const Scene::SceneInteractor& scene_interactor() const
    {
        return m_scene_interactor;
    }

    /**
     * @brief Get mutable scene interactor
     * @return Mutable scene interactor instance
     */
    Scene::SceneInteractor& scene_interactor()
    {
        return m_scene_interactor;
    }

    Slicing::SlicingInteractor& slicing_interactor()
    {
        return m_slicing_interactor;
    }

    FDMResultCache& fdm_result_cache()
    {
        return m_fdm_result_cache;
    }

    const FDMResultCache& fdm_result_cache() const
    {
        return m_fdm_result_cache;
    }

    SLAResultCache& sla_result_cache()
    {
        return m_sla_result_cache;
    }

    const SLAResultCache& sla_result_cache() const
    {
        return m_sla_result_cache;
    }

    SLAObjectCache& sla_object_cache()
    {
        return m_sla_object_cache;
    }

    GeneratedSupportPointsCache& generated_support_points_cache()
    {
        return m_generated_support_points_cache;
    }

    StatusCache& status_cache()
    {
        return m_status_cache;
    }

    Biz::ArrangeInteractor& arrange_interactor()
    {
        return m_arrange_interactor;
    }

    Biz::ClipboardInteractor& clipboard_interactor()
    {
        return m_clipboard_interactor;
    }

    ProjectSettingsInteractor& project_settings_interactor()
    {
        return m_project_settings_interactor;
    }

    const ProjectSettingsInteractor& project_settings_interactor() const
    {
        return m_project_settings_interactor;
    }

    BackupStore& backup_store()
    {
        return m_backup_store;
    }

    VirtualExtruderInteractor& virtual_extruder_interactor()
    {
        return m_virtual_extruder_interactor;
    }

    /**
     * @brief Replace the virtual extruders of one printer group and clean up after the removed ones.
     *
     * @param removed_virtual_extruder_ids IDs the user deleted.
     * @return Nothing on success, the validation error otherwise.
     */
    tl::expected<void, std::string> apply_virtual_extruders(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const Domain::VirtualExtruders& new_virtual_extruders,
        const std::vector<unsigned int>& removed_virtual_extruder_ids
    );

    IUndoProvider& undo_provider() {
        return *ASSERT_VAL(m_undo_provider);
    }

    void set_undo_provider(std::unique_ptr<IUndoProvider> undo_provider)
    {
        m_undo_provider = std::move(undo_provider);
        m_scene_interactor.set_undo_provider(m_undo_provider.get());
    }

    Domain::SlicingId selected_bed_slicing_id() const;

    /**
     * @name ISelectedBedInstancesChangedListener interface implementation
     * @{
     */
    void on_selected_bed_instances_changed(Domain::SelectionId project_id, const Scene::BedSelection& selection) override;
    /** @} */

    /**
     * @brief Creates PhysicalPrinter::PhysicalPrinterConfig and PrintHostData and passes it to ResultExportInteractor to start export.
     * PrintHostData copies gcode data from m_fdm_result_cache.
     * PhysicalPrinter::PhysicalPrinterConfig origin is yet to be decided.
     */
    void do_result_export(const Domain::SlicingId id, const boost::filesystem::path& dest_path);

    /**
     * @brief Creates PrintHostData and passes it with the given destination to ResultExportInteractor to start upload.
     * PrintHostData copies gcode data from m_fdm_result_cache.
     * @param print_host_config Destination captured when the upload was requested, so that a later
     * change of the selected physical printer cannot redirect the upload.
     */
    void do_result_upload(
        const Domain::SlicingId id,
        const std::string& filename,
        PrintHost::PrintHostAfterUploadAction post_action,
        const PhysicalPrinter::PhysicalPrinterConfig& print_host_config
    );

    /**
     * @brief Same as do_result_upload, but does parse connect_msg first.
     * Uploads to PrintHostType::PrusaConnect.
     * @param filename_override is used f.e. when bgcode is not allowed in profiles, but user still writes .bgcode to filename and then agrees to change it back to .gcode
     */
    void do_result_upload_connect(
        const Domain::SlicingId id,
        const std::string& connect_msg,
        const std::string& filename_override = std::string()
    );

    /**
     * @brief Called on every successful login to user account and token renewal.
     * Notifies all other running apps to read token store.
     */
    void on_user_account_id_success(bool is_refresh, const std::string& username) override
    {
        Platform::PlatformServices::instance().app_instance_message_handler().multicast_message(
            "STORE_READ",
            {}
        );

        if (!is_refresh)
        {
            m_physical_printer_interactor.select_connect_upload(true);
        }
    }

    /**
     * @brief Called on performed log out that originated locally.
     * Notifies all other running apps to read token store.
     * Dispatched separately from on_user_account_logged_out (only when notify_owner is true) so that a
     * logout caused by an accepted STORE_READ from another instance does not echo another STORE_READ
     * back, which would make the instances notify each other in an infinite loop.
     */
    void on_user_account_logged_out_notify_instances() override
    {
        Platform::PlatformServices::instance().app_instance_message_handler().multicast_message(
            "STORE_READ",
            {}
        );
    }

    void load_models_to_project(std::vector<boost::filesystem::path> paths);

    /**
     * @brief Callback from AppInstanceMessageHandler.
     */
    void on_open_models(std::vector<boost::filesystem::path> paths) override
    {
        load_models_to_project(paths);
    }

    /**
     * @brief Callback from AppInstanceMessageHandler.
     */
    void on_download_models(std::vector<std::string> message) override
    {
        if (m_raise_app_fn) {
            m_raise_app_fn();
        }
        m_file_downloader_interactor.download_files_prusaslicer_url(message);
    }

    void download_model_from_printables_tab(FileDownloader::FileDownloaderMultiTicket data)
    {
        m_file_downloader_interactor.init_multi_job(std::move(data));
    }

    /**
     * @brief Callback from FileDownloader.
     */
    void on_model_downloaded(const std::vector<boost::filesystem::path>& paths, bool in_new_project) override;

    void open_downloaded_file(const boost::filesystem::path& path, bool in_new_project)
    {
        if (in_new_project) {
            new_project();
        }
        load_models_to_project({path});
    }

    /**
     * @brief Callback from AppInstanceMessageHandler.
     */
    void on_read_token_store_message() override
    {
        m_user_account_interactor.on_read_token_store_message();
    }

    /**
     * @brief Callback from AppInstanceMessageHandler.
     */
    void on_login_data(const std::string& message) override
    {
        m_user_account_interactor.on_log_in_code_response(message);
    }

    /**
     * @brief Getter for user account.
     */
    UserAccount::UserAccountInteractor& user_account_interactor()
    {
        return m_user_account_interactor;
    }

    /**
     * @brief Getter for exporting / uploading logic.
     */
    ResultExport::ResultExportInteractor& result_export_interactor()
    {
        return m_result_export_interactor;
    }

    std::string get_project_name(Domain::SelectionId project_id) const;

    /**
     * @returns Project filename, or if empty name of the first object if there is any
     */
    std::string get_project_save_name(Domain::SelectionId project_id) const;

    /**
     * @brief Getter for default path when exporting 3mf file.
     */
    boost::filesystem::path project_dir(Domain::SelectionId project_id, const std::string& app_config_val) const;
    void set_project_dir(Domain::SelectionId project_id, const boost::filesystem::path& path);

    /**
     * @brief Getter for default path when exporting gcode.
     */
    boost::filesystem::path output_dir(Domain::SelectionId project_id, bool only_removable, const std::string& app_config_val) const;
    void set_output_dir(Domain::SelectionId project_id, const boost::filesystem::path& path);

    std::string output_extension(Domain::SelectionId project_id, const std::string& app_config_val) const;
    void set_output_extension(Domain::SelectionId project_id, const std::string& ext);

    ObservableProjectList& observable_project_list();

    /**
     * @brief Getter for Preset Updater.
     */
    PresetUpdater::PresetUpdaterInteractor& preset_updater_interactor()
    {
        return m_preset_updater_interactor;
    }

    /**
     * @brief Getter for RemovableDriveService.
     */
    RemovableDrive::RemovableDriveService& removable_drive_service()
    {
        return m_removable_drive_service;
    }

    /**
     * @brief Passing information from system about changed volumes to Removable Drive service.
     */
    void handle_volumes_changed_event()
    {
        m_removable_drive_service.handle_volumes_changed_event();
    }

    void set_dialog_provider(IMessageDialogProvider* dialog_provider);

    /*
     * @brief Sets callback to mainframe to bring application forward.
     */
    void set_raise_app_fn(std::function<void(void)> fn)
    {
        m_raise_app_fn = fn;
    }

    const std::function<void(void)>& raise_app_fn() const
    {
        return m_raise_app_fn;
    }

    PhysicalPrinter::PhysicalPrinterInteractor& physical_printer_interactor()
    {
        return m_physical_printer_interactor;
    }

    Connect::ConnectMessageHandler& connect_message_handler()
    {
        return m_connect_message_handler;
    }

private:
    void on_slicing_input_changed(const Domain::BedRef& bed_instance) override;
    void on_slicing_input_removed(const Domain::BedRef& bed_instance) override;
    void on_colors_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const std::vector<Domain::ColorRGB>& colors
    ) override;
    void on_virtual_extruders_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;

    Domain::SelectionId add_project(Domain::Project&& p, InvokeLaterBag& bag);
    void do_select_project(Domain::SelectionId project_id, InvokeLaterBag& bag);
    void do_select_config_container(Domain::SelectionId container_id);

    /**
     * @brief Integrates a fully-built project (config containers carrying selected
     * presets) into the workbench: registers the runtime presets, creates missing
     * beds, selects the first bed and config container, prepares the project and fires
     * on_project_loaded / on_project_load_failed. The shared body of load_project()
     * and new_project_with_preset().
     * @param project_file_path When given, it is stored as the project directory.
     * @return ID of the integrated project, or the error message on failure (the
     * project is then removed and the previous selection restored).
     */
    tl::expected<Domain::SelectionId, std::string> do_load_project(
        Domain::Project&& project,
        const std::optional<boost::filesystem::path>& project_file_path = std::nullopt
    );

    void do_result_export_inner(
        const Domain::SlicingId id,
        PhysicalPrinter::PhysicalPrinterConfig&& print_host_config,
        PrintHost::PrintHostJobData&& job_data
    );

private:
    struct Selection
    {
        Domain::SelectionId project_id{Domain::INVALID_ID};
        std::map<Domain::SelectionId, Domain::SelectionId> project_config_container;

        Domain::SelectionId config_container_id() const;
        void set_config_container_id(Domain::SelectionId container_id);
    };

    Domain::Workbench& m_workbench;
    Selection m_selection;

    Scene::SceneInteractor m_scene_interactor;
    BackupStore m_backup_store;
    Preset::PresetInteractor m_preset_interactor;
    ArrangeInteractor m_arrange_interactor;
    ClipboardInteractor m_clipboard_interactor;
    NullMdb m_null_mdb;
    ProjectSettingsInteractor m_project_settings_interactor;
    VirtualExtruderInteractor m_virtual_extruder_interactor;
    Slicing::SlicingInteractor m_slicing_interactor;
    FDMResultCache m_fdm_result_cache;
    SLAResultCache m_sla_result_cache;
    SLAObjectCache m_sla_object_cache;
    GeneratedSupportPointsCache m_generated_support_points_cache;
    StatusCache m_status_cache;
    ResultExport::ResultExportInteractor m_result_export_interactor;
    UserAccount::UserAccountInteractor m_user_account_interactor;
    PresetUpdater::PresetUpdaterInteractor m_preset_updater_interactor;
    RemovableDrive::RemovableDriveService m_removable_drive_service;
    FileDownloader::FileDownloaderInteractor m_file_downloader_interactor;
    PhysicalPrinter::PhysicalPrinterInteractor m_physical_printer_interactor;
    Connect::ConnectMessageHandler m_connect_message_handler;

    ObservableProjectList m_project_list;
    IMessageDialogProvider* m_dialog_provider{ nullptr };
    std::unique_ptr<IUndoProvider> m_undo_provider;


    /*
     * @brief Callback to mainframe to bring application forward.
     */
    std::function<void(void)> m_raise_app_fn;
};

} // namespace Slic3r::Biz
