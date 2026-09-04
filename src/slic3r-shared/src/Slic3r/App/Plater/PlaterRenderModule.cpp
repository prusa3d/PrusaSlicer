#include "Slic3r/App/Plater/PlaterRenderModule.hpp"

#include "Slic3r/Directories.hpp"
#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/Image.hpp"

#include "Slic3r/Biz/FileLoadingLogic.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/App/Scene/NodeVisitor.hpp"
#include "Slic3r/App/Scene/LightingHelper.hpp"
#include "Slic3r/App/Scene/MouseDragDetector.hpp"
#include "Slic3r/App/Scene/BedMaterials.hpp"
#include "Slic3r/App/Scene/BedRenderHelper.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/ScopedDebugGroup.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/Domain/Image.hpp"
#include "Slic3r/App/Plater/PlaterCameraGizmo.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/App/Plater/GizmoNodeTag.hpp"
#include "Slic3r/App/Scene/BedNodeTag.hpp"
#include "Slic3r/App/Scene/BedMaterials.hpp"
#include "Slic3r/App/Scene/BedRenderHelper.hpp"
#include "Slic3r/App/Scene/EmbossCreate.hpp"
#include "Slic3r/App/Plater/QuickSelectGizmo.hpp"
#include "Slic3r/App/Plater/QuickDragGizmo.hpp"
#include "Slic3r/App/Plater/BedSelectGizmo.hpp"
#include "Slic3r/App/Plater/TranslationGizmo.hpp"
#include "Slic3r/App/Plater/RotationGizmo.hpp"
#include "Slic3r/App/Plater/ScaleGizmo.hpp"
#include "Slic3r/App/Plater/PlaceOnFaceGizmo.hpp"
#include "Slic3r/App/Plater/SimplifyGizmo.hpp"
#include "Slic3r/App/Plater/SimplifyNotification.hpp"
#include "Slic3r/App/Plater/PaintOnSupportsGizmo.hpp"
#include "Slic3r/App/Plater/PaintOnSupportsDialog.hpp"
#include "Slic3r/App/Plater/PaintOnSeamsGizmo.hpp"
#include "Slic3r/App/Plater/PaintOnSeamsDialog.hpp"
#include "Slic3r/App/Plater/PaintOnFuzzySkinGizmo.hpp"
#include "Slic3r/App/Plater/PaintOnFuzzySkinDialog.hpp"
#include "Slic3r/App/Plater/MultiMaterialPaintingGizmo.hpp"
#include "Slic3r/App/Plater/MultiMaterialPaintingDialog.hpp"
#include "Slic3r/App/Plater/MeasureGizmo.hpp"
#include "Slic3r/App/Plater/MeasureDialog.hpp"
#include "Slic3r/App/Plater/TextDialog.hpp"
#include "Slic3r/App/Plater/TextGizmo.hpp"
#include "Slic3r/App/Plater/SvgDialog.hpp"
#include "Slic3r/App/Plater/SvgGizmo.hpp"
#include "Slic3r/App/Plater/ArrangeGizmo.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Plater/PlaterRenderLayout.hpp"
#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Plater/CutGizmo.hpp"
#include "Slic3r/App/Plater/CutDialog.hpp"
#include "Slic3r/App/Plater/VariableLayerHeightGizmo.hpp"
#include "Slic3r/App/Plater/VariableLayerHeightDialog.hpp"
#include "Slic3r/App/Plater/HeightRangeGizmo.hpp"
#include "Slic3r/App/Plater/HeightRangeDialog.hpp"
#include "Slic3r/App/Plater/ToolGizmosUiInfo.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/ThumbnailStoreUpdater.hpp"
#include "Slic3r/App/Platform/AnimationManager.hpp"
#include "Slic3r/App/Scene/CameraHelper.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/Plater/History.hpp"
#include "Slic3r/App/CubeView.hpp"
#include "Slic3r/App/SidebarBed.hpp"
#include "Slic3r/App/SidebarPrint.hpp"
#include "Slic3r/App/SidebarObject.hpp"
#include "Slic3r/App/SidebarActionButtons.hpp"
#include "Slic3r/App/LightSetting.hpp"
#include "Slic3r/App/Plater/SidebarPlaterActionButtons.hpp"
#include "Slic3r/App/ToolBar/ToolBarButton.hpp"
#include "Slic3r/App/Yoga/Menu.hpp"
#include "Slic3r/App/Yoga/MenuItem.hpp"
#include "Slic3r/App/ToolBar/ToolBar.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/RenderModuleHelper.hpp"
#include "Slic3r/App/TopBar.hpp"
#include "Slic3r/App/Wildcards.hpp"
#include "Slic3r/App/PreferencesDialog.hpp"
#include "Slic3r/App/NumberEntryDialog.hpp"
#include "Slic3r/App/InvalidDataDialog.hpp"
#include "Slic3r/App/SidebarStackLayout.hpp"
#include "Slic3r/App/ColorMix/ColorMixDialog.hpp"
#include "Slic3r/App/LogicalPrinterSettingsDialog.hpp"
#include "Slic3r/App/PhysicalPrinterSettingsDialog.hpp"
#include "Slic3r/App/PhysicalPrinterAdvancedSettingsDialog.hpp"
#include "Slic3r/App/PrinterAdvancedSettingsDialog.hpp"
#include "Slic3r/App/MaterialSelectionDialog.hpp"
#include "Slic3r/App/MaterialSettingsDialog.hpp"
#include "Slic3r/App/PrintSettingsDialog.hpp"
#include "Slic3r/App/PrinterAddDialog.hpp"
#include "Slic3r/App/PresetUpdater/PresetUpdaterDialog.hpp"
#include "Slic3r/App/UIItemCommand.hpp"
#include "Slic3r/App/MenuBuilder.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/Config/ConfigItemControl.hpp"
#include "Slic3r/App/Lua/PluginDialog.hpp"
#include "Slic3r/App/CrashedProjectsDialog.hpp"

#include <imgui/imgui.h>
#include <Eigen/SVD>
#include <boost/algorithm/string/predicate.hpp> // iends_with
#include <tracy/Tracy.hpp>

#define ENABLED_DEBUG_BEDS 0
#define ENABLED_NODE_LOGGING 0

using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec4d;

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

