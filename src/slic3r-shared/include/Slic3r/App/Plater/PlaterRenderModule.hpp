#pragma once

#include <memory>

#include "Slic3r/App/Undo/Store.hpp"
#include "Slic3r/App/InvalidDataDialog.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/App/Platform/AbstractRenderModule.hpp"
#include "Slic3r/App/Platform/CommandExecutionNotifier.hpp"
#include "Slic3r/Biz/Emboss/IFontManager.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/DialogNavigation.hpp"
#include "Slic3r/App/MenuManager.hpp"
#include "Slic3r/App/CommandBindingManager.hpp"
#include "Slic3r/App/Lua/PluginSystem.hpp"
#include "Slic3r/App/Scene/GizmoManager.hpp"
#include "Slic3r/App/Scene/ModelGeometryProvider.hpp"
#include "Slic3r/App/Plater/ContextMenuGizmo.hpp"
#include "Slic3r/App/WelcomeDialog.hpp"
#include "Slic3r/App/ModalDialog.hpp"

namespace Slic3r::Biz {
class ThumbnailImageProvider;
} // namespace Slic3r::Biz

namespace Slic3r::App {
struct ThumbnailStore;
class ThumbnailStoreUpdater;
class Navigator;
class ObjectListWindow;
class SidebarBed;
class SidebarObject;
class SidebarPrint;
class CubeView;
class TopBar;
class PreferencesDialog;
class ToolBarButton;
class ToolBarSwitchButton;
class NumberEntryDialog;
class CrashedProjectsDialog;
class ProjectSaver;
class PresetUpdaterDialog;
} // namespace Slic3r::App

namespace Slic3r::App::Lua {
class PluginDialog;
}

namespace Slic3r::App::Yoga {
class Menu;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::PopNotification {
class PopNotificationListView;
} // namespace Slic3r::App::PopNotification

namespace Slic3r::App::Plater {
class TranslationGizmo;
class RotationGizmo;
class ScaleGizmo;
class PlaceOnFaceGizmo;
class PaintOnSupportsGizmo;
class PaintOnSeamsGizmo;
class PaintOnFuzzySkinGizmo;
class MultiMaterialPaintingGizmo;
class SimplifyGizmo;
class TextGizmo;
class SvgGizmo;
class MeasureGizmo;
class PlaterCameraGizmo;
class CutGizmo;
class VariableLayerHeightGizmo;
class HeightRangeGizmo;
class ArrangeGizmo;
class PlaterScenePresenter;
class PlaterRenderLayout;
class SidebarPlaterActionButtons;
class History;
class ThumbnailImageGenerator;

class PlaterRenderModule final :
    public Platform::AbstractRenderModule,
    public Platform::ICommandExecutedListener,
    public Biz::IStatusCacheChangedListener,
    public Biz::Scene::ISceneSelectionChangedListener,
    private Scene::IGizmoActiveToolListener,
    public Biz::ISelectedProjectChangedListener,
    public Biz::Preset::IPresetChangedListener,
    public Scene::ISharedModelGeometryProvider,
    public IShowContextMenuListener
{
public:
    PlaterRenderModule(
        const Domain::Workbench& workbench,
        Biz::ProjectInteractor& project_interactor,
        App::Undo::Store& undo_store,
        std::shared_ptr<ThumbnailStore> thumbnail_store,
        std::shared_ptr<ThumbnailStoreUpdater> thumbnail_store_updater,
        std::shared_ptr<ThumbnailImageGenerator> thumbnail_image_generator,
        std::unique_ptr<Biz::Emboss::IFontManager> font_manager,
        std::shared_ptr<ProjectSaver> project_saver
    );
    ~PlaterRenderModule();

    void render_scene(Render::CommandBuffer& cmd_buffer) override;
    void render_imgui(Render::CommandBuffer& cmd_buffer) override;
    void on_scene_mouse_event(const Platform::MouseEvent& e) override;
    void on_scene_keyboard_event(const Platform::KeyboardEvent& e) override;
    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;

    void on_command_executed() override;

    void set_navigator(Navigator* navigator) override;

    void on_status_cache_status_code_changed(const Domain::SlicingId id) override;
    void on_show_context_menu(ContextMenuType type, Domain::Vec2f mouse_pos) override;

    void set_sidebars_visible(bool visible) override;

    const std::optional<Platform::CameraSynchData>& camera_synch_data() const override;
    void set_camera_synch_data(const Platform::CameraSynchData& data) override;

    void set_opened_dialog(Yoga::Dialog* opened_dialog);
    void open_invalid_data_dialog();

    void navigate_to_item(const Domain::ConfigItem* config_item);

    void open_search();
    void set_modal_dialog(ModalDialog dialog);

    void set_object_list_collapsed(bool collapsed);

    virtual void get_user_number_and_process(
        const std::string& message,
        const std::string& prompt,
        const std::string& title,
        int value,
        int min,
        int max,
        std::function<void(int)> on_process
    ) override;

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
        return m_gizmo_manager.get();
    }

