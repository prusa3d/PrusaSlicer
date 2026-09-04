#include "Slic3r/App/Preview/PreviewRenderModule.hpp"

#include "Slic3r/App/Preview/PreviewCameraGizmo.hpp"
#include "Slic3r/App/Preview/SidebarAutoReslice.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/CommandBuffer.hpp"
#include "Slic3r/App/Render/ScopedDebugGroup.hpp"
#include "Slic3r/App/Preview/Types.hpp"
#include "Slic3r/App/Render/ImguiRender.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Preview/SidebarPreviewActionButtons.hpp"
#include "Slic3r/App/ToolBar/ToolBar.hpp"
#include "Slic3r/App/ToolBar/ToolBarButton.hpp"
#include "Slic3r/App/Scene/LightingHelper.hpp"
#include "Slic3r/App/LightSetting.hpp"
#include "Slic3r/App/ThumbnailStore.hpp"
#include "Slic3r/App/ThumbnailStoreUpdater.hpp"
#include "Slic3r/App/Platform/AnimationManager.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/Scene/CameraHelper.hpp"
#include "Slic3r/App/RenderModuleHelper.hpp"
#include "Slic3r/App/SidebarStackLayout.hpp"
#include "Slic3r/App/ColorMix/ColorMixDialog.hpp"
#include "Slic3r/App/LogicalPrinterSettingsDialog.hpp"
#include "Slic3r/App/PhysicalPrinterSettingsDialog.hpp"
#include "Slic3r/App/PhysicalPrinterAdvancedSettingsDialog.hpp"
#include "Slic3r/App/PrinterAdvancedSettingsDialog.hpp"
#include "Slic3r/App/PrinterAddDialog.hpp"
#include "Slic3r/App/PrintSettingsDialog.hpp"
#include "Slic3r/App/MaterialSelectionDialog.hpp"
#include "Slic3r/App/MaterialSettingsDialog.hpp"
#include "Slic3r/App/InvalidDataDialog.hpp"
#include "Slic3r/App/UIItemCommand.hpp"
#include "Slic3r/App/AppConfig.hpp"

#include "Slic3r/Domain/TriangleMesh.hpp"

#include <Slic3r/App/libvgcode/FdmViewerInputData.hpp>

#include <Slic3r/Biz/libpgcode/Processor.hpp>
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <LibBGCode/core/core.hpp>
#include <LibBGCode/convert/convert.hpp>

#include <Slic3r/App/libvgcode/GCodeNodeTag.hpp>

#include <boost/nowide/cstdio.hpp>
#include <boost/filesystem/operations.hpp>

#define ENABLED_DEBUG_VIEWER 0
#define ENABLED_DEBUG_VIEWER_MODE 0

using namespace Slic3r::Biz;
using namespace Slic3r::Biz::libpgcode;
using namespace Slic3r::App::libvgcode;
using namespace Slic3r::App::Yoga;

using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec4d;

namespace Slic3r::App::Preview {

using Domain::TriangleMesh;
namespace CustomGCode = Domain::CustomGCode;

using CommandName          = Platform::CommandName;
using FuncCommandExtraOpts = Platform::FuncCommandExtraOpts;

void PreviewRenderModule::render_scene(Render::CommandBuffer& cmd_buffer)
{
    Render::ScopedDebugGroup event_imgui_render("Preview Render", cmd_buffer);
    m_device->load_state();

    cmd_buffer.set_viewport(Render::Rect::from(0, 0, m_screen_info));
    cmd_buffer.set_clear_values({0.61f, 0.61f, 0.61f, 1.00f});
    cmd_buffer.clear_buffers(true, true);

    m_viewer->render_scene();
    m_scene_presenter->render_scene(cmd_buffer);

    cmd_buffer.submit();
}

#if ENABLED_DEBUG_VIEWER
static void render_imgui_debug_viewer(Wrapper& viewer)
{
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
    if (ImGui::Begin(
            "Preview debug",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoFocusOnAppearing
        ))
    {
        if (ImGui::Button("COG marker scale factor", {-1.0f, 0.0f}))
            viewer.set_scale_factor_popup_type(Biz::libpgcode::OptionType::CenterOfGravity);
        if (ImGui::Button("Tool marker scale factor", {-1.0f, 0.0f}))
            viewer.set_scale_factor_popup_type(Biz::libpgcode::OptionType::ToolMarker);

        ImGui::Separator();

        if (ImGui::Button("Travels radius", {-1.0f, 0.0f}))
            viewer.set_radius_popup_type(Biz::libpgcode::MoveType::Travel);
        if (ImGui::Button("Wipes radius", {-1.0f, 0.0f}))
            viewer.set_radius_popup_type(Biz::libpgcode::MoveType::Wipe);

        ImGui::Separator();

        if (ImGui::Button("Extrusion roles color", {-1.0f, 0.0f}))
            viewer.set_extrusion_roles_colors_popup_visible(true);
        if (ImGui::Button("Options color", {-1.0f, 0.0f}))
            viewer.set_options_colors_popup_visible(true);

        ImGui::Separator();

        if (ImGui::Button("Height range colors", {-1.0f, 0.0f}))
            viewer.set_range_colors_popup_type(libvgcode::ViewType::Height);
        if (ImGui::Button("Width range colors", {-1.0f, 0.0f}))
            viewer.set_range_colors_popup_type(libvgcode::ViewType::Width);
        if (ImGui::Button("Speed range colors", {-1.0f, 0.0f}))
            viewer.set_range_colors_popup_type(libvgcode::ViewType::Speed);
        if (ImGui::Button("Actual speed range colors", {-1.0f, 0.0f}))
            viewer.set_range_colors_popup_type(libvgcode::ViewType::ActualSpeed);
        if (ImGui::Button("Fan speed range colors", {-1.0f, 0.0f}))
            viewer.set_range_colors_popup_type(libvgcode::ViewType::FanSpeed);
        if (ImGui::Button("Temperature range colors", {-1.0f, 0.0f}))
            viewer.set_range_colors_popup_type(libvgcode::ViewType::Temperature);
        if (ImGui::Button("Volumetric flow rate range colors", {-1.0f, 0.0f}))
            viewer.set_range_colors_popup_type(libvgcode::ViewType::VolumetricFlowRate);
        if (ImGui::Button("Layer time linear range colors", {-1.0f, 0.0f}))
            viewer.set_range_colors_popup_type(libvgcode::ViewType::LayerTimeLinear);
        if (ImGui::Button("Layer time logarithmic range colors", {0.0f, 0.0f}))
            viewer.set_range_colors_popup_type(libvgcode::ViewType::LayerTimeLogarithmic);
    }
    ImGui::End();
}
#endif // ENABLED_DEBUG_VIEWER

#if ENABLED_DEBUG_VIEWER_MODE
static void render_imgui_debug_viewer_mode(Wrapper& viewer)
{
    ImGui::SetNextWindowPos(
        {ImGui::GetMainViewport()->GetCenter().x, 0.0f},
        ImGuiCond_Always,
        {0.5f, 0.0f}
    );
    if (ImGui::Begin(
            "Type debug",
            nullptr,
            ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_AlwaysAutoResize
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoBackground
        ))
    {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Mode:");
        ImGui::SameLine();

        const char* items[]     = {"EditorGCode", "GCodeViewer"};
        static int item_current = 0;
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Combo("##modes", &item_current, items, IM_ARRAYSIZE(items))) {
            viewer.reset();
            viewer.set_mode(
                (item_current == 0) ? FdmViewerWrapperMode::EditorGCode :
                                      FdmViewerWrapperMode::GCodeViewer
            );
        }

        ImGui::SameLine();
        ImGui::Text("Current:");
        std::string mode_txt;
        switch (viewer.mode()) {
        case FdmViewerWrapperMode::EditorGCode: {
            mode_txt = "EditorGCode";
            break;
        }
        case FdmViewerWrapperMode::EditorPreGCode: {
            mode_txt = "EditorPreGCode";
            break;
        }
        case FdmViewerWrapperMode::EditorSLA: {
            mode_txt = "EditorSLA";
            break;
        }
        case FdmViewerWrapperMode::GCodeViewer: {
            mode_txt = "GCodeViewer";
            break;
        }
        default: {
            mode_txt = "Error";
            break;
        }
        }
        ImGui::SameLine();
        ImGui::Text("%s", mode_txt.c_str());
    }
    ImGui::End();
}
#endif // ENABLED_DEBUG_VIEWER_MODE