namespace Slic3r::App::Plater {

using CommandName          = Platform::CommandName;
using FuncCommandExtraOpts = Platform::FuncCommandExtraOpts;

PlaterRenderModule::PlaterRenderModule(
    const Domain::Workbench& workbench,
    Biz::ProjectInteractor& project_interactor,
    App::Undo::Store& undo_store,
    std::shared_ptr<ThumbnailStore> thumbnail_store,
    std::shared_ptr<ThumbnailStoreUpdater> thumbnail_store_updater,
    std::shared_ptr<Plater::ThumbnailImageGenerator> thumbnail_image_generator,
    std::unique_ptr<Biz::Emboss::IFontManager> font_manager,
    std::shared_ptr<ProjectSaver> project_saver
) :
    m_workbench(workbench),
    m_project_interactor(project_interactor),
    m_undo_store(undo_store),
    m_font_manager(std::move(font_manager)),
    m_menu_manager(m_command_registry),
    m_command_binding_manager(m_command_registry),
    m_plugin_system(
        {resources_dir() + "/lua", data_dir() + "/lua"},
        project_interactor,
        *m_font_manager
    ),
    m_thumbnail_store(thumbnail_store),
    m_thumbnail_store_updater(thumbnail_store_updater),
    m_thumbnail_image_generator(thumbnail_image_generator),
    m_project_saver(project_saver)
{}

PlaterRenderModule::~PlaterRenderModule()
{
    if (m_gizmo_manager) {
        m_gizmo_manager->remove_listener<IGizmoActiveToolListener>(this);
    }
}

void PlaterRenderModule::set_opened_dialog(Yoga::Dialog* opened_dialog)
{
    m_dialog_navigation.open_dialog(opened_dialog);
}

void PlaterRenderModule::set_modal_dialog(ModalDialog modal_dialog)
{
    auto handle_dialog = [&](Yoga::Popup* dialog, ModalDialog modal){
        if (modal_dialog == modal) {
            dialog->open();
        } else {
            dialog->close();
        }
    };

    handle_dialog(m_preferences_dialog.get(), ModalDialog::Preferences);
    handle_dialog(m_welcome_dialog.get(), ModalDialog::Welcome);
    handle_dialog(m_number_entry_dialog.get(), ModalDialog::NumberEntry);
    handle_dialog(m_crashed_projects_dialog.get(), ModalDialog::CrashedProjects);
    handle_dialog(m_preset_updater_dialog.get(), ModalDialog::PresetUpdater);

    request_render();
}

void PlaterRenderModule::open_invalid_data_dialog()
{
    if (m_invalid_data_dialog.get()) {
        set_opened_dialog(m_invalid_data_dialog.get());
    }
}

void PlaterRenderModule::set_object_list_collapsed(bool collapsed)
{
    if (m_object_list.get()) {
        m_object_list->set_collapsed(collapsed);
    }
}

void PlaterRenderModule::get_user_number_and_process(
    const std::string& message,
    const std::string& prompt,
    const std::string& title,
    int value,
    int min,
    int max,
    std::function<void(int)> on_process
)
{
    if (m_number_entry_dialog.get()) {
        m_number_entry_dialog
            ->get_user_number_and_process(message, prompt, title, value, min, max, on_process);
        set_opened_dialog(m_number_entry_dialog.get());
    }
}

Scene::IToolGizmo* PlaterRenderModule::tool_gizmo(Scene::ToolType type, Domain::PrinterTechnology pt)
{
    return m_gizmo_manager->find_tool(type, pt);
}

std::shared_ptr<Scene::ModelGeometryProvider> PlaterRenderModule::shared_model_geometry_provider()
{
    return m_scene_presenter->model_geometry_provider();
}

Lua::PluginSystem& PlaterRenderModule::plugin_system()
{
    return m_plugin_system;
}

void PlaterRenderModule::navigate_to_item(const Domain::ConfigItem* config_item)
{
    m_sidebar_bed->logical_printer_settings_dialog()
        .printer_advanced_settings_dialog()
        .clear_navigation();
    m_sidebar_print->print_settings_dialog().clear_navigation();
    m_sidebar_bed->material_selection_dialog().material_settings_dialog().clear_navigation();
    m_sidebar_print->print_settings_dialog().clear_navigation();
    m_preferences_dialog.get()->clear_navigation();

    std::visit(
        [=, this](auto&& location)
        {
            using T = std::decay_t<decltype(location)>;

            IConfigNavigable* dialog_to_open = nullptr;

            if constexpr (std::is_same_v<T, Domain::FDMConfigLocation>) {
                switch (location) {
                case Domain::FDMConfigLocation::Printer:
                    dialog_to_open = &m_sidebar_bed->logical_printer_settings_dialog()
                                          .printer_advanced_settings_dialog();
                    break;
                case Domain::FDMConfigLocation::Print:
                    dialog_to_open = &m_sidebar_print->print_settings_dialog();
                    break;
                case Domain::FDMConfigLocation::Filament: {
                    dialog_to_open =
                        &m_sidebar_bed->material_selection_dialog().material_settings_dialog();
                } break;
                case Domain::FDMConfigLocation::Tool: {
                    dialog_to_open = &m_sidebar_print->print_settings_dialog();
                } break;
                default:
                    break;
                }
            } else if constexpr (std::is_same_v<T, Domain::SLAConfigLocation>) {
                switch (location) {
                case Domain::SLAConfigLocation::Printer:
                    dialog_to_open = &m_sidebar_bed->logical_printer_settings_dialog()
                                          .printer_advanced_settings_dialog();
                    break;
                case Domain::SLAConfigLocation::Print:
                    dialog_to_open = &m_sidebar_print->print_settings_dialog();
                    break;
                case Domain::SLAConfigLocation::Material: {
                    dialog_to_open =
                        &m_sidebar_bed->material_selection_dialog().material_settings_dialog();
                } break;
                default:
                    break;
                }
            } else if constexpr (std::is_same_v<T, Domain::AppConfigLocation>) {
                dialog_to_open = m_preferences_dialog.get();
            }

            if (dialog_to_open) {
                m_render_module_navigator->set_opened_dialog(
                    dynamic_cast<Yoga::Dialog*>(dialog_to_open)
                );
                dialog_to_open->navigate_to_item(config_item);
            }
        },
        config_item->location()
    );
}

void PlaterRenderModule::open_search()
{
    m_top_bar->focus_search();
}

void PlaterRenderModule::on_init(
    Render::Device& device,
    Render::ImguiRender& imgui_render,
    Platform::AnimationManager& animation_manager
)
{
    AbstractRenderModule::on_init(device, imgui_render, animation_manager);
    // Set our color styles before gizmos initialization
    // to use them during GizmoDialogs creation
    AppServices::instance().theme().initialize_imgui_style();
    Yoga::Item::set_imgui_render(&imgui_render); // Todo: move this somewhere where it is invoked once
    Yoga::Item::set_theme(&AppServices::instance().theme());
    ConfigItemControl::set_project_interactor(&m_project_interactor);
    m_scene_presenter = std::make_unique<PlaterScenePresenter>(
        m_workbench,
        m_project_interactor,
        *m_device,
        *m_animation_manager
    );
    m_project_interactor.status_cache().add_listener<Biz::IStatusCacheChangedListener>(this);
    m_project_interactor.scene_interactor().add_listener<ISceneSelectionChangedListener>(this);
    m_project_interactor.add_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.fdm_result_cache().add_listener<Biz::IFDMResultCacheChangedListener>(m_scene_presenter.get());
    m_project_interactor.sla_result_cache().add_listener<Biz::ISLAResultCacheChangedListener>(m_scene_presenter.get());
    m_project_interactor.preset_interactor().add_listener<Biz::Preset::IPresetChangedListener>(this);

    m_project_interactor.status_cache().add_listener<Biz::IStatusCacheChangedListener>(
        &m_command_binding_manager
    );
    m_project_interactor.user_account_interactor()
        .add_listener<Biz::UserAccount::IUserAccountListener>(&m_command_binding_manager);
    m_project_interactor.scene_interactor().add_listener<ISelectedBedInstancesChangedListener>(
        &m_command_binding_manager
    );
    m_project_interactor.removable_drive_service().add_status_listener(&m_command_binding_manager);
    m_project_interactor.scene_interactor().add_listener<ISceneSelectionChangedListener>(&m_command_binding_manager);

    Platform::CommandExecutionNotifier::instance().add_listener<Platform::ICommandExecutedListener>(
        this
    );

    init_gizmos();
    init_scene();

    m_plugin_system.rescan();

    init_scene_layout();
    init_dialog_navigation();

    if (!m_thumbnail_image_generator->initialized()) {
        m_thumbnail_image_generator->init(m_workbench, *m_device, *m_scene_presenter);
    }
    m_scene_presenter->add_listener<Plater::IBedVisuallyChangedListener>(
        m_thumbnail_store_updater.get()
    );

    m_scene_presenter->force_bed_thumbnails_generation();

    m_scene_presenter->scene().set_lights(Slic3r::App::global_lighting());

    // force update on various UI elements
    update_object_selection();

    if (!AppServices::instance().app_config().get<bool>("initialized")) {
        m_welcome_dialog->open();
    }
}

void PlaterRenderModule::register_commands()
{
    auto is_instance_from_same_object_selected = [this]() -> bool
    {
        const Biz::Scene::ObjectSelection& selection =
            m_project_interactor.scene_interactor().object_selection();

        return !selection.empty()
            && selection.only_single_object()
            && selection.mode == Slic3r::Biz::Scene::SelectionMode::Instance;
    };

    // Toolbar commands
    m_command_registry
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                CommandName::AddInstance,
                [this]() -> void
                {
                    const ElementRefs new_instances =
                        m_project_interactor.scene_interactor().add_instance(Vec2d(10., 5.));
#if ENABLED_NODE_LOGGING
                    m_scene_presenter->scene().log_nodes();
#endif
                    const BedRef target_bed =
                        m_project_interactor.scene_interactor().bed_selection().last_selected_bed();

                    m_project_interactor.arrange_interactor().arrange_added_instances(
                        m_project_interactor.selected_project_id(),
                        new_instances,
                        target_bed,
                        UndoSnapshotType::AddInstance
                    );
                },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Plus},
                            Platform::KeyboardShortcut{0, Platform::KeyCode::KpPlus}
                        },
                    .enabled = is_instance_from_same_object_selected
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                CommandName::DelInstance,
                [this]() -> void
                {
                    m_project_interactor.scene_interactor().delete_selected_object_last_instance();
#if ENABLED_NODE_LOGGING
                    m_scene_presenter->scene().log_nodes();
#endif
                    m_project_interactor.undo_provider().take_snapshot(
                        UndoSnapshotType::DelInstance
                    );
                },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Minus},
                            Platform::KeyboardShortcut{0, Platform::KeyCode::KpMinus}
                        },
                    .enabled =
                        [this]()
                    {
                        const Biz::Scene::ObjectSelection& selection =
                            m_project_interactor.scene_interactor().object_selection();

                        return !selection.empty()
                            && selection.only_single_object()
                            && selection.mode == Slic3r::Biz::Scene::SelectionMode::Instance
                            && m_project_interactor.selected_project()
                                   .find_object_by_id(selection.elements.front().object_id)
                                   ->instances.size()
                            > 1;
                    }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                CommandName::SwitchToPreview,
                [this]()
                {
                    m_render_module_navigator->navigate_to_module_type(Render::ModuleType::Preview);
                },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Tab}
                        }
                }
            )
        )
        // Command for create separate text object and open Text-gizmo
        .register_command(
            std::make_unique<UIItemCommand>(
                CommandName::CreateObjectAsText,
                [this]() { m_gizmo_manager->activate_tool(Scene::ToolType::TextGizmo); },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::T}
                        },
                    .enabled = [this]() { return !m_text_gizmo->enabled(); }
                }
            )
        )
        .register_command(
            std::make_unique<UIItemCommand>(
                CommandName::DeactivateCurrentGizmo,
                [this]() { m_gizmo_manager->deactivate_current_tool(); },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Escape}
                        },
                    .enabled = [this]()
                    { return m_gizmo_manager->current_tool_type() != Scene::ToolType::None; }
                }
            )
        );

    for (const Scene::GizmoManager::IToolGizmoPtr& tool_gizmo : m_gizmo_manager->tool_gizmos()) {
        const Scene::ToolType type{tool_gizmo->type()};
        m_command_registry.register_command(
            std::make_unique<UIItemCommand>(
                tool_command_name(type),
                [this, type]()
                {
                    if (m_gizmo_manager->current_tool_type() == type) {
                        m_gizmo_manager->deactivate_current_tool();
                    } else {
                        m_gizmo_manager->activate_tool(type);
                    }
                },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, tool_key_code(type)}
                        },
                    .enabled = [tool_gizmo = tool_gizmo.get()]() { return tool_gizmo->enabled(); },
                    .checked = [this, type]()
                    { return type == m_gizmo_manager->current_tool_type(); }
                }
            )
        );
    }

    m_top_bar->register_context_menus(m_gizmo_manager->data_factory(), m_scene_presenter.get());

    MenuBuilder menu_builder(menu_manager(), command_binding_manager());

    std::map<MenuItemName, Yoga::Menu*> context_menus = {
        {MenuItemName::BedContextMenu, m_bed_menu},
        {MenuItemName::ObjectContextMenu, m_object_menu},
        {MenuItemName::MultiObjectsContextMenu, m_multi_objects_menu},
        {MenuItemName::VolumeContextMenu, m_volume_menu},
        {MenuItemName::SvgOrTextContextMenu, m_svg_or_text_menu}
    };

    for (auto [item_name, menu] : context_menus) {
        if (App::MenuItem* menu_item = menu_manager().menu_item(item_name)) {
            menu_builder.add_menu_items(menu, menu_item);
        }
    }
}