    Scene::IToolGizmo* tool_gizmo(Scene::ToolType type, Domain::PrinterTechnology pt) override;

    Scene::IGizmoController& gizmo_controller();

    /**
     * @name Implementation of Scene::ISharedModelGeometryProvider public interface
     * @{
     */
    std::shared_ptr<Scene::ModelGeometryProvider> shared_model_geometry_provider() override;
    /**@}*/

    Lua::PluginSystem& plugin_system();

protected:
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
    /**
     * @name Implementation of Biz::ISelectedProjectChangedListener public interface
     * @{
     */
    void on_selected_project_changed(size_t index) override;
    /**@}*/

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;

private:
    void active_tool_changed(Scene::IToolGizmo* active_tool) override;

    void init_scene();
    void init_scene_layout();
    void
    render_object_hud(const Scene::Node& n, const Eigen::AlignedBox<float, 2>& screen_bounding_box);
    void toggle_activate_tool(Scene::ToolType tool_type);
    void init_dialog_navigation();
    void update_object_selection();
    void update_current_right_sidebar();
    void update_toolbar_visibility();

    void init_gizmos();
    void close_all_menus();

    ToolBarButton* get_toolbar_button(Scene::ToolType tool_type) const;

private:
    const Domain::Workbench& m_workbench;
    Biz::ProjectInteractor& m_project_interactor;
    App::Undo::Store& m_undo_store;
    std::unique_ptr<PlaterScenePresenter> m_scene_presenter;
    std::unique_ptr<Scene::GizmoManager> m_gizmo_manager;

    Yoga::Menu* m_bed_menu = nullptr;
    Yoga::Menu* m_object_menu = nullptr;
    Yoga::Menu* m_volume_menu = nullptr;
    Yoga::Menu* m_multi_objects_menu = nullptr;
    Yoga::Menu* m_svg_or_text_menu = nullptr;

    // We need these to outlive the TopBar bellow
    std::unique_ptr<Biz::Emboss::IFontManager> m_font_manager = nullptr;
    MenuManager m_menu_manager;
    CommandBindingManager m_command_binding_manager;
    Lua::PluginSystem m_plugin_system;

    // main window layout
    std::unique_ptr<PlaterRenderLayout> m_layout;
    // Layout objects
    Yoga::Passthrough<TopBar> m_top_bar;
    Yoga::Passthrough<ObjectListWindow> m_object_list;
    Yoga::Passthrough<CubeView> m_cube_view;
    Yoga::Passthrough<PopNotification::PopNotificationListView> m_pop_notification_list_view;
    Yoga::Passthrough<SidebarBed> m_sidebar_bed;
    Yoga::Passthrough<SidebarPrint> m_sidebar_print;
    Yoga::Passthrough<SidebarObject> m_sidebar_object;
    Yoga::Passthrough<SidebarPlaterActionButtons> m_sidebar_action_buttons;
    Yoga::Passthrough<History> m_history;
    Yoga::Passthrough<PreferencesDialog> m_preferences_dialog;
    Yoga::Passthrough<NumberEntryDialog> m_number_entry_dialog;
    Yoga::Passthrough<PresetUpdaterDialog> m_preset_updater_dialog;
    Yoga::Passthrough<WelcomeDialog> m_welcome_dialog;
    Yoga::Passthrough<InvalidDataDialog> m_invalid_data_dialog;
    Yoga::Passthrough<CrashedProjectsDialog> m_crashed_projects_dialog;