void PreviewRenderModule::render_imgui(Render::CommandBuffer& cmd_buffer)
{
    if (m_thumbnail_image_generator->initialized()) {
        m_thumbnail_image_generator->handle_enqueued_requests();
    }

    m_thumbnail_store_updater->update(
        *m_device,
        [this](const Plater::BedThumbnailTextures& textures) { update_bed_instances(); }
    );

    Domain::PrinterTechnology printer_technology =
        m_project_interactor.selected_config_container().print_technology();
    bool gcode_window_enabled = m_fdm_viewer.mode() != FdmViewerWrapperMode::EditorPreGCode
        && m_fdm_viewer.has_data()
        && printer_technology == Domain::PrinterTechnology::FFF;

    if (m_layout) {
        m_button_gcode_inspect->set_visible(gcode_window_enabled);
        if (m_button_gcode_inspect->checked() && !gcode_window_enabled) {
            m_button_gcode_inspect->set_checked(false);
        }
        m_legend->set_visible(gcode_window_enabled);
        m_slider_layers->set_visible(m_fdm_viewer.has_data());
        m_sla_slider_layers->set_visible(m_sla_viewer.has_data());
        m_slider_gcode->set_visible(m_fdm_viewer.has_data());
    }

    m_cube_view->set_camera_data(
        m_scene_presenter->scene().camera(),
        m_scene_presenter->scene().camera_trackball()
    );

    m_layout->render();

    if (m_cube_view->require_render())
        request_render();

    if (gcode_window_enabled != m_fdm_viewer.tool_marker_enabled()) {
        m_fdm_viewer.set_tool_marker_enabled(gcode_window_enabled);
        update_scene_aabb();
    }

    m_scene_presenter->render_imgui(m_screen_info);

#if ENABLED_SHORTCUTS_LIST
    imgui_shortcuts_list(m_command_registry);
#endif

#if ENABLED_DEBUG_OUTLINE
    if (ImGui::Begin("Outline", nullptr))
        imgui_scenegraph_node_info(m_scene_presenter->scene().root());
    ImGui::End();
#endif // ENABLED_DEBUG_OUTLINE

#if ENABLED_DEBUG_VIEWER
    render_imgui_debug_viewer(m_fdm_viewer);
#endif // ENABLED_DEBUG_VIEWER
#if ENABLED_DEBUG_VIEWER_MODE
    render_imgui_debug_viewer_mode(m_fdm_viewer);
#endif // ENABLED_DEBUG_VIEWER_MODE
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

void PreviewRenderModule::on_scene_mouse_event(const Platform::MouseEvent& e)
{
    m_gizmo_manager->on_scene_mouse_event(e, m_screen_info);
}

void PreviewRenderModule::on_scene_keyboard_event(const Platform::KeyboardEvent& e)
{
    if (!m_render_module_navigator->is_any_modal_dialog_opened()
        && !m_gizmo_manager->on_scene_keyboard_event(e))
    {
        Platform::AbstractRenderModule::on_scene_keyboard_event(e);
    }
}

void PreviewRenderModule::set_navigator(Navigator* navigator)
{
    m_render_module_navigator = navigator;
}

void PreviewRenderModule::on_selected_bed_instances_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::BedSelection& selection
)
{
    DEBUG_ASSERT(m_project_interactor.selected_project_id() == project_id);

    const Domain::BedRef bed_instance{selection.last_selected_bed()};
    const Domain::Project& project{m_project_interactor.selected_project()};
    const Domain::SelectionId config_container_id{bed_instance.config_container_id};
    const Domain::SelectionId bed_instance_id{bed_instance.instance_id};
    const Domain::ConfigContainer* cc{project.find_config_container(config_container_id)};

    DEBUG_ASSERT(cc != nullptr);

    update_viewer();

    if (cc->print_technology() == Domain::PrinterTechnology::SLA) {
        update_sla_viewer_data({project_id, bed_instance_id});
    } else {
        update_fdm_viewer_data({project_id, bed_instance_id});
    }

    update_bed_instances();
    m_scene_presenter->update_shells_visibility();
    m_scene_presenter->center_camera_on_selected_bed(true);

    m_object_list->update_sliced_info();

    if (m_active && m_sidebar_auto_reslice->is_enabled()) {
        m_project_interactor.slicing_interactor().enable_auto_slicing(
            m_project_interactor.selected_bed_slicing_id()
        );
    }

    request_render();
}

void PreviewRenderModule::on_status_cache_status_code_changed(const Domain::SlicingId id)
{
    /*    if (m_project_interactor.selected_project_id() == id.project_id && m_viewer->has_data()) {
            const std::optional<Biz::Slicing::Status> status {
                m_project_interactor.status_cache().get_status(id) };
            if (status && status == Biz::Slicing::Status::Modified)
                m_viewer->reset();
        }*/
    m_object_list->update_sliced_info();

    const auto status{m_project_interactor.status_cache().get_status(id)};
    if (status && status->code == Slicing::StatusCode::Modified) {
        m_fdm_viewer.set_mode(FdmViewerWrapperMode::EditorPreGCode);
    }

    // request redraw
    request_render();
}