void PlaterRenderModule::bind_commands()
{
    m_command_binding_manager.bind_tb_item(CommandName::ImportGeometry, m_toolbar_add);
    m_command_binding_manager.bind_tb_item(CommandName::DeleteSelected, m_toolbar_delete);
    m_command_binding_manager.bind_tb_item(CommandName::AddInstance, m_toolbar_add_instance);
    m_command_binding_manager.bind_tb_item(CommandName::SwitchToPreview, m_toolbar_preview_switch);

    for (const Scene::GizmoManager::IToolGizmoPtr& tool_gizmo : m_gizmo_manager->tool_gizmos()) {
        const Scene::ToolType type{tool_gizmo->type()};
        m_command_binding_manager.bind_tb_item(
            tool_command_name(type),
            get_toolbar_button(type)
        );
    }
}

void PlaterRenderModule::init_scene_layout()
{
    ASSERT(m_render_module_navigator);

    m_preferences_dialog = std::make_unique<PreferencesDialog>(
        AppServices::instance().app_config_interactor(),
        *m_render_module_navigator
    );

    m_number_entry_dialog = std::make_unique<NumberEntryDialog>(
        *m_render_module_navigator
    );

    m_preset_updater_dialog = std::make_unique<PresetUpdaterDialog>(
        AppServices::instance().preset_updater_controller(),
        *m_render_module_navigator
    );

    m_welcome_dialog = std::make_unique<WelcomeDialog>(m_project_interactor);

    m_invalid_data_dialog = std::make_unique<InvalidDataDialog>(
        m_project_interactor,
        *m_render_module_navigator
    );
    m_crashed_projects_dialog =
        std::make_unique<CrashedProjectsDialog>(m_project_interactor, *m_render_module_navigator);

    // >> This code is same for Plater/PreviewRenderModule
    m_top_bar = std::make_unique<TopBar>(
        &m_project_interactor,
        this,
        *m_render_module_navigator,
        &m_undo_store,
        *m_project_saver,
        &m_plugin_system
    );

    m_object_list = Passthrough(std::make_unique<ObjectListWindow>(&m_project_interactor, true));
    m_object_list->set_gizmo_controller(m_gizmo_manager.get());
    m_object_list->on_config_container_added = [this]()
    {
        m_render_module_navigator->set_opened_dialog(
            &m_sidebar_bed->logical_printer_settings_dialog()
        );
    };

    m_cube_view = Passthrough{std::make_unique<CubeView>()};
    m_sidebar_bed =
        Passthrough(std::make_unique<SidebarBed>(m_project_interactor, *m_render_module_navigator));
    m_sidebar_print = Passthrough(
        std::make_unique<SidebarPrint>(m_project_interactor, *m_render_module_navigator)
    );
    m_sidebar_object = Passthrough(std::make_unique<SidebarObject>(m_project_interactor));
    m_gizmo_manager->add_listener<Scene::IGizmoActiveToolListener>(m_sidebar_object.get());
    m_pop_notification_list_view =
        Passthrough{std::make_unique<PopNotification::PopNotificationListView>(
            AppServices::instance().pop_notification_center().observable_list()
        )};
    m_history = Passthrough(std::make_unique<History>());
    m_history->set_visible(false);

    m_sidebar_action_buttons =
        Passthrough{std::make_unique<SidebarPlaterActionButtons>(
            m_render_module_navigator,
            [this]() { command(Slic3r::App::Platform::CommandName::ImportGeometry).execute(); }
        )};
    m_sidebar_action_buttons->on_init(&m_project_interactor);

    m_layout.reset(new PlaterRenderLayout(
        *m_render_module_navigator,
        m_top_bar.release(),
        m_preferences_dialog.release(),
        m_object_list.release(),
        m_cube_view.release(),
        m_pop_notification_list_view.release(),
        m_sidebar_bed.release(),
        m_sidebar_print.release(),
        m_sidebar_object.release(),
        m_sidebar_action_buttons.release(),
        m_history.release(),
        m_number_entry_dialog.release(),
        m_welcome_dialog.release(),
        m_invalid_data_dialog.release(),
        m_plugin_system.init_dialog().release(),
        m_crashed_projects_dialog.release(),
        m_preset_updater_dialog.release()
    ));
    m_layout->init();

    // init toolbars

    // m_layout->add_toolbar_item_panel(
    // ToolbarID::Bottom,
    // Render::Icon::ToolbarHistory,
    // "Actions History",
    // "Shift + Alt + H",
    // {},
    // m_history.get()
    // );

    m_bed_menu =
        m_top_bar->emplace_back<Yoga::Menu>("bed_context_menu", Yoga::Position::Bottom);
    m_object_menu =
        m_top_bar->emplace_back<Yoga::Menu>("object_context_menu", Yoga::Position::Bottom);
    m_volume_menu =
        m_top_bar->emplace_back<Yoga::Menu>("volume_context_menu", Yoga::Position::Bottom);
    m_multi_objects_menu =
        m_top_bar->emplace_back<Yoga::Menu>("multi_objects_context_menu", Yoga::Position::Bottom);
    m_svg_or_text_menu =
        m_top_bar->emplace_back<Yoga::Menu>("svg_or_text_context_menu", Yoga::Position::Bottom);

    m_toolbar_add = m_layout->add_toolbar_item(ToolbarID::ToolLeft, Render::Icon::AddObject, _u8L("Add..."));

    m_toolbar_delete = m_layout->add_toolbar_item(
        ToolbarID::ToolRight,
        Render::Icon::DeleteBtnIcon,
        _u8L("Delete selection")
    );

    m_toolbar_add_instance = m_layout->add_toolbar_item(
        ToolbarID::ToolRight,
        Render::Icon::RectangleAdd,
        _u8L("Add instance")
    );

    m_toolbar_move = m_layout->add_toolbar_item(
        ToolbarID::ToolRight,
        tool_icon(Scene::ToolType::Translation),
        tool_name(Scene::ToolType::Translation)
    );

    m_toolbar_rotate = m_layout->add_toolbar_item(
        ToolbarID::ToolRight,
        tool_icon(Scene::ToolType::Rotation),
        tool_name(Scene::ToolType::Rotation)
    );

    m_toolbar_scale = m_layout->add_toolbar_item(
        ToolbarID::ToolRight,
        tool_icon(Scene::ToolType::Scale),
        tool_name(Scene::ToolType::Scale)
    );

    m_toolbar_place_on_face = m_layout->add_toolbar_item(
        ToolbarID::ToolRight,
        tool_icon(Scene::ToolType::PlaceOnFace),
        tool_name(Scene::ToolType::PlaceOnFace)
    );

    m_toolbar_arrange = m_layout->add_toolbar_item(
        ToolbarID::ToolLeft,
        tool_icon(Scene::ToolType::ArrangeGizmo),
        tool_name(Scene::ToolType::ArrangeGizmo)
    );

    m_toolbar_simplify = m_layout->add_toolbar_item(
        ToolbarID::ToolRight,
        tool_icon(Scene::ToolType::Simplify),
        tool_name(Scene::ToolType::Simplify)
    );

    m_toolbar_paint_on_supports = m_layout->add_toolbar_item(
        ToolbarID::ToolRight,
        tool_icon(Scene::ToolType::PaintOnSupportsGizmo),
        tool_name(Scene::ToolType::PaintOnSupportsGizmo)
    );

    m_toolbar_paint_on_seams = m_layout->add_toolbar_item(
        ToolbarID::ToolRight,
        tool_icon(Scene::ToolType::PaintOnSeamsGizmo),
        tool_name(Scene::ToolType::PaintOnSeamsGizmo)
    );

    m_toolbar_paint_on_fuzzy_skin = m_layout->add_toolbar_item(
        ToolbarID::ToolRight,
        tool_icon(Scene::ToolType::PaintOnFuzzySkinGizmo),
        tool_name(Scene::ToolType::PaintOnFuzzySkinGizmo)
    );

    m_toolbar_multi_material_painting = m_layout->add_toolbar_item(
        ToolbarID::ToolRight,
        tool_icon(Scene::ToolType::MultiMaterialPaintingGizmo),
        tool_name(Scene::ToolType::MultiMaterialPaintingGizmo)
    );

    m_toolbar_text = m_layout->add_toolbar_item(
        ToolbarID::ToolRight,
        tool_icon(Scene::ToolType::TextGizmo),
        tool_name(Scene::ToolType::TextGizmo)
    );

    m_toolbar_svg = m_layout->add_toolbar_item(
        ToolbarID::ToolRight,
        tool_icon(Scene::ToolType::Svg),
        tool_name(Scene::ToolType::Svg)
    );

    m_toolbar_cut = m_layout->add_toolbar_item(
        ToolbarID::ToolRight,
        tool_icon(Scene::ToolType::CutGizmo),
        tool_name(Scene::ToolType::CutGizmo)
    );

    m_toolbar_measure = m_layout->add_toolbar_item(
        ToolbarID::ToolRight,
        tool_icon(Scene::ToolType::MeasureGizmo),
        tool_name(Scene::ToolType::MeasureGizmo)
    );

    m_toolbar_variable_layer_height = m_layout->add_toolbar_item(
        ToolbarID::ToolRight,
        tool_icon(Scene::ToolType::VariableLayerHeightGizmo),
        tool_name(Scene::ToolType::VariableLayerHeightGizmo)
    );

    m_toolbar_height_range = m_layout->add_toolbar_item(
        ToolbarID::ToolRight,
        tool_icon(Scene::ToolType::HeightRangeGizmo),
        tool_name(Scene::ToolType::HeightRangeGizmo)
    );

    ToolBarButton* plater_button = m_layout->add_toolbar_item_switch(
        ToolbarID::Mode,
        Render::Icon::ObjectIcon,
        _u8L("Prepare"),
        _u8L("Prepare Mode"),
        ToolBarSwitchButton::SwitchPosition::Left
    );
    plater_button->set_checked(true);

    m_toolbar_preview_switch = m_layout->add_toolbar_item_switch(
        ToolbarID::Mode,
        Render::Icon::Preview,
        _u8L("Preview"),
        _u8L("Preview Mode"),
        ToolBarSwitchButton::SwitchPosition::Right
    );

    this->update_toolbar_visibility();
}