    ToolBarButton* m_toolbar_add                     = nullptr;
    ToolBarButton* m_toolbar_delete                  = nullptr;
    ToolBarButton* m_toolbar_add_instance            = nullptr;
    ToolBarButton* m_toolbar_move                    = nullptr;
    ToolBarButton* m_toolbar_rotate                  = nullptr;
    ToolBarButton* m_toolbar_scale                   = nullptr;
    ToolBarButton* m_toolbar_place_on_face           = nullptr;
    ToolBarButton* m_toolbar_simplify                = nullptr;
    ToolBarButton* m_toolbar_arrange                 = nullptr;
    ToolBarButton* m_toolbar_paint_on_supports       = nullptr;
    ToolBarButton* m_toolbar_paint_on_seams          = nullptr;
    ToolBarButton* m_toolbar_paint_on_fuzzy_skin     = nullptr;
    ToolBarButton* m_toolbar_multi_material_painting = nullptr;
    ToolBarButton* m_toolbar_text                    = nullptr;
    ToolBarButton* m_toolbar_svg                     = nullptr;
    ToolBarButton* m_toolbar_measure                 = nullptr;
    ToolBarButton* m_toolbar_cut                     = nullptr;
    ToolBarButton* m_toolbar_variable_layer_height   = nullptr;
    ToolBarButton* m_toolbar_height_range            = nullptr;
    ToolBarSwitchButton* m_toolbar_preview_switch    = nullptr;

    TranslationGizmo* m_translation_gizmo                       = nullptr;
    RotationGizmo* m_rotation_gizmo                             = nullptr;
    ScaleGizmo* m_scale_gizmo                                   = nullptr;
    PlaceOnFaceGizmo* m_place_on_face_gizmo                     = nullptr;
    ArrangeGizmo* m_arrange_gizmo                               = nullptr;
    SimplifyGizmo* m_simplify_gizmo                             = nullptr;
    PaintOnSupportsGizmo* m_paint_on_supports_gizmo             = nullptr;
    PaintOnSeamsGizmo* m_paint_on_seams_gizmo                   = nullptr;
    PaintOnFuzzySkinGizmo* m_paint_on_fuzzy_skin_gizmo          = nullptr;
    MultiMaterialPaintingGizmo* m_multi_material_painting_gizmo = nullptr;
    TextGizmo* m_text_gizmo                                     = nullptr;
    SvgGizmo* m_svg_gizmo                                       = nullptr;
    MeasureGizmo* m_measure_gizmo                               = nullptr;
    PlaterCameraGizmo* m_camera_gizmo                           = nullptr;
    CutGizmo* m_cut_gizmo                                       = nullptr;
    VariableLayerHeightGizmo* m_variable_layer_height_gizmo     = nullptr;
    HeightRangeGizmo* m_height_range_gizmo                      = nullptr;

    std::shared_ptr<ThumbnailStore> m_thumbnail_store;
    std::shared_ptr<ThumbnailStoreUpdater> m_thumbnail_store_updater;
    std::shared_ptr<Plater::ThumbnailImageGenerator> m_thumbnail_image_generator;
    std::shared_ptr<ProjectSaver> m_project_saver;

    Navigator* m_render_module_navigator{nullptr};

    DialogNavigation m_dialog_navigation;

    std::set<Yoga::Dialog*> m_gizmo_dialogs;
};

} // namespace Slic3r::App::Plater
