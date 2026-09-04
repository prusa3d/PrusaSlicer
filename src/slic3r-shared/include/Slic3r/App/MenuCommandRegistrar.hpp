#pragma once

#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/App/Wildcards.hpp"

#include <boost/filesystem/path.hpp>
#include <string>

namespace Slic3r::App::Platform {
class AbstractRenderModule;
} // namespace Slic3r::App::Platform

namespace Slic3r::App::Scene {
class GeometryDataFactory;
class ISceneProvider;
} // namespace Slic3r::App::Scene

namespace Slic3r::Biz {
class ClipboardInteractor;
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Lua {
class PluginSystem;
} // namespace Slic3r::App::Lua

namespace Slic3r::App {

class MenuManager;
struct ThumbnailStore;
class Navigator;
class ProjectSaver;

class MenuCommandRegistrar
{
public:
    MenuCommandRegistrar(
        Platform::AbstractRenderModule& render_module,
        Biz::ProjectInteractor& project_interactor,
        Navigator& navigator,
        ProjectSaver& project_saver
    );

    void register_top_bar_menus(Lua::PluginSystem* plugin_system = nullptr);
    void register_context_menus(
        Scene::GeometryDataFactory& data_factory,
        Scene::ISceneProvider* scene_provider
    );

    void update_main_menu_plugin_commands(Lua::PluginSystem& plugin_system);

private:

    void register_undo_redo_commands();
    void register_main_menu_commands(Lua::PluginSystem* plugin_system);
    void register_main_menu_edit_commands();
    void register_main_menu_view_commands();
    void register_main_menu_plugin_commands(Lua::PluginSystem& plugin_system);
    void register_main_menu_config_commands();
    void register_main_menu_help_commands();

    void register_file_menu_commands();
    void register_file_menu_import_commands();
    void register_file_menu_export_commands();

    void register_bed_menu_commands();
    void register_bed_menu_add_shape_commands();

    void register_object_menu_commands();
    void register_object_menu_add_volume_commands();

    void register_svg_or_text_volume_menu_commands();
    void register_volume_menu_commands();
    void register_multi_object_menu_commands();

    /**
     * @brief Folder a file dialog should start in.
     *
     * The directory of the selected project, falling back to the last one the user
     * picked in any file dialog.
     */
    boost::filesystem::path default_dialog_folder() const;

    void load_project();

    void install_plugin(Lua::PluginSystem& plugin_system);

    void load_object(Wildcards::TypeFlag specific_type = Wildcards::TypeFlag::None);

    void load_volume(
        Domain::ModelVolumeType type,
        Wildcards::TypeFlag specific_type = Wildcards::TypeFlag::None
    );

    void load_shape_from_gallery(
        Domain::ModelVolumeType type = Domain::ModelVolumeType::MODEL_PART
    );

    void export_selection_as_stl_obj();

    void replace_selected_volume_with_stl();

    void reload_selection_from_disk();

    Platform::AbstractRenderModule& m_render_module;
    MenuManager& m_menu_manager;
    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;
    ProjectSaver& m_project_saver;

    Scene::GeometryDataFactory* m_data_factory{nullptr};
    Scene::ISceneProvider* m_scene_provider{nullptr};
    Biz::ClipboardInteractor& m_clipboard_interactor;
};

} // namespace Slic3r::App