void PreviewRenderModule::on_selected_project_changed(size_t project_id)
{
    m_scene_presenter->set_model_geometry_provider(m_shared_model_geometry_provider->shared_model_geometry_provider());

    update_bed_instances();
    update_shells();
    update_viewer();

    const Biz::Scene::BedSelection& bed_selection =
        m_project_interactor.scene_interactor().bed_selection();

    if (m_viewer == &m_fdm_viewer && !bed_selection.empty()) {
        update_fdm_viewer_data(m_project_interactor.selected_bed_slicing_id());
    } else if (m_viewer == &m_sla_viewer && !bed_selection.empty()) {
        update_sla_viewer_data(m_project_interactor.selected_bed_slicing_id());
    }
}

void PreviewRenderModule::on_bed_instance_updated(Domain::SelectionId project_id, const Domain::BedRefs& instances)
{
    update_viewer();
}

void PreviewRenderModule::set_sidebars_visible(bool hide)
{
    m_layout->set_sidebars_visible(hide);
    // request redraw
    request_render();
}

const std::optional<Platform::CameraSynchData>& PreviewRenderModule::camera_synch_data() const
{
    return m_scene_presenter->camera_synch_data();
}

void PreviewRenderModule::set_camera_synch_data(const Platform::CameraSynchData& data)
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

void PreviewRenderModule::set_opened_dialog(Yoga::Dialog* opened_dialog)
{
    m_dialog_navigation.open_dialog(opened_dialog);
}

void PreviewRenderModule::set_modal_dialog(ModalDialog modal_dialog)
{
    auto handle_dialog = [&](Yoga::Dialog* dialog, ModalDialog modal){
        if (modal_dialog == modal) {
            dialog->open();
        } else {
            dialog->close();
        }
    };

    handle_dialog(m_preferences_dialog.get(), ModalDialog::Preferences);
    handle_dialog(m_crashed_projects_dialog.get(), ModalDialog::CrashedProjects);
    handle_dialog(m_preset_updater_dialog.get(), ModalDialog::PresetUpdater);

    request_render();
}

void PreviewRenderModule::open_invalid_data_dialog()
{
    if (m_invalid_data_dialog.get()) {
        set_opened_dialog(m_invalid_data_dialog.get());
    }
}

void PreviewRenderModule::set_object_list_collapsed(bool collapsed)
{
    if (m_object_list.get()) {
        m_object_list->set_collapsed(collapsed);
    }
}

void PreviewRenderModule::on_init(
    Render::Device& device,
    Render::ImguiRender& imgui_render,
    Platform::AnimationManager& animation_manager
)
{
    AbstractRenderModule::on_init(device, imgui_render, animation_manager);
    Yoga::Item::set_imgui_render(
        &imgui_render
    ); // Todo: move this somewhere where it is invoked once
    Yoga::Item::set_theme(&AppServices::instance().theme());
    m_scene_presenter =
        std::make_unique<PreviewScenePresenter>(m_workbench, m_project_interactor, *m_device, *m_animation_manager);

    m_project_interactor.scene_interactor().add_listener<Biz::ISelectedBedInstancesChangedListener>(
        this
    );
    m_project_interactor.fdm_result_cache().add_listener<Biz::IFDMResultCacheChangedListener>(this);
    m_project_interactor.sla_result_cache().add_listener<Biz::ISLAResultCacheChangedListener>(this);
    m_project_interactor.sla_object_cache().add_listener<Biz::ISLAObjectCacheChangedListener>(this);
    m_project_interactor.status_cache().add_listener<Biz::IStatusCacheChangedListener>(this);
    m_project_interactor.add_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.scene_interactor().add_listener<ISceneBedInstanceChangedListener>(this);

    m_project_interactor.status_cache().add_listener<Biz::IStatusCacheChangedListener>(
        &m_command_binding_manager
    );
    m_project_interactor.user_account_interactor()
        .add_listener<Biz::UserAccount::IUserAccountListener>(&m_command_binding_manager);
    m_project_interactor.scene_interactor().add_listener<ISelectedBedInstancesChangedListener>(
        &m_command_binding_manager
    );
    m_project_interactor.removable_drive_service().add_status_listener(&m_command_binding_manager);

    m_menu_manager
        // Menu items
        .register_menu_item(
            {MenuItemName::JumpToValue},
            std::make_unique<UIItemCommand>(
                "slider-layers-jump-to_value",
                [this]() { m_viewer->slider_layers_jump_to_value(); },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Shift),
                        Platform::KeyCode::G
                    }},
                    .enabled           = [this]() { return m_viewer->has_data(); }
                }
            )
        );

    init_gizmos();
    init_viewers(device);
    init_scene_layout();
    init_dialog_navigation();

    m_scene_presenter->scene().set_lights(Slic3r::App::global_lighting());
}

void PreviewRenderModule::on_activated()
{
    m_active = true;
    if (m_scene_presenter != nullptr)
        m_scene_presenter->scene().set_lights(App::global_lighting());

    m_scene_presenter->set_model_geometry_provider(m_shared_model_geometry_provider->shared_model_geometry_provider());

    update_bed_instances();
    update_shells();
    update_viewer();
    update_scene_aabb();

    if (m_sidebar_auto_reslice->is_enabled()) {
        m_project_interactor.slicing_interactor().enable_auto_slicing(
            m_project_interactor.selected_bed_slicing_id()
        );
    }

    m_layout->load_column_sizes();
}

void PreviewRenderModule::on_deactivated()
{
    m_active = false;
    Slic3r::App::set_global_lighting(m_scene_presenter->scene().lights());
    m_project_interactor.slicing_interactor().disable_auto_slicing();

    // update the camera synch data only if the preview was already synchronized with the plater
    if (m_scene_presenter->camera_synch_data().has_value()) {
        Platform::CameraSynchData data;
        m_scene_presenter->scene().camera().update_synch_data(data);
        m_scene_presenter->scene().camera_trackball().update_synch_data(data);
        m_scene_presenter->set_camera_synch_data(data);
    }

    m_layout->save_column_sizes();
}

void PreviewRenderModule::on_screen_resized()
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