void PlaterRenderModule::init_dialog_navigation()
{
    // Init sidebar dialogs
    m_dialog_navigation.insert_dialog(&m_sidebar_bed->logical_printer_settings_dialog());
    m_dialog_navigation.insert_dialog(
        &m_sidebar_bed->printer_add_dialog(),
        &m_sidebar_bed->logical_printer_settings_dialog()
    );
    m_dialog_navigation.insert_dialog(
        &m_sidebar_bed->logical_printer_settings_dialog().printer_advanced_settings_dialog(),
        &m_sidebar_bed->logical_printer_settings_dialog()
    );
    m_dialog_navigation.insert_dialog(
        &m_sidebar_bed->logical_printer_settings_dialog().color_mix_dialog(),
        &m_sidebar_bed->logical_printer_settings_dialog()
    );

    m_dialog_navigation.insert_dialog(&m_sidebar_bed->material_selection_dialog());
    m_dialog_navigation.insert_dialog(
        &m_sidebar_bed->material_selection_dialog().material_settings_dialog(),
        &m_sidebar_bed->material_selection_dialog()
    );

    m_dialog_navigation.insert_dialog(&m_sidebar_print->print_settings_dialog());
    m_dialog_navigation.insert_dialog(m_preferences_dialog.get());
    m_dialog_navigation.insert_dialog(m_number_entry_dialog.get());
    m_dialog_navigation.insert_dialog(m_invalid_data_dialog.get());

    m_dialog_navigation.insert_dialog(&m_sidebar_action_buttons->physical_printer_settings_dialog());
    m_dialog_navigation.insert_dialog(
        &m_sidebar_action_buttons->physical_printer_advanced_settings_dialog(),
        &m_sidebar_action_buttons->physical_printer_settings_dialog()
    );

    // Init gizmos dialogs
    auto init_gizmo_dialog = [this](Scene::ToolType tool_type, GizmoWindowPtr dialog)
    {
        dialog->gizmo_callbacks().close_requested = [this]
        {
            m_gizmo_manager->deactivate_current_tool();
            m_command_binding_manager.update_ui_items();
        };
        dialog->set_title(tool_name(tool_type));
        dialog->set_shortcut(tool_shortcut(tool_type));
        dialog->set_icon(tool_icon(tool_type));

        m_layout->sidebar_stack_layout()->insert_gizmo(tool_type, std::move(dialog));
    };

    init_gizmo_dialog(Scene::ToolType::Translation, m_translation_gizmo->release_ui_window());
    init_gizmo_dialog(Scene::ToolType::Scale, m_scale_gizmo->release_ui_window());
    init_gizmo_dialog(Scene::ToolType::Rotation, m_rotation_gizmo->release_ui_window());
    init_gizmo_dialog(Scene::ToolType::ArrangeGizmo, m_arrange_gizmo->release_ui_window());
    init_gizmo_dialog(Scene::ToolType::MeasureGizmo, m_measure_gizmo->release_ui_window());
    init_gizmo_dialog(
        Scene::ToolType::PaintOnSupportsGizmo,
        m_paint_on_supports_gizmo->release_ui_window()
    );
    init_gizmo_dialog(
        Scene::ToolType::PaintOnSeamsGizmo,
        m_paint_on_seams_gizmo->release_ui_window()
    );
    init_gizmo_dialog(
        Scene::ToolType::PaintOnFuzzySkinGizmo,
        m_paint_on_fuzzy_skin_gizmo->release_ui_window()
    );
    init_gizmo_dialog(
        Scene::ToolType::MultiMaterialPaintingGizmo,
        m_multi_material_painting_gizmo->release_ui_window()
    );
    init_gizmo_dialog(Scene::ToolType::Simplify, m_simplify_gizmo->release_ui_window());
    init_gizmo_dialog(Scene::ToolType::TextGizmo, m_text_gizmo->release_ui_window());
    init_gizmo_dialog(Scene::ToolType::Svg, m_svg_gizmo->release_ui_window());
    init_gizmo_dialog(Scene::ToolType::CutGizmo, m_cut_gizmo->release_ui_window());
    init_gizmo_dialog(
        Scene::ToolType::VariableLayerHeightGizmo,
        m_variable_layer_height_gizmo->release_ui_window()
    );
    init_gizmo_dialog(Scene::ToolType::HeightRangeGizmo, m_height_range_gizmo->release_ui_window());
}

