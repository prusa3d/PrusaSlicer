#pragma once

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/ProjectScoped.hpp"

#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/InvalidDataDialog.hpp"
#include "Slic3r/App/Platform/AbstractRenderModule.hpp"
#include "Slic3r/App/Preview/PreviewScenePresenter.hpp"
#include "Slic3r/App/Scene/GizmoManager.hpp"
#include "Slic3r/App/Preview/PreviewRenderLayout.hpp"
#include "Slic3r/App/Preview/FdmViewerWrapper.hpp"
#include "Slic3r/App/Preview/SlaViewerWrapper.hpp"
#include "Slic3r/App/Preview/SidebarPreviewActionButtons.hpp"
#include "Slic3r/App/Preview/SidebarAutoReslice.hpp"
#include <Slic3r/App/Preview/LegendWindow.hpp>
#include <Slic3r/App/Preview/GCodeWindow.hpp>
#include <Slic3r/App/Preview/DoubleSliderForGCode.hpp>
#include <Slic3r/App/Preview/DoubleSliderForLayers.hpp>
#include "Slic3r/App/DialogNavigation.hpp"
#include "Slic3r/App/MenuManager.hpp"
#include "Slic3r/App/CommandBindingManager.hpp"
#include "Slic3r/App/Scene/ModelGeometryProvider.hpp"
#include "Slic3r/App/ModalDialog.hpp"

#include <map>
#include <memory>
#include <optional>

namespace Slic3r::App {
struct ThumbnailStore;
class ThumbnailStoreUpdater;
class Navigator;
class ProjectSaver;
} // namespace Slic3r::App

namespace Slicer::App::Lua {
class PluginSystem;
}