void PreviewRenderModule::register_commands()
{
    // Layers slider specific commands, which should be called for active viewer
    m_command_registry
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-layers-increase-slow",
                [this]() { m_viewer->slider_layers_move_current_thumb(1); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Up}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-layers-decrease-slow",
                [this]() { m_viewer->slider_layers_move_current_thumb(-1); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Down}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-layers-increase-medium",
                [this]() { m_viewer->slider_layers_move_current_thumb(5); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Shift),
                        Platform::KeyCode::Up
                    }}
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-layers-decrease-medium",
                [this]() { m_viewer->slider_layers_move_current_thumb(-5); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Shift),
                        Platform::KeyCode::Down
                    }}
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-layers-increase-fast",
                [this]() { m_viewer->slider_layers_move_current_thumb(10); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::Up
                    }}
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-layers-decrease-fast",
                [this]() { m_viewer->slider_layers_move_current_thumb(-10); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::Down
                    }}
                }
            )
        );

    m_command_registry.register_command(
        std::make_unique<Platform::FuncCommand>(
            CommandName::SwitchToPlater,
            [this]()
            { m_render_module_navigator->navigate_to_module_type(Render::ModuleType::Plater); },
            FuncCommandExtraOpts{
                .keyboard_shortcuts = Platform::KeyboardShortcuts{
                    Platform::KeyboardShortcut{0, Platform::KeyCode::Tab}
                }
            }
        )
    );

    // Toolbar commands
    const std::map<const char*, OptionType> tools{
        {CommandName::ShowTravels, OptionType::Travels},
        {CommandName::ShowWipes, OptionType::Wipes},
        {CommandName::ShowRetractions, OptionType::Retractions},
        {CommandName::ShowUnretractions, OptionType::Unretractions},
        {CommandName::ShowSeams, OptionType::Seams},
        {CommandName::ShowToolChanges, OptionType::ToolMarker},
        {CommandName::ShowColorChanges, OptionType::ColorChanges},
        {CommandName::ShowPausePrints, OptionType::PausePrints},
        {CommandName::ShowCustomGCodes, OptionType::CustomGCodes},
        {CommandName::ShowCenterOfGravity, OptionType::CenterOfGravity},
        {CommandName::ShowToolMarker, OptionType::ToolMarker},
    };
    for (const auto& [cmd_name, option_type] : tools) {
        m_command_registry.register_command(
            std::make_unique<Platform::FuncCommand>(
                cmd_name,
                [this, option_type]()
                {
                    m_fdm_viewer.toggle_option_visibility(option_type);
                    if (option_type == OptionType::ToolMarker) {
                        update_scene_aabb();
                    }
                }
            )
        );
    }
    m_command_registry.register_command(
        std::make_unique<Platform::FuncCommand>(
            CommandName::ShowShell,
            [this]() { m_scene_presenter->toggle_shells_visibility(); }
        )
    );

    m_command_registry.register_command(
        std::make_unique<UIItemCommand>(
            CommandName::SwitchToInspect,
            [this]() {},
            UIItemCommandExtraOpts{
                .checked =
                    [this]()
                {
                    return m_layout->sidebar_stack_layout()->is_current_item(
                        SidebarStackLayout::ItemType::GCode
                    );
                },
                .checked_changed = [this](bool checked) { update_current_right_sidebar(); }
            }
        )
    );
}

void PreviewRenderModule::bind_commands() {
    m_command_binding_manager.bind_tb_item(CommandName::ShowTravels, m_button_travels);
    m_command_binding_manager.bind_tb_item(CommandName::ShowWipes, m_button_wipes);
    m_command_binding_manager.bind_tb_item(CommandName::ShowRetractions, m_button_retractions);
    m_command_binding_manager.bind_tb_item(CommandName::ShowUnretractions, m_button_unretractions);
    m_command_binding_manager.bind_tb_item(CommandName::ShowSeams, m_button_seams);
    m_command_binding_manager.bind_tb_item(CommandName::ShowToolChanges, m_button_tool_changes);
    m_command_binding_manager.bind_tb_item(CommandName::ShowColorChanges, m_button_color_changes);
    m_command_binding_manager.bind_tb_item(CommandName::ShowPausePrints, m_button_pause_prints);
    m_command_binding_manager.bind_tb_item(CommandName::ShowCustomGCodes, m_button_custom_gcodes);
    m_command_binding_manager.bind_tb_item(CommandName::ShowCenterOfGravity, m_button_center_of_gravity);
    m_command_binding_manager.bind_tb_item(CommandName::ShowToolMarker, m_button_tool_marker);
    m_command_binding_manager.bind_tb_item(CommandName::ShowShell, m_button_shells);
    m_command_binding_manager.bind_tb_item(CommandName::SwitchToPlater, m_button_plater_switch);
    m_command_binding_manager.bind_tb_item(CommandName::SwitchToInspect, m_button_gcode_inspect);
}

void PreviewRenderModule::update_current_right_sidebar()
{
    if (m_button_gcode_inspect->checked()) {
        m_layout->sidebar_stack_layout()->switch_to_item(SidebarStackLayout::ItemType::GCode);
    } else {
        m_layout->sidebar_stack_layout()->switch_to_item(SidebarStackLayout::ItemType::Bed);
    }
}

void PreviewRenderModule::init_gizmos()
{
    m_gizmo_manager = std::make_unique<Scene::GizmoManager>(
        *m_device,
        *m_scene_presenter,
        m_project_interactor,
        nullptr
    );
    m_camera_gizmo = &m_gizmo_manager->add_base_gizmo<PreviewCameraGizmo>(
        m_workbench,
        m_project_interactor,
        *m_scene_presenter,
        *m_animation_manager
    );
    m_command_binding_manager.set_gizmos_command_registry(&m_gizmo_manager->command_registry());
}