void PlaterRenderModule::update_object_selection()
{
    const Biz::Scene::ObjectSelection& selection =
        m_project_interactor.scene_interactor().object_selection();

    const bool empty_selection = selection.empty();
    m_layout->tool_right_toolbar()->set_visible(!empty_selection);


    update_toolbar_visibility();

    update_current_right_sidebar();
}

void PlaterRenderModule::update_current_right_sidebar()
{
    const Biz::Scene::ObjectSelection& selection =
        m_project_interactor.scene_interactor().object_selection();

    const bool empty_selection = selection.empty();

    const Scene::ToolType tool_type  = m_gizmo_manager->current_tool_type();
    SidebarStackLayout* stack_layout = m_layout->sidebar_stack_layout();

    bool switch_to_gizmo{false};
    if (tool_type != Scene::ToolType::None && stack_layout->contains_gizmo(tool_type)) {
        stack_layout->switch_to_gizmo(tool_type);
        switch_to_gizmo =true;
    } else if (!empty_selection) {
        stack_layout->switch_to_item(SidebarStackLayout::ItemType::Object);
        // upon object selection we are selecting sidebar and closing opened dialogs
        m_render_module_navigator->set_opened_dialog(nullptr);
    } else {
        stack_layout->switch_to_item(SidebarStackLayout::ItemType::Bed);
    }
    m_sidebar_action_buttons->set_visible(!switch_to_gizmo);
}