namespace Slic3r::App::Preview {

struct ExtrudersSequence;
class SidebarPreviewActionButtons;
class PreviewCameraGizmo;

// Remembers the gcode view type (Speed, FeatureType, ...) the user explicitly picked in the
// legend, per ConfigContainer, so autoslicing / project switching doesn't reset it. Cleared
// whenever the heuristic that would otherwise pick the type changes (e.g. printer switched to
// a multiextruder one), since the user's choice was made in the context of the old heuristic.
struct GCodeViewTypeState
{
    std::optional<libvgcode::ViewType> user_choice;
    std::optional<libvgcode::ViewType> last_heuristic;
};

struct ProjectGCodeViewTypeStates
{
    std::map<Domain::SelectionId, GCodeViewTypeState> by_config_container;
};

class PreviewRenderModule final :
    public Platform::AbstractRenderModule,
    public Biz::ISelectedBedInstancesChangedListener,
    public Biz::IFDMResultCacheChangedListener,
    public Biz::ISelectedProjectChangedListener,
    public Biz::ISLAResultCacheChangedListener,
    public Biz::ISLAObjectCacheChangedListener,
    public Biz::IStatusCacheChangedListener,
    public Biz::Scene::ISceneBedInstanceChangedListener
{
public:
    PreviewRenderModule(
        const Domain::Workbench& workbench,
        Biz::ProjectInteractor& project_interactor,
        App::Undo::Store& undo_store,
        std::shared_ptr<ThumbnailStore> thumbnail_store,
        std::shared_ptr<ThumbnailStoreUpdater> thumbnail_store_updater,
        std::shared_ptr<Plater::ThumbnailImageGenerator> thumbnail_image_generator,
        Scene::ISharedModelGeometryProvider* model_geometry_provider,
        Lua::PluginSystem* plugin_system,
        std::shared_ptr<ProjectSaver> project_saver
    ) :
        m_workbench(workbench),
        m_project_interactor(project_interactor),
        m_undo_store(undo_store),
        m_menu_manager(m_command_registry),
        m_command_binding_manager(m_command_registry),
        m_shared_model_geometry_provider(model_geometry_provider),
        m_thumbnail_store(thumbnail_store),
        m_thumbnail_store_updater(thumbnail_store_updater),
        m_thumbnail_image_generator(thumbnail_image_generator),
        m_plugin_system(plugin_system),
        m_gcode_view_type_states(m_project_interactor),
        m_project_saver(project_saver)
    {}

    /**
     * @name Implementation of Platform::AbstractRenderModule public interface
     * @{
     */
    void render_scene(Render::CommandBuffer& cmd_buffer) override;
    void render_imgui(Render::CommandBuffer& cmd_buffer) override;
    void on_scene_mouse_event(const Platform::MouseEvent& e) override;
    void on_scene_keyboard_event(const Platform::KeyboardEvent& e) override;
    void set_navigator(Navigator* navigator) override;
    /**@}*/

    void on_selected_bed_instances_changed(Domain::SelectionId project_id, const Biz::Scene::BedSelection& selection) override;

    void on_fdm_result_cache_changed(const Domain::SlicingId id) override
    {
        update_fdm_viewer_data(id);
    }

    void on_sla_result_cache_changed(const Domain::SlicingId& id) override
    {
        update_sla_viewer_result_data(id);
    }

    void on_sla_object_cache_changed(const Domain::SlicingId& id, Domain::ObjectID instance_id) override
    {
        update_sla_viewer_object_data(id, instance_id);
    }

    void on_status_cache_status_code_changed(const Domain::SlicingId id) override;

    /**
     * @name Implementation of Biz::ISelectedProjectChangedListener public interface
     * @{
     */
    void on_selected_project_changed(size_t index) override;
    /**@}*/

    /**
     * @name Partial implementation of Biz::Scene::ISceneBedInstanceChangedListener public interface
     * @{
     */
    void on_bed_instance_updated(Domain::SelectionId project_id, const Domain::BedRefs& instances) override;
    /**@}*/

    void set_sidebars_visible(bool hide) override;

    const std::optional<Platform::CameraSynchData>& camera_synch_data() const override;
    void set_camera_synch_data(const Platform::CameraSynchData& data) override;

    void set_opened_dialog(Yoga::Dialog* opened_dialog);
    void open_invalid_data_dialog();

    void set_modal_dialog(ModalDialog dialog);

    void set_object_list_collapsed(bool collapsed);

    MenuManager& menu_manager() override
    {
        return m_menu_manager;
    }

    CommandBindingManager& command_binding_manager() override
    {
        return m_command_binding_manager;
    }

    const Platform::CommandRegistry::CommandsMap& gizmo_commands() const override
    {
        ASSERT(m_gizmo_manager);
        return m_gizmo_manager->commands();
    }

    const Platform::ICommand& command(const char* name) const override
    {
        if (gizmo_commands().contains(name)) {
            return m_gizmo_manager->command(name);
        }
        return m_command_registry.command(name);
    }

    bool is_gizmo_manager_completed() const override
    {
        return m_gizmo_manager ? true : false;
    }

    TopBar* top_bar()
    {
        return m_top_bar.get();
    }

protected:
    /**
     * @name Implementation of Platform::AbstractRenderModule protected interface
     * @{
     */
    void on_init(
        Render::Device& device,
        Render::ImguiRender& imgui_render,
        Platform::AnimationManager& animation_manager
    ) override;
    void on_activated() override;
    void on_deactivated() override;
    void on_screen_resized() override;
    void register_commands() override;
    void bind_commands() override;
    /**@}*/

    void update_current_right_sidebar();

private:
    const Domain::Workbench& m_workbench;
    Biz::ProjectInteractor& m_project_interactor;
    Undo::Store& m_undo_store;
    std::unique_ptr<PreviewScenePresenter> m_scene_presenter;
    std::unique_ptr<Scene::GizmoManager> m_gizmo_manager;
    DialogNavigation m_dialog_navigation;
    MenuManager m_menu_manager;
    CommandBindingManager m_command_binding_manager;
    Scene::ISharedModelGeometryProvider* m_shared_model_geometry_provider{ nullptr };

    PreviewCameraGizmo* m_camera_gizmo{ nullptr };

    FdmViewerWrapper m_fdm_viewer;
    SlaViewerWrapper m_sla_viewer;

    AbstractViewerWrapper* m_viewer = nullptr;

    // main window layout
    std::unique_ptr<PreviewRenderLayout> m_layout;
    // Layout objects
    Yoga::Passthrough<TopBar> m_top_bar;
    Yoga::Passthrough<ObjectListWindow> m_object_list;
    Yoga::Passthrough<CubeView> m_cube_view;
    Yoga::Passthrough<PopNotification::PopNotificationListView> m_pop_notification_list_view;
    Yoga::Passthrough<SidebarBed> m_sidebar_bed;
    Yoga::Passthrough<SidebarPrint> m_sidebar_print;
    Yoga::Passthrough<SidebarObject> m_sidebar_object;
    Yoga::Passthrough<SidebarPreviewActionButtons> m_sidebar_action_buttons;
    Yoga::Passthrough<GCodeWindow> m_gcode_window;
    Yoga::Passthrough<LegendWindow> m_legend;
    Yoga::Passthrough<DoubleSliderForGcode> m_slider_gcode;
    Yoga::Passthrough<DoubleSliderForLayers> m_slider_layers;
    Yoga::Passthrough<DoubleSliderForLayers> m_sla_slider_layers;
    Yoga::Passthrough<SidebarAutoReslice> m_sidebar_auto_reslice;
    Yoga::Passthrough<PreferencesDialog> m_preferences_dialog;
    Yoga::Passthrough<NumberEntryDialog> m_number_entry_dialog;
    Yoga::Passthrough<InvalidDataDialog> m_invalid_data_dialog;
    Yoga::Passthrough<CrashedProjectsDialog> m_crashed_projects_dialog;
    Yoga::Passthrough<PresetUpdaterDialog> m_preset_updater_dialog;
    // temporary variable to allow to switch yoga layout on/off

    ToolBarButton* m_button_travels           = nullptr;
    ToolBarButton* m_button_wipes             = nullptr;
    ToolBarButton* m_button_retractions       = nullptr;
    ToolBarButton* m_button_unretractions     = nullptr;
    ToolBarButton* m_button_seams             = nullptr;
    ToolBarButton* m_button_tool_changes      = nullptr;
    ToolBarButton* m_button_color_changes     = nullptr;
    ToolBarButton* m_button_pause_prints      = nullptr;
    ToolBarButton* m_button_custom_gcodes     = nullptr;
    ToolBarButton* m_button_center_of_gravity = nullptr;
    ToolBarButton* m_button_tool_marker       = nullptr;
    ToolBarButton* m_button_shells            = nullptr;
    ToolBarButton* m_button_plater_switch     = nullptr;

    ToolBarButton* m_button_gcode_inspect = nullptr;

    std::shared_ptr<ThumbnailStore> m_thumbnail_store;
    std::shared_ptr<ThumbnailStoreUpdater> m_thumbnail_store_updater;
    std::shared_ptr<Plater::ThumbnailImageGenerator> m_thumbnail_image_generator;

    Navigator* m_render_module_navigator{nullptr};
    Lua::PluginSystem* m_plugin_system{nullptr};
    Biz::ProjectScoped<ProjectGCodeViewTypeStates> m_gcode_view_type_states;
    std::shared_ptr<ProjectSaver> m_project_saver;
    bool m_active{false};


private:
    void init_gizmos();
    void init_viewers(Render::Device& device);
    void update_fdm_viewer_data(const Domain::SlicingId id);
    void update_sla_viewer_result_data(const Domain::SlicingId id);
    void update_sla_viewer_object_data(const Domain::SlicingId id, Domain::ObjectID instance_id);
    void update_sla_viewer_data(const Domain::SlicingId id);
    void init_scene_layout();
    void update_toolbar_visibility();
    void init_dialog_navigation();

    void on_invalidate_slice();
    void on_update_layers_slider(const Domain::CustomGCode::Info& info);
    void on_request_extra_frames(unsigned int count = 1);
    void on_gcode_view_type_changed();
    void on_slider_layers_on_thumb_move();
    void on_slider_layers_ticks_changed();
    bool on_slider_layers_auto_color_change();
    void on_slider_layers_notify_empty_auto_color_change();
    void on_slider_layers_notify_empty_color_change_gcode();
    bool on_slider_layers_get_extruders_sequence(ExtrudersSequence& sequence);
    int on_slider_layers_show_info_msg(const std::string& message, int btns_flag);
    std::set<int> on_slider_layers_get_used_extruders_in_print(float print_z);
    void on_slider_layers_app_config_changed(const std::string& key, bool val);
    void on_slider_gcode_on_thumb_move();

    void update_shells();
    void update_bed_instances();
    void update_viewer();
    void update_scene_aabb();
};

} // namespace Slic3r::App::Preview