void PreviewRenderModule::init_viewers(Render::Device& device)
{
    AppConfig& app_config = AppServices::instance().app_config();

    // Initialize the SLA ViewerWrapper

    ViewerWrapperBaseSettings base_settings;
    base_settings.layers_slider_base_flags.show_ruler           = app_config.get<bool>("show_ruler_in_dbl_slider");
    base_settings.layers_slider_base_flags.show_ruler_bg        = app_config.get<bool>("show_ruler_bg_in_dbl_slider");
    base_settings.layers_slider_base_flags.show_estimated_times = app_config.get<bool>("show_estimated_times_in_dbl_slider");
    // set layers slider callbacks
    base_settings.layers_slider_base_callbacks.on_thumb_move =
        std::bind(&PreviewRenderModule::on_slider_layers_on_thumb_move, this);
    base_settings.layers_slider_base_callbacks.request_extra_frames =
        std::bind(&PreviewRenderModule::on_request_extra_frames, this, std::placeholders::_1);
    base_settings.layers_slider_base_callbacks.app_config_changed = std::bind(
        &PreviewRenderModule::on_slider_layers_app_config_changed,
        this,
        std::placeholders::_1,
        std::placeholders::_2
    );

    if (m_sla_viewer.init(device, m_scene_presenter->scene(), m_gizmo_manager->data_factory())
        && m_sla_viewer.set_settings(base_settings)) {
        m_sla_slider_layers = Passthrough(m_sla_viewer.unload_double_slider_layers());
    } else {
        // log some error message
    }

    // Initialize the FDM ViewerWrapper

    FdmViewerWrapperMode mode = FdmViewerWrapperMode::EditorGCode;

    FdmViewerWrapperSettings settings;
    settings.ViewerWrapperBaseSettings::operator=(base_settings);

    settings.mode                             = mode;
    settings.slider_layers_use_default_colors = app_config.get<bool>("use_default_colors_in_dbl_slider");
    settings.seq_top_layer_only               = app_config.get<bool>("seq_top_layer_only");
    // set wrapper callbacks
    settings.cb_invalidate_slice = std::bind(&PreviewRenderModule::on_invalidate_slice, this);
    settings.cb_update_layers_slider =
        std::bind(&PreviewRenderModule::on_update_layers_slider, this, std::placeholders::_1);
    settings.cb_gcode_view_type_changed =
        std::bind(&PreviewRenderModule::on_gcode_view_type_changed, this);
    // set layers slider callbacks
    settings.layers_slider_base_callbacks.on_thumb_move =
        std::bind(&PreviewRenderModule::on_slider_layers_on_thumb_move, this);
    settings.cb_slider_layers_ticks_changed =
        std::bind(&PreviewRenderModule::on_slider_layers_ticks_changed, this);
    settings.cb_slider_layers_auto_color_change =
        std::bind(&PreviewRenderModule::on_slider_layers_auto_color_change, this);
    settings.cb_slider_layers_notify_empty_auto_color_change =
        std::bind(&PreviewRenderModule::on_slider_layers_notify_empty_auto_color_change, this);
    settings.cb_slider_layers_notify_empty_color_change_gcode =
        std::bind(&PreviewRenderModule::on_slider_layers_notify_empty_color_change_gcode, this);
    settings.cb_slider_layers_get_extruders_sequence = std::bind(
        &PreviewRenderModule::on_slider_layers_get_extruders_sequence,
        this,
        std::placeholders::_1
    );
    settings.cb_slider_layers_show_info_msg = std::bind(
        &PreviewRenderModule::on_slider_layers_show_info_msg,
        this,
        std::placeholders::_1,
        std::placeholders::_2
    );
    settings.cb_slider_layers_get_used_extruders_in_print = std::bind(
        &PreviewRenderModule::on_slider_layers_get_used_extruders_in_print,
        this,
        std::placeholders::_1
    );
    // set gcode slider callbacks
    settings.cb_slider_gcode_on_thumb_move =
        std::bind(&PreviewRenderModule::on_slider_gcode_on_thumb_move, this);

    if (m_fdm_viewer.init(device, m_scene_presenter->scene(), m_gizmo_manager->data_factory())
        && m_fdm_viewer.set_settings(settings)) {
        m_legend        = Passthrough(m_fdm_viewer.unload_legend());
        m_gcode_window  = Passthrough(m_fdm_viewer.unload_gcode_window());
        m_slider_gcode  = Passthrough(m_fdm_viewer.unload_double_slider_gcode());
        m_slider_layers = Passthrough(m_fdm_viewer.unload_double_slider_layers());
    } else {
        // log some error message
    }
}