void PlaterRenderModule::update_toolbar_visibility()
{
    for (const Scene::GizmoManager::IToolGizmoPtr& tool_gizmo : m_gizmo_manager->tool_gizmos()) {
        get_toolbar_button(tool_gizmo->type())->set_visible(tool_gizmo->enabled());
    }

    m_toolbar_add->set_visible(
        m_command_binding_manager.command(CommandName::ImportGeometry).enabled()
    );
    m_toolbar_delete->set_visible(
        m_command_binding_manager.command(CommandName::DeleteSelected).enabled()
    );
    if (m_command_binding_manager.has_command(CommandName::AddInstance)) {
        m_toolbar_add_instance->set_visible(
            m_command_binding_manager.command(CommandName::AddInstance).enabled()
        );
    }
}

Scene::IGizmoController& PlaterRenderModule::gizmo_controller() {
    return *m_gizmo_manager;
}

void PlaterRenderModule::init_scene()
{
#if ENABLED_NODE_LOGGING
    m_scene_presenter->scene().log_nodes();
#endif
}

void PlaterRenderModule::init_gizmos()
{
    // TODO: It shoud be OS dependent by maximal time betweem clic to be double click
    int min_drag_time_span = 500; // [in ms]
    // TODO: Load constant from OS
    int min_drag_offset = 500; // [in um]
    auto drag_detector = std::make_unique<Scene::MouseDragDetector>(min_drag_time_span, min_drag_offset);
    m_gizmo_manager = std::make_unique<Scene::GizmoManager>(
        *m_device,
        *m_scene_presenter,
        m_project_interactor,
        std::move(drag_detector)
    );
    m_undo_store.set_gizmo_controller(m_gizmo_manager.get());
    m_gizmo_manager->add_listener<IGizmoActiveToolListener>(this);
    m_camera_gizmo = &m_gizmo_manager->add_base_gizmo<PlaterCameraGizmo>(m_workbench, m_project_interactor, *m_scene_presenter,
        *m_animation_manager);
    m_gizmo_manager->add_base_gizmo<BedSelectGizmo>(m_project_interactor, *m_scene_presenter);
    ContextMenuGizmo& context_menu_gizmo = m_gizmo_manager->add_base_gizmo<ContextMenuGizmo>(
        m_project_interactor,
        *m_scene_presenter
    );
    context_menu_gizmo.add_listener<IShowContextMenuListener>(this);
    QuickSelectGizmo& quick_select_gizmo = m_gizmo_manager->add_base_gizmo<QuickSelectGizmo>(
        *m_gizmo_manager,
        m_project_interactor.scene_interactor(),
        *m_device,
        *m_scene_presenter,
        m_screen_info
    );
    quick_select_gizmo.add_listener<IHoverChangedListener>(m_scene_presenter.get());
    m_scene_presenter->add_listener<Scene::ICameraUpdateListener>(&quick_select_gizmo);
    m_gizmo_manager->add_base_gizmo<QuickDragGizmo>(
        m_project_interactor,
        *m_scene_presenter
    );
    m_translation_gizmo = &m_gizmo_manager->add_tool_gizmo<TranslationGizmo>(
        *m_device,
        m_gizmo_manager->data_factory(),
        *m_scene_presenter,
        m_project_interactor
    );
    m_rotation_gizmo = &m_gizmo_manager->add_tool_gizmo<RotationGizmo>(
        *m_device,
        m_gizmo_manager->data_factory(),
        *m_scene_presenter,
        m_project_interactor
    );
    m_scale_gizmo = &m_gizmo_manager->add_tool_gizmo<ScaleGizmo>(
        *m_device,
        m_gizmo_manager->data_factory(),
        *m_scene_presenter,
        m_project_interactor
    );
    m_place_on_face_gizmo = &m_gizmo_manager->add_tool_gizmo<PlaceOnFaceGizmo>(
        *m_device,
        *m_scene_presenter,
        m_project_interactor
    );
    m_project_interactor.scene_interactor().add_listener<Biz::Scene::ISceneSelectionChangedListener>(
        m_place_on_face_gizmo
    );
    m_arrange_gizmo = &m_gizmo_manager->add_tool_gizmo<ArrangeGizmo>(
        m_project_interactor.arrange_interactor(),
        *m_device,
        *m_scene_presenter,
        m_gizmo_manager->data_factory(),
        m_project_interactor,
        m_workbench
    );
    m_simplify_gizmo = &m_gizmo_manager->add_tool_gizmo<SimplifyGizmo>(
        *m_device,
        *m_scene_presenter,
        m_project_interactor,
        *m_gizmo_manager,
        std::make_unique<SimplifyNotification>(
            m_project_interactor,
            *m_gizmo_manager,
            AppServices::instance().pop_notification_center()
        )
    );
    m_paint_on_supports_gizmo = &m_gizmo_manager->add_tool_gizmo<PaintOnSupportsGizmo>(
        *m_device,
        m_gizmo_manager->data_factory(),
        m_project_interactor,
        *m_scene_presenter
    );
    m_paint_on_seams_gizmo = &m_gizmo_manager->add_tool_gizmo<PaintOnSeamsGizmo>(
        *m_device,
        m_gizmo_manager->data_factory(),
        m_project_interactor,
        *m_scene_presenter
    );
    m_paint_on_fuzzy_skin_gizmo = &m_gizmo_manager->add_tool_gizmo<PaintOnFuzzySkinGizmo>(
        *m_device,
        m_gizmo_manager->data_factory(),
        m_project_interactor,
        *m_scene_presenter
    );
    m_multi_material_painting_gizmo = &m_gizmo_manager->add_tool_gizmo<MultiMaterialPaintingGizmo>(
        *m_device,
        m_gizmo_manager->data_factory(),
        m_project_interactor,
        *m_scene_presenter
    );
    m_text_gizmo = &m_gizmo_manager->add_tool_gizmo<TextGizmo>(
        *m_device,
        *m_scene_presenter,
        m_project_interactor,
        *m_font_manager,
        *m_gizmo_manager
    );
    m_svg_gizmo = &m_gizmo_manager->add_tool_gizmo<SvgGizmo>(
        *m_scene_presenter,
        m_project_interactor,
        *m_gizmo_manager
    );
    m_measure_gizmo = &m_gizmo_manager->add_tool_gizmo<MeasureGizmo>(
        *m_device,
        m_project_interactor,
        *m_scene_presenter
    );
    m_project_interactor.scene_interactor()
        .add_listener<Biz::Scene::ISceneSelectionChangedListener>(m_measure_gizmo);
    m_cut_gizmo = &m_gizmo_manager->add_tool_gizmo<CutGizmo>(
        *m_device,
        m_gizmo_manager->data_factory(),
        *m_scene_presenter,
        &m_project_interactor
    );
    m_project_interactor.scene_interactor()
        .add_listener<Biz::Scene::ISceneSelectionChangedListener>(m_cut_gizmo);
    m_variable_layer_height_gizmo = &m_gizmo_manager->add_tool_gizmo<VariableLayerHeightGizmo>(
        *m_device,
        m_project_interactor,
        *m_scene_presenter
    );
    m_height_range_gizmo = &m_gizmo_manager->add_tool_gizmo<HeightRangeGizmo>(
        *m_device,
        m_project_interactor,
        *m_scene_presenter
    );
    m_project_interactor.scene_interactor().add_listener<ISceneSelectionChangedListener>(
        m_height_range_gizmo
    );

    m_command_binding_manager.set_gizmos_command_registry(&m_gizmo_manager->command_registry());
}