void PreviewRenderModule::init_scene_layout()
{
    ASSERT(m_render_module_navigator);

    m_preferences_dialog = std::make_unique<PreferencesDialog>(
        AppServices::instance().app_config_interactor(),
        *m_render_module_navigator
    );

    m_number_entry_dialog = std::make_unique<NumberEntryDialog>(
        *m_render_module_navigator
    );

    m_invalid_data_dialog = std::make_unique<InvalidDataDialog>(
        m_project_interactor,
        *m_render_module_navigator
    );
    m_crashed_projects_dialog =
        std::make_unique<CrashedProjectsDialog>(m_project_interactor, *m_render_module_navigator);

    m_preset_updater_dialog = std::make_unique<PresetUpdaterDialog>(
        AppServices::instance().preset_updater_controller(),
        *m_render_module_navigator
    );

    // >> This code is same for Plater/PreviewRenderModule
    m_top_bar = std::make_unique<TopBar>(
        &m_project_interactor,
        this,
        *m_render_module_navigator,
        nullptr,
        *m_project_saver,
        m_plugin_system
    );

    m_object_list = Passthrough(std::make_unique<ObjectListWindow>(&m_project_interactor, false));
    m_object_list->set_collapsed(m_render_module_navigator->object_list_collapsed());

    m_cube_view   = std::make_unique<CubeView>();
    m_sidebar_bed = std::make_unique<SidebarBed>(m_project_interactor, *m_render_module_navigator);
    m_sidebar_print =
        std::make_unique<SidebarPrint>(m_project_interactor, *m_render_module_navigator);
    m_sidebar_object             = std::make_unique<SidebarObject>(m_project_interactor);
    m_pop_notification_list_view = std::make_unique<PopNotification::PopNotificationListView>(
        AppServices::instance().pop_notification_center().observable_list()
    );
    m_sidebar_auto_reslice = std::make_unique<SidebarAutoReslice>(m_project_interactor);

    m_sidebar_action_buttons =
        std::make_unique<SidebarPreviewActionButtons>(m_render_module_navigator);
    m_sidebar_action_buttons->on_init(&m_project_interactor);

    m_layout.reset(new PreviewRenderLayout(
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
        m_gcode_window.release(),
        m_legend.release(),
        m_slider_layers.release(),
        m_sla_slider_layers.release(),
        m_slider_gcode.release(),
        m_sidebar_auto_reslice.release(),
        m_number_entry_dialog.release(),
        m_invalid_data_dialog.release(),
        m_crashed_projects_dialog.release(),
        m_preset_updater_dialog.release()
    ));
    m_layout->init();

    // register FDM layer_slider specific commands
    m_slider_layers->register_commands(m_menu_manager, m_command_binding_manager);
    // register SLA layer_slider specific commands
    m_sla_slider_layers->register_commands(m_menu_manager, m_command_binding_manager);
    // G-code slider specific commands
    m_slider_gcode->register_commands(m_menu_manager, m_command_binding_manager);

    m_button_travels = m_layout->add_toolbar_item_checkable(
        ToolbarID::ToolRight,
        Render::Icon::LegendTravel,
        to_string(OptionType::Travels),
        m_fdm_viewer.is_option_visible(OptionType::Travels)
    );

    m_button_wipes = m_layout->add_toolbar_item_checkable(
        ToolbarID::ToolRight,
        Render::Icon::LegendWipe,
        to_string(OptionType::Wipes),
        m_fdm_viewer.is_option_visible(OptionType::Wipes)
    );

    m_button_retractions = m_layout->add_toolbar_item_checkable(
        ToolbarID::ToolRight,
        Render::Icon::LegendRetract,
        to_string(OptionType::Retractions),
        m_fdm_viewer.is_option_visible(OptionType::Retractions)
    );

    m_button_unretractions = m_layout->add_toolbar_item_checkable(
        ToolbarID::ToolRight,
        Render::Icon::LegendDeretract,
        to_string(OptionType::Unretractions),
        m_fdm_viewer.is_option_visible(OptionType::Unretractions)
    );

    m_button_seams = m_layout->add_toolbar_item_checkable(
        ToolbarID::ToolRight,
        Render::Icon::LegendSeams,
        to_string(OptionType::Seams),
        m_fdm_viewer.is_option_visible(OptionType::Seams)
    );

    m_button_tool_changes = m_layout->add_toolbar_item_checkable(
        ToolbarID::ToolRight,
        Render::Icon::LegendToolChanges,
        to_string(OptionType::ToolChanges),
        m_fdm_viewer.is_option_visible(OptionType::ToolChanges)
    );

    m_button_color_changes = m_layout->add_toolbar_item_checkable(
        ToolbarID::ToolRight,
        Render::Icon::LegendColorChanges,
        to_string(OptionType::ColorChanges),
        m_fdm_viewer.is_option_visible(OptionType::ColorChanges)
    );

    m_button_pause_prints = m_layout->add_toolbar_item_checkable(
        ToolbarID::ToolRight,
        Render::Icon::LegendPausePrints,
        to_string(OptionType::PausePrints),
        m_fdm_viewer.is_option_visible(OptionType::PausePrints)
    );

    m_button_custom_gcodes = m_layout->add_toolbar_item_checkable(
        ToolbarID::ToolRight,
        Render::Icon::LegendCustomGCodes,
        to_string(OptionType::CustomGCodes),
        m_fdm_viewer.is_option_visible(OptionType::CustomGCodes)
    );

    m_button_center_of_gravity = m_layout->add_toolbar_item_checkable(
        ToolbarID::ToolRight,
        Render::Icon::LegendCOG,
        to_string(OptionType::CenterOfGravity),
        m_fdm_viewer.is_option_visible(OptionType::CenterOfGravity)
    );

    m_button_tool_marker = m_layout->add_toolbar_item_checkable(
        ToolbarID::ToolRight,
        Render::Icon::LegendToolMarker,
        to_string(OptionType::ToolMarker),
        m_fdm_viewer.is_option_visible(OptionType::ToolMarker)
    );

    m_button_shells = m_layout->add_toolbar_item_checkable(
        ToolbarID::ToolRight,
        Render::Icon::LegendShells,
        "Shells",
        m_scene_presenter->are_shells_visible()
    );

    // init toolbars

    m_button_plater_switch = m_layout->add_toolbar_item_switch(
        ToolbarID::Mode,
        Render::Icon::ObjectIcon,
        _u8L("Prepare"),
        _u8L("Prepare Mode"),
        ToolBarSwitchButton::SwitchPosition::Left
    );

    ToolBarButton* preview_button = m_layout->add_toolbar_item_switch(
        ToolbarID::Mode,
        Render::Icon::Preview,
        _u8L("Preview"),
        _u8L("Preview Mode"),
        ToolBarSwitchButton::SwitchPosition::Right
    );
    preview_button->set_checked(true);

    m_button_gcode_inspect = m_layout->add_toolbar_item_checkable(
        ToolbarID::Mode,
        Render::Icon::LayersInspect,
        _u8L("G-code inspect")
    );

    // Initialize toolbar buttons visibility
    update_toolbar_visibility();
    // <<
}

void PreviewRenderModule::update_toolbar_visibility()
{
    const OptionTypes& options = m_fdm_viewer.options();
    auto fdm_has_option        = [&](OptionType type)
    { return std::find(options.begin(), options.end(), type) != options.end(); };
    const bool fdm_has_gcode =
        m_fdm_viewer.has_data() && m_fdm_viewer.mode() != FdmViewerWrapperMode::EditorPreGCode;

    m_layout->tool_right_toolbar()->set_visible(fdm_has_gcode);
    m_button_travels->set_visible(fdm_has_gcode && fdm_has_option(OptionType::Travels));
    m_button_retractions->set_visible(fdm_has_gcode && fdm_has_option(OptionType::Retractions));
    m_button_unretractions->set_visible(fdm_has_gcode && fdm_has_option(OptionType::Unretractions));
    m_button_seams->set_visible(fdm_has_gcode && fdm_has_option(OptionType::Seams));
    m_button_tool_changes->set_visible(fdm_has_gcode && fdm_has_option(OptionType::ToolChanges));
    m_button_color_changes->set_visible(fdm_has_gcode && fdm_has_option(OptionType::ColorChanges));
    m_button_pause_prints->set_visible(fdm_has_gcode && fdm_has_option(OptionType::PausePrints));
    m_button_custom_gcodes->set_visible(fdm_has_gcode && fdm_has_option(OptionType::CustomGCodes));
    m_button_center_of_gravity->set_visible(fdm_has_gcode);
    m_button_tool_marker->set_visible(fdm_has_gcode);
    m_button_shells
        ->set_visible(fdm_has_gcode /*m_fdm_viewer.mode() != FdmViewerWrapperMode::GCodeViewer*/);
    m_button_wipes->set_visible(fdm_has_gcode);

    // m_button_gcode_inspect
}

void PreviewRenderModule::init_dialog_navigation()
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
    m_dialog_navigation.insert_dialog(m_invalid_data_dialog.get());
    m_dialog_navigation.insert_dialog(m_preset_updater_dialog.get());

    m_dialog_navigation.insert_dialog(&m_sidebar_action_buttons->physical_printer_settings_dialog());
    m_dialog_navigation.insert_dialog(
        &m_sidebar_action_buttons->physical_printer_advanced_settings_dialog(),
        &m_sidebar_action_buttons->physical_printer_settings_dialog()
    );
}