ToolBarButton* PlaterRenderModule::get_toolbar_button(Scene::ToolType tool_type) const
{
    switch (tool_type) {
    case Scene::ToolType::Translation:
        return m_toolbar_move;
    case Scene::ToolType::Rotation:
        return m_toolbar_rotate;
    case Scene::ToolType::Scale:
        return m_toolbar_scale;
    case Scene::ToolType::PlaceOnFace:
        return m_toolbar_place_on_face;
    case Scene::ToolType::Simplify:
        return m_toolbar_simplify;
    case Scene::ToolType::ArrangeGizmo:
        return m_toolbar_arrange;
    case Scene::ToolType::PaintOnSupportsGizmo:
        return m_toolbar_paint_on_supports;
    case Scene::ToolType::PaintOnSeamsGizmo:
        return m_toolbar_paint_on_seams;
    case Scene::ToolType::PaintOnFuzzySkinGizmo:
        return m_toolbar_paint_on_fuzzy_skin;
    case Scene::ToolType::MultiMaterialPaintingGizmo:
        return m_toolbar_multi_material_painting;
    case Scene::ToolType::TextGizmo:
        return m_toolbar_text;
    case Scene::ToolType::Svg:
        return m_toolbar_svg;
    case Scene::ToolType::MeasureGizmo:
        return m_toolbar_measure;
    case Scene::ToolType::CutGizmo:
        return m_toolbar_cut;
    case Scene::ToolType::VariableLayerHeightGizmo:
        return m_toolbar_variable_layer_height;
    case Scene::ToolType::HeightRangeGizmo:
        return m_toolbar_height_range;
    case Scene::ToolType::None:
        return nullptr;
    default:
        PANIC("Unknown gizmo!");
    }
}

void PlaterRenderModule::active_tool_changed(Scene::IToolGizmo* active_tool)
{
    const std::set<Scene::ToolType> tools_with_visible_selection_box{
        Scene::ToolType::ArrangeGizmo,
        Scene::ToolType::Translation,
        Scene::ToolType::Rotation,
        Scene::ToolType::Scale
    };

    update_current_right_sidebar();
    m_command_binding_manager.update_ui_items();
    const bool selection_box_visible{
        active_tool == nullptr || tools_with_visible_selection_box.contains(active_tool->type())
    };
    m_scene_presenter->set_selection_bounding_box_visible(selection_box_visible);
    m_command_binding_manager.update_ui_items();
}

void PlaterRenderModule::set_navigator(Navigator* navigator)
{
    m_render_module_navigator = navigator;
}

void PlaterRenderModule::on_status_cache_status_code_changed(const Domain::SlicingId id)
{
    // request redraw
    request_render();
}

void PlaterRenderModule::on_show_context_menu(ContextMenuType type, Domain::Vec2f mouse_position)
{
    Domain::Vec2f pos = mouse_position - m_top_bar->get_global_pos();

    Yoga::Menu* menu{nullptr};

    switch (type) {
    case ContextMenuType ::Bed:
        menu = m_bed_menu;
        break;
    case ContextMenuType ::Object:
        menu = m_object_menu;
        break;
    case ContextMenuType ::MultiObjects:
        menu = m_multi_objects_menu;
        break;
    case ContextMenuType ::Volume:
        menu = m_volume_menu;
        break;
    case ContextMenuType ::SvgOrText:
        menu = m_svg_or_text_menu;
        break;
    default:
        break;
    }

    if (menu) {
        menu->open(pos);
        request_render();
    }
}