void PreviewRenderModule::update_fdm_viewer_data(const Domain::SlicingId id)
{
    std::optional<Biz::FDMResultRef> fdm_result{
        m_project_interactor.fdm_result_cache().get_result(id)
    };
    m_scene_presenter->update_bed_instance_error_state(
        id,
        fdm_result.has_value() && !fdm_result->get().contained_in_bed
    );

    if (m_project_interactor.selected_bed_slicing_id() != id)
        return;

    if (m_viewer == &m_sla_viewer) {
        update_viewer();
    }

    if (m_viewer != &m_fdm_viewer) {
        // Safety guard to avoid updating m_fdm_viewer when it is not the active viewer.
        // This code path may be triggered only because of print result invalidations.
        return;
    }

    if (!fdm_result) {
        m_fdm_viewer.reset();
        return;
    }

    const Domain::Project& project{m_project_interactor.workbench().project(id.project_id)};
    const Domain::BedInstance* bed_instance{project.find_bed_instance_by_id(id.bed_instance_id)};

    if (bed_instance == nullptr) {
        return;
    }

    m_fdm_viewer.load_from_result(*fdm_result, bed_instance->transformation.get_matrix());

    update_toolbar_visibility();

    m_scene_presenter->update_shells_visibility();

    // request redraw
    request_render();

    if (!m_fdm_viewer.has_data()) {
        // log some error message
        return;
    }

    const GCodeEvents& gcode_events = m_fdm_viewer.gcode_events();
    const ViewType heuristic_view_type =
        !gcode_events.empty()                       ? ViewType::ColorPrint :
            m_fdm_viewer.used_extruders_count() > 1 ? ViewType::Tool :
                                                      ViewType::FeatureType;

    const Domain::ConfigContainer* config_container{
        project.find_config_container_by_bed_instance_id(id.bed_instance_id)
    };
    ASSERT(config_container != nullptr);

    ProjectGCodeViewTypeStates& proj_states = m_gcode_view_type_states.project(id.project_id);

    // Lazy prune: forget states for config containers that no longer exist in this project.
    std::erase_if(proj_states.by_config_container, [&project](const auto& kv) {
        return project.find_config_container(kv.first) == nullptr;
    });

    GCodeViewTypeState& view_type_state = proj_states.by_config_container[config_container->id().id];
    if (view_type_state.last_heuristic != heuristic_view_type)
        view_type_state.user_choice.reset();
    view_type_state.last_heuristic = heuristic_view_type;

    m_fdm_viewer.set_view_type(
        view_type_state.user_choice.has_value() && m_fdm_viewer.is_view_type_available(*view_type_state.user_choice)
            ? *view_type_state.user_choice : heuristic_view_type
    );

    update_scene_aabb();

    // hbFIXME -> This code is commented out until @barzto fixes
    // the order of on_select_project and on_select_config_container calls.
    // center_camera_on_selected_bed();
}

void PreviewRenderModule::update_sla_viewer_result_data(const Domain::SlicingId id)
{
    std::optional<Biz::SLAResultRef> sla_result{
        m_project_interactor.sla_result_cache().get_result(id)
    };
    m_scene_presenter->update_bed_instance_error_state(
        id,
        sla_result.has_value() && !sla_result->get().contained_in_bed
    );

    if (m_project_interactor.selected_bed_slicing_id() != id)
        return;

    if (m_viewer == &m_fdm_viewer) {
        update_viewer();
    }

    if (m_viewer != &m_sla_viewer) {
        // Safety guard to avoid updating m_sla_viewer when it is not the active viewer.
        // This code path may be triggered only because of print result invalidations.
        return;
    }

    update_toolbar_visibility();

    if (!sla_result) {
        m_sla_viewer.reset_result();
    } else {
        const Domain::BedInstance* bed_instance = m_project_interactor.workbench()
                                                      .project(id.project_id)
                                                      .find_bed_instance_by_id(id.bed_instance_id);
        ASSERT(bed_instance != nullptr);
        m_sla_viewer.load_from_result(sla_result->get(), bed_instance->transformation.get_matrix());
    }
}

void PreviewRenderModule::update_sla_viewer_object_data(
    const Domain::SlicingId id,
    Domain::ObjectID instance_id
)
{
    if (m_project_interactor.selected_bed_slicing_id() != id)
        return;

    const std::optional<Biz::SLAObjectRef> sla_object_result{
        m_project_interactor.sla_object_cache().get_instance({id, instance_id})
    };
    if (sla_object_result) {
        const Domain::BedInstance* bed_instance = m_project_interactor.workbench()
                                                      .project(id.project_id)
                                                      .find_bed_instance_by_id(id.bed_instance_id);
        ASSERT(bed_instance != nullptr);
        m_sla_viewer.load_from_object(
            sla_object_result->get(),
            bed_instance->transformation.get_matrix()
        );
    }
    else
        m_sla_viewer.reset_object(instance_id);

    m_scene_presenter->update_shells_visibility();
}

void PreviewRenderModule::update_sla_viewer_data(const Domain::SlicingId id)
{
    if (m_project_interactor.selected_bed_slicing_id() != id)
        return;

    m_sla_viewer.reset();
    update_sla_viewer_result_data(id);

    const std::vector<Slic3r::Domain::ObjectID> object_ids =
        m_project_interactor.sla_object_cache().get_object_ids(id);

    for (const Slic3r::Domain::ObjectID& obj_id : object_ids) {
        update_sla_viewer_object_data(id, obj_id);
    }
}

void PreviewRenderModule::on_invalidate_slice()
{
    // TODO
}

void PreviewRenderModule::on_update_layers_slider(const CustomGCode::Info& info)
{
    // TODO
}

void PreviewRenderModule::on_request_extra_frames(unsigned int count)
{
    for (unsigned int i = 0; i < count; ++i) {
        request_render();
    }
}

void PreviewRenderModule::on_gcode_view_type_changed()
{
    const Domain::SelectionId project_id = m_project_interactor.selected_project_id();
    const Domain::SelectionId container_id = m_project_interactor.selected_config_container_id();
    m_gcode_view_type_states.project(project_id).by_config_container[container_id].user_choice =
        m_fdm_viewer.view_type();
}

void PreviewRenderModule::on_slider_layers_on_thumb_move()
{
    update_scene_aabb();
}

void PreviewRenderModule::on_slider_layers_ticks_changed()
{
    const Domain::SlicingId slicing_id = m_project_interactor.selected_bed_slicing_id();
    m_project_interactor.scene_interactor().update_custom_gcode(
        slicing_id,
        m_slider_layers->ticks_values()
    );
}

bool PreviewRenderModule::on_slider_layers_auto_color_change()
{
    // TODO collect data from print config and objects, and update layers slider ticks
    // See:
    // master:                TickCodeManager::auto_color_change()
    // lm_processor_squashed: Preview::layers_slider_auto_color_changed_callback()
    return true;
}

void PreviewRenderModule::on_slider_layers_notify_empty_auto_color_change()
{
    // TODO -> fire notification PopNotificationType::EmptyAutoColorChange
}

void PreviewRenderModule::on_slider_layers_notify_empty_color_change_gcode()
{
    // TODO -> fire notification PopNotificationType::EmptyColorChangeCode
}

bool PreviewRenderModule::on_slider_layers_get_extruders_sequence(ExtrudersSequence& sequence)
{
    // TODO
    return false;
}

int PreviewRenderModule::on_slider_layers_show_info_msg(const std::string& message, int btns_flag)
{
    // TODO
    return 0;
}

std::set<int> PreviewRenderModule::on_slider_layers_get_used_extruders_in_print(float print_z)
{
    // TODO: replace the following code with some using ToolOrdering,
    // see master: TickCodeManager::get_used_extruders_for_tick()

    std::vector<uint8_t> ids = m_fdm_viewer.used_extruders_ids();
    std::set<int> ret;
    std::transform(
        ids.begin(),
        ids.end(),
        std::inserter(ret, ret.begin()),
        [](uint8_t id) { return id + 1; }
    );

    return ret;
}

void PreviewRenderModule::on_slider_layers_app_config_changed(const std::string& key, bool val)
{
    if (key == "seq_top_layer_only") {
        bool active   = m_fdm_viewer.is_top_layer_only_view_range();
        bool required = val;
        if (active != required) {
            m_fdm_viewer.toggle_top_layer_only_view_range();
            const Interval& range = m_fdm_viewer.layers_range();
            m_fdm_viewer.set_layers_range(range[0], range[1]);
        }
    }

    AppConfig& app_config = AppServices::instance().app_config();

    app_config.set(key, val);

    constexpr std::array<std::string_view, 3> layers_slider_base_flags_keys = {
        "show_ruler_in_dbl_slider",
        "show_ruler_bg_in_dbl_slider",
        "show_estimated_times_in_dbl_slider"
    };

    if (std::find(layers_slider_base_flags_keys.begin(), layers_slider_base_flags_keys.end(), key)
        != layers_slider_base_flags_keys.end())
    {
        // synchronise base layer slider flags between sliders
        LayersSliderBaseFlags layers_slider_base_flags;
        layers_slider_base_flags.show_ruler = app_config.get<bool>("show_ruler_in_dbl_slider");
        layers_slider_base_flags.show_ruler_bg =
            app_config.get<bool>("show_ruler_bg_in_dbl_slider");
        layers_slider_base_flags.show_estimated_times =
            app_config.get<bool>("show_estimated_times_in_dbl_slider");

        if (m_viewer != &m_fdm_viewer) {
            m_fdm_viewer.set_layers_slider_base_flags(layers_slider_base_flags);
        }
        if (m_viewer != &m_sla_viewer) {
            m_sla_viewer.set_layers_slider_base_flags(layers_slider_base_flags);
        }
    }
}

void PreviewRenderModule::on_slider_gcode_on_thumb_move()
{
    update_scene_aabb();
}

void PreviewRenderModule::update_shells()
{
    m_scene_presenter->remove_all_shells();
    m_scene_presenter->add_shells();
    m_scene_presenter->update_shells_visibility();
}

void PreviewRenderModule::update_bed_instances()
{
    m_scene_presenter->remove_all_bed_instances();
    const Plater::BedThumbnailTextures& store_thumbnails =
        m_thumbnail_store->projects.selected().thumbnails;
    Domain::BedRefs beds;
    beds.reserve(store_thumbnails.size());
    Plater::BedThumbnailTextures thumbnails;
    thumbnails.reserve(store_thumbnails.size());
    for (const auto& t : store_thumbnails) {
        const auto cc = m_workbench.project(t.project_id)
                            .find_config_container_by_bed_instance_id(t.bed_instance_id);
        if (cc != nullptr) {
            beds.emplace_back(cc->id().id, t.bed_instance_id);
            thumbnails.emplace_back(t);
        }
    }
    m_scene_presenter->add_bed_instances(beds);
    m_scene_presenter->update_bed_instances();
    m_object_list->set_bed_instance_icons(thumbnails);
}

void PreviewRenderModule::update_viewer()
{
    Domain::SelectionId config_container_id =
        m_project_interactor.scene_interactor().selected_config_container_id();
    const Domain::ConfigContainer* cc =
        m_project_interactor.selected_project().find_config_container(config_container_id);
    if (!cc) {
        const Biz::Scene::BedSelection& bed_selection =
            m_project_interactor.scene_interactor().bed_selection();
        if (!bed_selection.empty()) {
            config_container_id = bed_selection.last_selected_bed().config_container_id;
        }
        ASSERT(config_container_id != Domain::INVALID_ID);
        cc = m_project_interactor.selected_project().find_config_container(config_container_id);
        if (!cc)
            return;
        ASSERT(cc);
    }

    if (cc->print_technology() == Domain::PrinterTechnology::SLA) {
        m_fdm_viewer.reset();
        m_fdm_viewer.clear_scene();
        if (m_viewer != &m_sla_viewer)
            m_viewer = &m_sla_viewer;
    } else {
        m_sla_viewer.reset();
        m_sla_viewer.clear_scene();
        if (m_viewer != &m_fdm_viewer)
            m_viewer = &m_fdm_viewer;
    }

    m_viewer->set_scene(m_scene_presenter->scene());
}

void PreviewRenderModule::update_scene_aabb()
{
    if (m_viewer == &m_fdm_viewer) {
        Scene::Scene& scene = m_scene_presenter->scene();
        Scene::Node* node   = scene.root().query_first(
            [](const Scene::Node* n)
            {
                const libvgcode::GCodeNodeTag* tag = n->tag_of_type<libvgcode::GCodeNodeTag>();
                return tag != nullptr && tag->type == libvgcode::GCodeElementType::ToolMarker;
            },
            true
        );
        DEBUG_ASSERT(node != nullptr);

        bool enabled = m_fdm_viewer.tool_marker_enabled()
            && m_fdm_viewer.is_option_visible(Biz::libpgcode::OptionType::ToolMarker);
        node->set_enabled(enabled);
        if (enabled && m_fdm_viewer.current_vertex_id() > 0) {
            Scene::Transform xtrafo = Scene::Transform::Identity();
            xtrafo.scale(m_fdm_viewer.tool_marker_scale_factor());
            xtrafo.translate(m_fdm_viewer.tool_marker_position().cast<double>());
            node->set_local_transform(xtrafo);
        }
    }
}

} // namespace Slic3r::App::Preview