void PlaterRenderModule::set_sidebars_visible(bool visible)
{
    m_layout->set_sidebars_visible(visible);

    // request redraw
    request_render();
}

const std::optional<Platform::CameraSynchData>& PlaterRenderModule::camera_synch_data() const
{
    return m_scene_presenter->camera_synch_data();
}

void PlaterRenderModule::set_camera_synch_data(const Platform::CameraSynchData& data)
{
    if (m_scene_presenter == nullptr)
        return;

    synchronize_camera(
        data,
        m_scene_presenter->scene().camera(),
        m_scene_presenter->scene().camera_trackball()
    );
    m_scene_presenter->set_camera_synch_data(data);
}

void PlaterRenderModule::render_scene(Render::CommandBuffer& cmd_buffer)
{
    ZoneScoped;

    Render::ScopedDebugGroup event_imgui_render("Plater Render", cmd_buffer);
    m_device->load_state();

    cmd_buffer.set_viewport(Render::Rect::from(0, 0, m_screen_info));
    cmd_buffer.set_clear_values({0.61f, 0.61f, 0.61f, 1.00f});
    cmd_buffer.clear_buffers(true, true);

    m_scene_presenter->render_scene(cmd_buffer);

    m_gizmo_manager->render_scene(cmd_buffer);

    cmd_buffer.submit();
}

void PlaterRenderModule::render_imgui(Render::CommandBuffer& cmd_buffer)
{
    ZoneScoped;

    if (!m_scene_presenter->project_ready())
        return;

    m_thumbnail_image_generator->handle_enqueued_requests();
    m_thumbnail_store_updater->update(
        *m_device,
        [this](const BedThumbnailTextures& textures)
        { m_object_list->set_bed_instance_icons(textures); }
    );

    m_cube_view->set_camera_data(
        m_scene_presenter->scene().camera(),
        m_scene_presenter->scene().camera_trackball()
    );

    m_layout->render();

    if (m_cube_view->require_render())
        request_render();

    m_scene_presenter->render_imgui(m_screen_info);
    m_gizmo_manager->render_imgui();

#if ENABLED_SHORTCUTS_LIST
    imgui_shortcuts_list(m_command_registry);
#endif

#if ENABLED_DEBUG_OUTLINE
    if (ImGui::Begin("Outline", nullptr)) {
        imgui_scenegraph_node_info(m_scene_presenter->scene().root());
    }
    ImGui::End();
#endif // ENABLED_DEBUG_OUTLINE

#if ENABLED_DEBUG_CAMERA
    render_imgui_debug_camera(
        m_scene_presenter->scene().camera(),
        m_scene_presenter->scene().camera_trackball()
    );
#endif // ENABLED_DEBUG_CAMERA
    Scene::render_imgui_graphics_settings_debug_window(
        m_project_interactor.selected_project(),
        *m_device,
        *m_scene_presenter,
        *m_imgui_render
    );
}

void PlaterRenderModule::render_object_hud(
    const Scene::Node& n,
    const Eigen::AlignedBox<float, 2>& screen_bounding_box
)
{
    std::string node_name = "##node_hud_" + std::to_string(reinterpret_cast<size_t>(&n));

    ImGui::SetNextWindowPos({screen_bounding_box.max().x(), screen_bounding_box.min().y()});
    if (ImGui::Begin(
            node_name.c_str(),
            nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground
        ))
    {
        if (ImGui::SmallButton("Foc"))
            m_scene_presenter->scene().camera_trackball().set_target(Vec3d::Zero());
    }
    ImGui::End();
}

void PlaterRenderModule::on_scene_mouse_event(const Platform::MouseEvent& e)
{
    ZoneScoped;

    m_gizmo_manager->on_scene_mouse_event(e, m_screen_info);
    m_scene_presenter->update_sinking_contours_visibility(e, m_screen_info);

    // Close menus on click inside the scene
    if (!ImGui::GetIO().WantCaptureMouseUnlessPopupClose
        && e.button() == Platform::MouseButton::Left
        && e.type() == Platform::MouseEvent::Type::ButtonUp)
    {
        close_all_menus();
    }
}

void PlaterRenderModule::on_scene_keyboard_event(const Platform::KeyboardEvent& e)
{
    if (!m_render_module_navigator->is_any_modal_dialog_opened()
        && !m_gizmo_manager->on_scene_keyboard_event(e))
    {
        Platform::AbstractRenderModule::on_scene_keyboard_event(e);
    }
}

void PlaterRenderModule::on_activated()
{
    if (m_scene_presenter != nullptr) {
        m_scene_presenter->scene().set_lights(App::global_lighting());
        m_scene_presenter->update_bed_instances();
    }

    if (m_object_list.get() != nullptr) {
        // object list icons may have been updated while the preview was active
        m_object_list->set_bed_instance_icons(m_thumbnail_store->projects.selected().thumbnails);
    }

    if (m_layout) {
        m_layout->load_column_sizes();
    }
}

void PlaterRenderModule::on_deactivated()
{
    App::set_global_lighting(m_scene_presenter->scene().lights());

    Platform::CameraSynchData data;
    m_scene_presenter->scene().camera().update_synch_data(data);
    m_scene_presenter->scene().camera_trackball().update_synch_data(data);
    m_scene_presenter->set_camera_synch_data(data);

    m_layout->save_column_sizes();
}

void PlaterRenderModule::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
)
{
    this->update_object_selection();
    this->update_toolbar_visibility();
}

void PlaterRenderModule::on_screen_resized()
{
    // m_scene->camera().set_viewport(Render::Rect::from(0, 0, m_screen_info));
    auto viewport = Render::Rect::from(0, 0, m_screen_info);
    m_scene_presenter->screen_resized(viewport);
    Yoga::Object::set_scale_factor(m_screen_info.scale());
    m_imgui_render->set_scale_factor(m_screen_info.scale());
    if (m_layout) {
        m_layout->set_size_info_from_screen(m_screen_info);
    }
}

void PlaterRenderModule::on_selected_project_changed(size_t index)
{
    this->update_object_selection();
    this->update_toolbar_visibility();
    m_scene_presenter->update_bed_instances();
    m_animation_manager->terminate_all();
}

void PlaterRenderModule::on_preset_selection_changed(
    SelectionId project_id,
    SelectionId config_container_id,
    Biz::Preset::PresetItemType type
)
{
    this->update_toolbar_visibility();
}

void PlaterRenderModule::close_all_menus()
{
    for (Yoga::Menu* menu :
         {m_bed_menu, m_object_menu, m_volume_menu, m_multi_objects_menu, m_svg_or_text_menu})
    {
        menu->close();
    }
}

void PlaterRenderModule::on_command_executed()
{
    // Close menus on some command execution
    close_all_menus();
}

} // namespace Slic3r::App::Plater
