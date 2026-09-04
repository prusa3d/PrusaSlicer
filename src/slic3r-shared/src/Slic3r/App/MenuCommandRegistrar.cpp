#include "Slic3r/App/MenuCommandRegistrar.hpp"

#include "Slic3r/Directories.hpp"
#include "Slic3r/App/MenuManager.hpp"
#include "Slic3r/App/UIItemCommand.hpp"
#include "Slic3r/App/Platform/CommandName.hpp"
#include "Slic3r/App/Platform/AbstractRenderModule.hpp"
#include "Slic3r/App/ResultExport/ExportActions.hpp"
#include "Slic3r/App/Scene/GeometryDataFactory.hpp"
#include "Slic3r/App/Scene/ISceneProvider.hpp"
#include "Slic3r/App/Scene/GizmoManager.hpp"
#include "Slic3r/App/Scene/EmbossCreate.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/ThumbnailStore.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/Wildcards.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/Localization.hpp"
#include "Slic3r/App/OpenBrowser.hpp"
#include "Slic3r/App/Lua/ProjectApi.hpp"
#include "Slic3r/App/Platform/IFileExplorerHandler.hpp"
#include "Slic3r/App/ProjectSaver.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/Scene/Selection.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Format/3mf.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Biz/FileLoadingLogic.hpp"
#include "Slic3r/Biz/MeshExportLogic.hpp"
#include "Slic3r/Biz/VolumeReloadLogic.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

#include "Slic3r/App/Plater/PlaterRenderModule.hpp"
#include "Slic3r/App/Plater/TextGizmo.hpp"

#include <optional>

namespace Slic3r::App {

// #define SHOW_NOT_IMPLEMENTED_ITEMS

using namespace Slic3r::Biz;
using CommandName = Platform::CommandName;

namespace {

/**
 * File types offered when importing geometry into the scene.
 */
constexpr Wildcards::TypeFlag import_file_types = Wildcards::TypeFlag::Project3mf
    | Wildcards::TypeFlag::Stl
    | Wildcards::TypeFlag::Obj
    | Wildcards::TypeFlag::Svg
    | Wildcards::TypeFlag::Step;

/**
 * File types offered when replacing or reloading a volume mesh.
 */
constexpr Wildcards::TypeFlag model_file_types = Wildcards::TypeFlag::Project3mf
    | Wildcards::TypeFlag::Stl
    | Wildcards::TypeFlag::Obj
    | Wildcards::TypeFlag::Step;

class CommandBuilder
{
public:
    CommandBuilder(MenuManager& menu_manager, Platform::AbstractRenderModule& render_module) :
        m_menu_manager(menu_manager),
        m_render_module(render_module)
    {}

    ~CommandBuilder()
    {
        register_commands();
    }

    CommandBuilder& push_path_level(MenuItemName name)
    {
        m_path.push_back(name);
        return *this;
    }

    CommandBuilder& pop_path_level()
    {
        ASSERT(!m_path.empty());
        m_path.pop_back();
        return *this;
    }

    CommandBuilder& append_item(
        MenuItemName key,
        const char* command_name,
        std::function<void()> action,
        UIItemCommandExtraOpts extra_opts = {}
    )
    {
        ASSERT(!m_path.empty());
        auto path_and_key = m_path;
        path_and_key.push_back(key);
        m_commands.emplace_back(
            CommandElement{path_and_key, command_name, std::move(action), extra_opts}
        );
        return *this;
    }

    CommandBuilder& append_item_from_command(MenuItemName key, const char* command_name)
    {
        ASSERT(!m_path.empty());
        auto path_and_key = m_path;
        path_and_key.push_back(key);
        m_commands.emplace_back(CommandElement{path_and_key, command_name, nullptr, {}});
        return *this;
    }

    CommandBuilder& append_separator()
    {
        ASSERT(!m_path.empty());
        m_commands.emplace_back(CommandElement{m_path, nullptr, []() {}, {}});
        return *this;
    }

    void register_commands_and_clear_cache()
    {
        register_commands();
        m_commands.clear();
    }

private:
    void register_commands() const
    {
        for (const auto& command : m_commands) {
            if (command.name) {
                if (command.action) {
                    m_menu_manager.register_menu_item(
                        command.path,
                        std::make_unique<UIItemCommand>(
                            command.name,
                            command.action,
                            command.extra_opts
                        )
                    );
                } else {
                    m_menu_manager.register_menu_item_from_command(
                        command.path,
                        m_render_module.command(command.name)

                    );
                }
            } else {
                m_menu_manager.register_menu_separator_item(command.path);
            }
        }
    }

private:
    struct CommandElement
    {
        std::vector<UniversalMenuItemName> path;
        const char* name; // nullptr means separator
        std::function<void()> action{nullptr}; // nullptr means use already registered command
        UIItemCommandExtraOpts extra_opts;
    };

    std::vector<UniversalMenuItemName> m_path;
    std::vector<CommandElement> m_commands;

    MenuManager& m_menu_manager;
    Platform::AbstractRenderModule& m_render_module;
};

} // namespace

MenuCommandRegistrar::MenuCommandRegistrar(
    Platform::AbstractRenderModule& render_module,
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator,
    ProjectSaver& project_saver
) :
    m_render_module(render_module),
    m_menu_manager(m_render_module.menu_manager()),
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_project_saver(project_saver),
    m_clipboard_interactor(m_project_interactor.clipboard_interactor())
{}

void MenuCommandRegistrar::register_top_bar_menus(Lua::PluginSystem* plugin_system)
{
    register_undo_redo_commands();
    register_main_menu_commands(plugin_system);
    register_file_menu_commands();
}

void MenuCommandRegistrar::register_context_menus(
    Scene::GeometryDataFactory& data_factory,
    Scene::ISceneProvider* scene_provider
)
{
    m_data_factory   = &data_factory;
    m_scene_provider = scene_provider;

    register_bed_menu_commands();
    register_object_menu_commands();
    register_svg_or_text_volume_menu_commands();
    register_volume_menu_commands();
    register_multi_object_menu_commands();
}

void MenuCommandRegistrar::register_bed_menu_commands()
{
    register_bed_menu_add_shape_commands();

    auto any_selected_bed_has_object = [this]() -> bool
    {
        const Biz::Scene::BedSelection& selection{
            m_project_interactor.scene_interactor().bed_selection()
        };
        if (selection.empty()) {
            return false;
        }
        const Biz::Scene::BedInstances beds{
            m_project_interactor.scene_interactor().selected_bed_instances()
        };

        for (const auto& bed : beds) {
            if (!bed.get().model_instances.empty())
                return true;
        }
        return false;
    };

    CommandBuilder builder(m_menu_manager, m_render_module);
    builder.push_path_level(MenuItemName::BedContextMenu)
        .append_separator()
        .append_item(
            MenuItemName::ArrangeBed,
            CommandName::ArrangeBed,
            [this]
            {
                const auto it{m_render_module.gizmo_commands().find("arrange-gizmo-arrange-local")};
                if (it != m_render_module.gizmo_commands().end()) {
                    it->second->execute();
                }
            },
            UIItemCommandExtraOpts{
                .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                    Platform::KeyModifiers{},
                    Platform::KeyCode::D
                }},
                .enabled = [any_selected_bed_has_object]() { return any_selected_bed_has_object(); },
            }
        )
        .append_item(
            MenuItemName::ArrangeSelectionBed,
            CommandName::ArrangeSelectionBed,
            [this]
            {
                const auto it{m_render_module.gizmo_commands().find("arrange-gizmo-arrange-local-selection")};
                if (it != m_render_module.gizmo_commands().end()) {
                    it->second->execute();
                }
            },
            UIItemCommandExtraOpts{
                .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                    Platform::KeyModifiers(Platform::KeyModifier::Shift),
                    Platform::KeyCode::D
                }},
                .enabled = [this]() { return !m_project_interactor.scene_interactor().object_selection().empty(); },
            }
        )
        .append_item(
            MenuItemName::SelectAllOnBed,
            CommandName::SelectAllOnBed,
            [this]
            {
                const Biz::Scene::BedInstances beds{
                    m_project_interactor.scene_interactor().selected_bed_instances()};

                Biz::Scene::ObjectSelection new_selection;
                for (const auto& bed : beds) {
                    for (const auto& instance : bed.get().model_instances) {
                        new_selection.elements.push_back(
                            {instance->get_object()->id().id, instance->id().id}
                        );
                    }
                }
                m_project_interactor.scene_interactor().set_object_selection(new_selection);
            },
            UIItemCommandExtraOpts{
                .enabled = [any_selected_bed_has_object]() { return any_selected_bed_has_object(); }
            }
        )
        .append_separator()
        .append_item(
            MenuItemName::DeleteBed,
            CommandName::DeleteBed,
            [this]
            {
                if (m_project_interactor.selected_config_container().bed_instances().size() > 1) {
                    m_project_interactor.scene_interactor().remove_bed_instance(
                        m_project_interactor.scene_interactor().bed_selection().last_selected_bed()
                    );
                    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::DeleteBed);
                } else {
                    m_project_interactor.remove_config_container(
                        m_project_interactor.selected_config_container_id()
                    );
                    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::DeleteConfigContainer);
                }
            },
            UIItemCommandExtraOpts{
                .enabled = [this]()
                {
                    if (m_project_interactor.selected_config_container_id() == Domain::INVALID_ID) {
                        return false;
                    }
                    return
                        m_project_interactor.selected_project().config_containers().size() > 1
                     || m_project_interactor.selected_config_container().bed_instances().size() > 1;
                }
            }
        );
}

static std::string geometry_name(Scene::GeometryDataId geometry_id)
{
    switch (geometry_id) {
    case Scene::GeometryDataId::Cube:
        return _u8L("Cube");
    case Scene::GeometryDataId::Cylinder:
        return _u8L("Cylinder");
    case Scene::GeometryDataId::Sphere:
        return _u8L("Sphere");
    case Scene::GeometryDataId::Segment:
    case Scene::GeometryDataId::Cone:
    case Scene::GeometryDataId::Circle:
    case Scene::GeometryDataId::GradedCircle:
    case Scene::GeometryDataId::SmoothSphere:
    case Scene::GeometryDataId::ToolMarker:
    case Scene::GeometryDataId::CandyButton:
        break;
    }
    PANIC("Unsupported geometry");
    return "Undefined";
}

void MenuCommandRegistrar::register_bed_menu_add_shape_commands()
{
    auto add_object_shape =
        [this](Scene::GeometryDataId geometry_id, UndoSnapshotType snapshot_type)
    {
        const Domain::ElementRefs new_instances =
            m_project_interactor.scene_interactor().add_object_to_active_bed(
                m_data_factory->triangle_mesh(geometry_id)->triangles(),
                geometry_name(geometry_id)
            );

        const Domain::BedRef target_bed =
            m_project_interactor.scene_interactor().bed_selection().last_selected_bed();
        m_project_interactor.arrange_interactor().arrange_added_instances(
            m_project_interactor.selected_project_id(),
            new_instances,
            target_bed,
            snapshot_type
        );
    };

    CommandBuilder builder(m_menu_manager, m_render_module);
    builder.push_path_level(MenuItemName::BedContextMenu)
        .push_path_level(MenuItemName::AddObjectShape)
        .append_item_from_command(MenuItemName::ObjectShapeLoad, CommandName::ImportGeometry)
        .append_separator()
        .append_item(
            MenuItemName::ObjectShapeCube,
            "add-object-shape-cube",
            [this, add_object_shape]()
            {
                add_object_shape(Scene::GeometryDataId::Cube, UndoSnapshotType::AddCube);
            }
        )
        .append_item(
            MenuItemName::ObjectShapeCylinder,
            "add-object-shape-cylinder",
            [this, add_object_shape]()
            {
                add_object_shape(Scene::GeometryDataId::Cylinder, UndoSnapshotType::AddCylinder);
            }
        )
        .append_item(
            MenuItemName::ObjectShapeSphere,
            "add-object-shape-sphere",
            [this, add_object_shape]()
            {
                add_object_shape(Scene::GeometryDataId::Sphere, UndoSnapshotType::AddSphere);
            }
        )
        .append_separator()
        .append_item_from_command(MenuItemName::ObjectShapeText, CommandName::CreateObjectAsText)
        .append_item(
            MenuItemName::ObjectShapeSvg,
            "add-object-shape-svg",
            [this]() { load_object(Wildcards::TypeFlag::Svg); },
            UIItemCommandExtraOpts{
                .keyboard_shortcuts =
                    Platform::KeyboardShortcuts{
                        Platform::KeyboardShortcut{0, Platform::KeyCode::G}
                    },
                .enabled = [this]()
                { return m_project_interactor.scene_interactor().object_selection().empty(); }
            }
        )
        .append_separator()
        .append_item(
            MenuItemName::ObjectShapeFromGallery,
            "add-object-shape-gallery",
            [this]()
            {
                load_shape_from_gallery();
                m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::AddObject);
            },
            UIItemCommandExtraOpts{.todo = true}
        );
}

void MenuCommandRegistrar::register_object_menu_commands()
{
    CommandBuilder builder(m_menu_manager, m_render_module);

    builder.push_path_level(MenuItemName::ObjectContextMenu)
        .append_item(
            MenuItemName::CopyObject,
            CommandName::CopyModelItems,
            [this]() { m_clipboard_interactor.copy(m_project_interactor.selected_project_id()); },
            UIItemCommandExtraOpts{
                .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                    Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                    Platform::KeyCode::C
                }},
                .enabled            = [this]() { return m_clipboard_interactor.can_copy(); }
            }
        )
        .append_item(
            MenuItemName::PasteObject,
            CommandName::PasteModelItems,
            [this]() { m_clipboard_interactor.paste(m_project_interactor.selected_project_id()); },
            UIItemCommandExtraOpts{
                .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                    Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                    Platform::KeyCode::V
                }},
                .enabled            = [this]() { return m_clipboard_interactor.can_paste(); }
            }
        )
        .append_item_from_command(MenuItemName::DeleteSelectedObject, CommandName::DeleteSelected)
        .append_separator()
        .append_item(
            MenuItemName::SetNumberOfInstances,
            CommandName::SetNumberOfInstances,
            [this]()
            {
                size_t init_cnt{1};
                const auto& selection = m_project_interactor.scene_interactor().object_selection();
                if (selection.only_single_object()) {
                    Domain::ModelObject* object =
                        m_project_interactor.selected_project().find_object_by_id(
                            selection.elements.front().object_id
                        );
                    init_cnt = object->instances.size();
                }

                m_render_module.get_user_number_and_process(
                    " ",
                    Biz::_u8L("Enter the number of copies:"),
                    Biz::_u8L("Copies of the selected object"),
                    init_cnt,
                    1,
                    1000,
                    [this](int number)
                    {
                        ASSERT(number > 0);
                        const Domain::ElementRefs new_instances =
                            m_project_interactor.scene_interactor()
                                .set_selected_objects_instance_count(number);
                        const Domain::BedRef target_bed = m_project_interactor.scene_interactor()
                                                              .bed_selection()
                                                              .last_selected_bed();

                        m_project_interactor.arrange_interactor().arrange_added_instances(
                            m_project_interactor.selected_project_id(),
                            new_instances,
                            target_bed,
                            UndoSnapshotType::SetNumberOfInstances
                        );
                    }
                );
            },
            UIItemCommandExtraOpts{
                .enabled =
                    [this]()
                {
                    const auto& selection =
                        m_project_interactor.scene_interactor().object_selection();
                    return !selection.empty()
                        && selection.mode == Biz::Scene::SelectionMode::Instance;
                }
            }
        )
        .append_item(
            MenuItemName::FillBedWithInstances,
            "fill-bed-with-instances",
            []() {},
            UIItemCommandExtraOpts{.todo = true}
        )
        .append_separator()
        // we need to force a registration of added menu items before call extra function
        .register_commands_and_clear_cache();

    register_object_menu_add_volume_commands();

    builder.append_separator()
        .append_item(
            MenuItemName::SetAsSeparateObject,
            "set-as-separate-object",
            [this]()
            {
                m_project_interactor.scene_interactor().extract_selected_instances();
                m_project_interactor.undo_provider().take_snapshot(
                    Biz::UndoSnapshotType::SetAsSeparateObject
                );
            },
            UIItemCommandExtraOpts{
                .enabled = [this]()
                { return m_project_interactor.scene_interactor().can_extract_selected_instances(); }
            }
        )
        .append_separator()
        .append_item(
            MenuItemName::ExportObject,
            CommandName::ExportAsStl,
            [this]() { this->export_selection_as_stl_obj(); },
            UIItemCommandExtraOpts{
                .enabled = [this]()
                { return m_project_interactor.scene_interactor().can_export_selection_as_mesh(); }
            }
        )
        .append_item(
            MenuItemName::ReplaceObject,
            CommandName::ReplaceWithStl,
            [this]() { replace_selected_volume_with_stl(); },
            UIItemCommandExtraOpts{
                .enabled = [this]()
                { return m_project_interactor.scene_interactor().can_replace_selected_volume(); }
            }
        )
        .append_item(
            MenuItemName::ReloadObject,
            CommandName::ReloadFromDisk,
            [this]() { reload_selection_from_disk(); },
            UIItemCommandExtraOpts{
                .enabled = [this]()
                { return m_project_interactor.scene_interactor().can_reload_selection_from_disk(); }
            }
        )
        .append_separator()
        .push_path_level(MenuItemName::SplitObject)
        .append_item(
            MenuItemName::SplitObjectToObjects,
            "split-object-to-objects",
            [this]()
            {
                m_project_interactor.scene_interactor().split_selection_to_objects();
                m_project_interactor.undo_provider().take_snapshot(
                    Biz::UndoSnapshotType::SplitToObjects
                );
            },
            UIItemCommandExtraOpts{
                .enabled = [this]()
                { return m_project_interactor.scene_interactor().can_split_selection_to_objects(); }
            }
        )
        .append_item(
            MenuItemName::SplitObjectToVolumes,
            CommandName::SplitToVolumes,
            [this]()
            {
                m_project_interactor.scene_interactor().split_selection_to_volumes();
                m_project_interactor.undo_provider().take_snapshot(
                    Biz::UndoSnapshotType::SplitToVolumes
                );
            },
            UIItemCommandExtraOpts{
                .enabled = [this]()
                { return m_project_interactor.scene_interactor().can_split_selection_to_volumes(); }
            }
        )
        .pop_path_level()
        .append_item(
            MenuItemName::ScaleToPrintVolume,
            "scale-to-print-volume",
            []() {},
            UIItemCommandExtraOpts{.todo = true}
        )
#if SLIC3R_ENABLE_WIN10_MESH_REPAIR
        .append_item(
            MenuItemName::FixObjectWithRepairAlgorithm,
            CommandName::FixWithRepairAlgorithm,
            [this]() { m_project_interactor.scene_interactor().repair_selected_object(); },
            UIItemCommandExtraOpts{
                .enabled = [this]()
                { return m_project_interactor.scene_interactor().can_repair_selected_object(); }
            }
        )
#endif
        .append_separator()
        .append_item(
            MenuItemName::InvalidateCutInfo,
            "invalidate-cut-info",
            [this]()
            {
                m_project_interactor.scene_interactor().invalidate_cut_info();
                m_project_interactor.undo_provider().take_snapshot(
                    Biz::UndoSnapshotType::InvalidateCutInfo
                );
            },
            UIItemCommandExtraOpts{
                .enabled = [this]()
                { return m_project_interactor.scene_interactor().can_invalidate_cut_info(); }
            }
        )
        .append_separator()
        .append_item(
            MenuItemName::PrintableObject,
            CommandName::SetAsPrintable,
            [this]()
            {
                if (const UIItemCommand* ui_command = dynamic_cast<const UIItemCommand*>(
                        &m_render_module.command(CommandName::SetAsPrintable)
                    ))
                {
                    m_project_interactor.scene_interactor().set_selected_instances_printable(
                        !ui_command->checked()
                    );
                    m_project_interactor.undo_provider().take_snapshot(
                        Biz::UndoSnapshotType::SetAsPrintable
                    );
                }
            },
            UIItemCommandExtraOpts{
                .checked = [this]()
                { return m_project_interactor.scene_interactor().selected_instances_printable(); },
            }
        );
}

void MenuCommandRegistrar::register_object_menu_add_volume_commands()
{
    using VolumeType = Domain::ModelVolumeType;
    using GeometryId = Scene::GeometryDataId;

    auto add_volume_shape = [this](GeometryId geometry_id, VolumeType volume_type)
    {
        m_project_interactor.scene_interactor().add_volume_to_active_object(
            m_data_factory->triangle_mesh(geometry_id)->triangles(),
            volume_type,
            geometry_name(geometry_id)
        );
    };

    auto add_volume_text = [this](VolumeType volume_type)
    {
        Plater::TextGizmo* text_gizmo = dynamic_cast<Plater::TextGizmo*>(
            m_render_module.tool_gizmo(Scene::ToolType::TextGizmo, Domain::PrinterTechnology::FFF)
        );
        ASSERT(text_gizmo);
        text_gizmo->set_next_volume_type(volume_type);
        m_render_module.command(CommandName::TextGizmo).execute();
    };

    CommandBuilder builder(m_menu_manager, m_render_module);

    builder.push_path_level(MenuItemName::ObjectContextMenu)
        .push_path_level(MenuItemName::AddVolume)
        .push_path_level(MenuItemName::SolidPartVolume)
        .append_item(
            MenuItemName::SolidPartVolumeShapeLoad,
            "add-volume-shape-load-solid",
            [this]()
            {
                load_volume(VolumeType::MODEL_PART);
                m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::AddVolume);
            }
        )
        .append_separator()
        .append_item(
            MenuItemName::SolidPartVolumeShapeCube,
            "add-volume-shape-cube-solid",
            [this, add_volume_shape]()
            {
                add_volume_shape(GeometryId::Cube, VolumeType::MODEL_PART);
                m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::AddVolumeCube);
            }
        )
        .append_item(
            MenuItemName::SolidPartVolumeShapeCylinder,
            "add-volume-shape-cylinder-solid",
            [this, add_volume_shape]()
            {
                add_volume_shape(GeometryId::Cylinder, VolumeType::MODEL_PART);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddVolumeCylinder
                );
            }
        )
        .append_item(
            MenuItemName::SolidPartVolumeShapeSphere,
            "add-volume-shape-sphere-solid",
            [this, add_volume_shape]()
            {
                add_volume_shape(GeometryId::Sphere, VolumeType::MODEL_PART);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddVolumeSphere
                );
            }
        )
        .append_separator()
        .append_item(
            MenuItemName::SolidPartVolumeShapeText,
            "add-volume-shape-text-solid",
            [this, add_volume_text]()
            {
                add_volume_text(VolumeType::MODEL_PART);
                m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::AddVolumeText);
            }
        )
        .append_item(
            MenuItemName::SolidPartVolumeShapeSVG,
            "add-volume-shape-svg-solid",
            [this]()
            {
                load_volume(VolumeType::MODEL_PART, Wildcards::TypeFlag::Svg);
                m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::AddVolumeSvg);
            }
        )
        .append_separator()
        .append_item(
            MenuItemName::SolidPartVolumeShapeFromGallery,
            "add-volume-shape-from-gallery-solid",
            [this]()
            {
                load_shape_from_gallery(VolumeType::MODEL_PART);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddVolumeGallery
                );
            },
            UIItemCommandExtraOpts{.todo = true}
        )
        .pop_path_level()
        .push_path_level(MenuItemName::NegativeVolume)
        .append_item(
            MenuItemName::NegativeVolumeShapeLoad,
            "add-volume-shape-load-negative",
            [this]()
            {
                load_volume(VolumeType::NEGATIVE_VOLUME);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddNegativeVolume
                );
            }
        )
        .append_separator()
        .append_item(
            MenuItemName::NegativeVolumeShapeCube,
            "add-volume-shape-cube-negative",
            [this, add_volume_shape]()
            {
                add_volume_shape(GeometryId::Cube, VolumeType::NEGATIVE_VOLUME);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddNegativeVolumeCube
                );
            }
        )
        .append_item(
            MenuItemName::NegativeVolumeShapeCylinder,
            "add-volume-shape-cylinder-negative",
            [this, add_volume_shape]()
            {
                add_volume_shape(GeometryId::Cylinder, VolumeType::NEGATIVE_VOLUME);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddNegativeVolumeCylinder
                );
            }
        )
        .append_item(
            MenuItemName::NegativeVolumeShapeSphere,
            "add-volume-shape-sphere-negative",
            [this, add_volume_shape]()
            {
                add_volume_shape(GeometryId::Sphere, VolumeType::NEGATIVE_VOLUME);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddNegativeVolumeSphere
                );
            }
        )
        .append_separator()
        .append_item(
            MenuItemName::NegativeVolumeShapeText,
            "add-volume-shape-text-negative",
            [this, add_volume_text]()
            {
                add_volume_text(VolumeType::NEGATIVE_VOLUME);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddNegativeVolumeText
                );
            }
        )
        .append_item(
            MenuItemName::NegativeVolumeShapeSVG,
            "add-volume-shape-svg-negative",
            [this]()
            {
                load_volume(VolumeType::NEGATIVE_VOLUME, Wildcards::TypeFlag::Svg);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddNegativeVolumeSvg
                );
            }
        )
        .append_separator()
        .append_item(
            MenuItemName::NegativeVolumeShapeFromGallery,
            "add-volume-shape-from-gallery-negative",
            [this]()
            {
                load_shape_from_gallery(VolumeType::NEGATIVE_VOLUME);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddNegativeVolumeGallery
                );
            },
            UIItemCommandExtraOpts{.todo = true}
        )
        .pop_path_level()
        .push_path_level(MenuItemName::ModifierVolume)
        .append_item(
            MenuItemName::ModifierVolumeShapeLoad,
            "add-volume-shape-load-modifier",
            [this]()
            {
                load_volume(VolumeType::PARAMETER_MODIFIER);

                m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::AddModifier);
            }
        )
        .append_separator()
        .append_item(
            MenuItemName::ModifierVolumeShapeCube,
            "add-volume-shape-cube-modifier",
            [this, add_volume_shape]()
            {
                add_volume_shape(GeometryId::Cube, VolumeType::PARAMETER_MODIFIER);

                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddModifierCube
                );
            }
        )
        .append_item(
            MenuItemName::ModifierVolumeShapeCylinder,
            "add-volume-shape-cylinder-modifier",
            [this, add_volume_shape]()
            {
                add_volume_shape(GeometryId::Cylinder, VolumeType::PARAMETER_MODIFIER);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddModifierCylinder
                );
            }
        )
        .append_item(
            MenuItemName::ModifierVolumeShapeSphere,
            "add-volume-shape-sphere-modifier",
            [this, add_volume_shape]()
            {
                add_volume_shape(GeometryId::Sphere, VolumeType::PARAMETER_MODIFIER);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddModifierSphere
                );
            }
        )
        .append_separator()
        .append_item(
            MenuItemName::ModifierVolumeShapeText,
            "add-volume-shape-text-modifier",
            [this, add_volume_text]()
            {
                add_volume_text(VolumeType::PARAMETER_MODIFIER);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddModifierText
                );
            }
        )
        .append_item(
            MenuItemName::ModifierVolumeShapeSVG,
            "add-volume-shape-svg-modifier",
            [this]()
            {
                load_volume(VolumeType::PARAMETER_MODIFIER, Wildcards::TypeFlag::Svg);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddModifierSvg
                );
            }
        )
        .append_separator()
        .append_item(
            MenuItemName::ModifierVolumeShapeFromGallery,
            "add-volume-shape-from-gallery-modifier",
            [this]()
            {
                load_shape_from_gallery(VolumeType::PARAMETER_MODIFIER);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddModifierGallery
                );
            },
            UIItemCommandExtraOpts{.todo = true}
        )
        .pop_path_level()
        .push_path_level(MenuItemName::SupportBlocker)
        .append_item(
            MenuItemName::SupportBlockerShapeLoad,
            "add-volume-shape-load-support-blocker",
            [this]()
            {
                load_volume(VolumeType::SUPPORT_BLOCKER);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddSupportBlocker
                );
            }
        )
        .append_separator()
        .append_item(
            MenuItemName::SupportBlockerShapeCube,
            "add-volume-shape-cube-support-blocker",
            [this, add_volume_shape]()
            {
                add_volume_shape(GeometryId::Cube, VolumeType::SUPPORT_BLOCKER);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddSupportBlockerCube
                );
            }
        )
        .append_item(
            MenuItemName::SupportBlockerShapeCylinder,
            "add-volume-shape-cylinder-support-blocker",
            [this, add_volume_shape]()
            {
                add_volume_shape(GeometryId::Cylinder, VolumeType::SUPPORT_BLOCKER);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddSupportBlockerCube
                );
            }
        )
        .append_item(
            MenuItemName::SupportBlockerShapeSphere,
            "add-volume-shape-sphere-support-blocker",
            [this, add_volume_shape]()
            {
                add_volume_shape(GeometryId::Sphere, VolumeType::SUPPORT_BLOCKER);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddSupportBlockerSphere
                );
            }
        )
        .append_separator()
        .append_item(
            MenuItemName::SupportBlockerShapeFromGallery,
            "add-volume-shape-from-gallery-support-blocker",
            [this]()
            {
                load_shape_from_gallery(VolumeType::SUPPORT_BLOCKER);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddSupportBlockerGallery
                );
            },
            UIItemCommandExtraOpts{.todo = true}
        )
        .pop_path_level()
        .push_path_level(MenuItemName::SupportModifier)
        .append_item(
            MenuItemName::SupportModifierShapeLoad,
            "add-volume-shape-load-support-modifier",
            [this]()
            {
                load_volume(VolumeType::SUPPORT_ENFORCER);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddSupportModifier
                );
            }
        )
        .append_separator()
        .append_item(
            MenuItemName::SupportModifierShapeCube,
            "add-volume-shape-cube-support-modifier",
            [this, add_volume_shape]()
            {
                add_volume_shape(GeometryId::Cube, VolumeType::SUPPORT_ENFORCER);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddSupportModifierCube
                );
            }
        )
        .append_item(
            MenuItemName::SupportModifierShapeCylinder,
            "add-volume-shape-cylinder-support-modifier",
            [this, add_volume_shape]()
            {
                add_volume_shape(GeometryId::Cylinder, VolumeType::SUPPORT_ENFORCER);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddSupportModifierCylinder
                );
            }
        )
        .append_item(
            MenuItemName::SupportModifierShapeSphere,
            "add-volume-shape-sphere-support-modifier",
            [this, add_volume_shape]()
            {
                add_volume_shape(GeometryId::Sphere, VolumeType::SUPPORT_ENFORCER);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddSupportModifierSphere
                );
            }
        )
        .append_separator()
        .append_item(
            MenuItemName::SupportModifierShapeFromGallery,
            "add-volume-shape-from-gallery-support-modifier",
            [this]()
            {
                load_shape_from_gallery(VolumeType::SUPPORT_ENFORCER);
                m_project_interactor.undo_provider().take_snapshot(
                    UndoSnapshotType::AddSupportModifierGallery
                );
            },
            UIItemCommandExtraOpts{.todo = true}
        );
}

void MenuCommandRegistrar::register_svg_or_text_volume_menu_commands()
{
    CommandBuilder builder(m_menu_manager, m_render_module);
    builder.push_path_level(MenuItemName::SvgOrTextContextMenu)
        .append_item(
            MenuItemName::EditSvgOrText,
            "edit-svg-or-text",
            []() {},
            UIItemCommandExtraOpts{.todo = true}
        )
        .append_item_from_command(
            MenuItemName::DeleteSelectedSvgOrText,
            CommandName::DeleteSelected
        );
}

void MenuCommandRegistrar::register_volume_menu_commands()
{
    CommandBuilder builder(m_menu_manager, m_render_module);

    builder.push_path_level(MenuItemName::VolumeContextMenu)
        .append_item_from_command(MenuItemName::DeleteSelectedVolume, CommandName::DeleteSelected)
        .append_item_from_command(MenuItemName::CopyVolume, CommandName::CopyModelItems)
        .append_separator()
        .append_item_from_command(MenuItemName::ExportVolume, CommandName::ExportAsStl)
        .append_item_from_command(MenuItemName::ReplaceVolume, CommandName::ReplaceWithStl)
        .append_item_from_command(MenuItemName::ReloadVolume, CommandName::ReloadFromDisk)
        .append_separator()
        .append_item_from_command(MenuItemName::SplitVolume, CommandName::SplitToVolumes)
#if SLIC3R_ENABLE_WIN10_MESH_REPAIR
        .append_item_from_command(
            MenuItemName::FixVolumeWithRepairAlgorithm,
            CommandName::FixWithRepairAlgorithm
        )
#endif
    ;
}

void MenuCommandRegistrar::register_multi_object_menu_commands()
{
    CommandBuilder builder(m_menu_manager, m_render_module);
    builder.push_path_level(MenuItemName::MultiObjectsContextMenu)
        .append_item_from_command(MenuItemName::CopyMultiObjects, CommandName::CopyModelItems)
        .append_item_from_command(
            MenuItemName::DeleteSelectedMultiObjects,
            CommandName::DeleteSelected
        )
        .append_separator()
        .append_item_from_command(
            MenuItemName::SetMultiObjectsNumberOfInstances,
            CommandName::SetNumberOfInstances
        )
        .append_separator()
#if SLIC3R_ENABLE_WIN10_MESH_REPAIR
        .append_item_from_command(
            MenuItemName::FixMultiObjectWithRepairAlgorithm,
            CommandName::FixWithRepairAlgorithm
        )
#endif
        .append_item(
            MenuItemName::MergeMultiObjects,
            "merge-multi-objects",
            [this]()
            {
                m_project_interactor.scene_interactor().merge_selection_into_object();
                m_project_interactor.undo_provider().take_snapshot(
                    Biz::UndoSnapshotType::MergeToOneObject
                );
            },
            UIItemCommandExtraOpts{
                .enabled =
                    [this]()
                {
                    return m_project_interactor.scene_interactor()
                        .can_merge_selection_into_object();
                }
            }
        )
        .append_separator()
        .append_item_from_command(MenuItemName::PrintableMultiObjects, CommandName::SetAsPrintable);
}

boost::filesystem::path MenuCommandRegistrar::default_dialog_folder() const
{
    return m_project_interactor.project_dir(
        m_project_interactor.selected_project_id(),
        AppServices::instance().app_config().get<std::string>("last_used_directory")
    );
}

void MenuCommandRegistrar::load_project()
{
    IDialogManager::FileCallback callback =
        [this](bool success, const std::vector<boost::filesystem::path>& file_paths)
    {
        if (success) {
            m_project_interactor.load_project(file_paths.front());
        }
    };

    auto& dlg_manager = AppServices::instance().dialog_manager();
    dlg_manager.show_file_dialog(
        FileDialogType::Open,
        _u8L("Open Project"),
        this->default_dialog_folder(),
        "",
        Wildcards::generate_wildcards(Wildcards::TypeFlag::Project3mf),
        callback
    );
}

void MenuCommandRegistrar::install_plugin(Lua::PluginSystem& plugin_system)
{
    IDialogManager::FileCallback callback =
        [&plugin_system](bool success, const std::vector<boost::filesystem::path>& file_paths)
    {
        if (success) {
            // as on Win/Linux this callback is triggered within render loop showing menu items,
            // we are going to recreate, we need to dispatch this action outside render loop
            // to prevent rendering disposed menu items
            Biz::Platform::PlatformServices::instance()
                .main_thread_dispatcher()
                .dispatch_on_main_thread([&plugin_system, file_path = file_paths.front().string()]
                                         { plugin_system.install(file_path); });
        }
    };

    auto& dlg_manager = AppServices::instance().dialog_manager();
    dlg_manager.show_file_dialog(
        FileDialogType::Open,
        _u8L("Install Plugin"),
        m_project_interactor.project_dir(
            m_project_interactor.selected_project_id(),
            AppServices::instance().app_config().get<std::string>("last_used_directory")
        ),
        "",
        Wildcards::generate_wildcards(Wildcards::TypeFlag::Zip),
        callback
    );

}

void MenuCommandRegistrar::load_object(Wildcards::TypeFlag specific_type)
{
    IDialogManager::FileCallback callback =
        [this](bool success, const std::vector<boost::filesystem::path>& file_paths)
    {
        if (success) {
            m_project_interactor.load_models_to_project(file_paths);
            if (m_render_module.has_command(CommandName::SvgGizmo)
                && m_render_module.command(CommandName::SvgGizmo).enabled())
            {
                // open SvgGizmo, when svg volume is selected(was imported)
                m_render_module.command(CommandName::SvgGizmo).execute();
            }
            m_navigator.navigate_to_module_type(App::Render::ModuleType::Plater);
        }
    };

    auto& dlg_manager = App::AppServices::instance().dialog_manager();
    dlg_manager.show_file_dialog(
        FileDialogType::OpenMultiple,
        _u8L("Import File"),
        this->default_dialog_folder(),
        "",
        specific_type == Wildcards::TypeFlag::None ? Wildcards::generate_wildcards(
                                                         import_file_types,
                                                         Wildcards::TypeFlag::AllImportFiles
                                                     ) :
                                                     Wildcards::generate_wildcards(specific_type),
        callback
    );
}

namespace {
// open SvgGizmo, when svg volume is just added into scene
// TODO: solve multiple svgs at once
std::optional<Scene::TrafoGuess> get_svg_guess_tr(
    const std::vector<boost::filesystem::path>& file_paths,
    const Biz::ProjectInteractor& project_interactor,
    const Scene::ISceneProvider& scene_provider
)
{
    const Biz::Scene::ObjectSelection& selection =
        project_interactor.scene_interactor().object_selection();
    const Domain::Project& project = project_interactor.selected_project();
    const Scene::Scene& scene      = scene_provider.scene();
    return Scene::guess_volume_transformation(selection.elements, project, scene);
}

static void open_svg_gizmo(
    const std::optional<Scene::TrafoGuess>& volume_tr,
    Biz::ProjectInteractor& project_interactor,
    Platform::AbstractRenderModule& render_module
)
{
    const Biz::Scene::ObjectSelection& selection =
        project_interactor.scene_interactor().object_selection();
    if (selection.elements.size() != 1)
        return;

    const Domain::ElementRef& element = selection.elements.front();
    const Domain::Project& project    = project_interactor.selected_project();
    const Domain::ModelVolume* volume = nullptr;
    if (element.has_volume()) {
        volume = project.find_volume_by_id(element.object_id, element.volume_id);
    } else {
        const Domain::ModelObject* object = project.find_object_by_id(element.object_id);
        if (object != nullptr && object->volumes.size() == 1)
            volume = object->volumes.front();
    }
    if (volume == nullptr)
        return;

    const Domain::ModelInstance* instance =
        project.find_instance_by_id(element.object_id, element.instance_id);
    if (instance == nullptr || instance != volume_tr->instance)
        return;
    const Domain::Transform3d& instance_tr = instance->get_matrix();
    Domain::Transform3d world_relative     = instance_tr
        * volume->get_matrix().inverse()
        * volume_tr->transformation
        * instance_tr.inverse();
    project_interactor.scene_interactor().transform_selection(world_relative.matrix());

    if (render_module.command(CommandName::SvgGizmo).enabled()) {
        render_module.command(CommandName::SvgGizmo).execute();
    }
}
} // namespace

void
MenuCommandRegistrar::load_volume(Domain::ModelVolumeType type, Wildcards::TypeFlag specific_type)
{
    IDialogManager::FileCallback callback =
        [this, type](bool success, const std::vector<boost::filesystem::path>& file_paths)
    {
        if (!success || file_paths.empty())
            return;

        bool svg_loading = file_paths.size() == 1 && file_paths.back().extension() == ".svg";

        // NOTE: Need to ray cast into scene before import
        auto svg_transform = svg_loading ?
            get_svg_guess_tr(file_paths, m_project_interactor, *m_scene_provider) :
            std::nullopt;
        Biz::FileLoadingLogic::import_volumes_into_selected_object(
            file_paths,
            type,
            m_project_interactor.scene_interactor(),
            &AppServices::instance().dialog_manager()
        );

        // open SvgGizmo, when svg volume is selected(was imported)
        if (svg_loading) {
            open_svg_gizmo(svg_transform, m_project_interactor, m_render_module);
        }
    };

    auto& dlg_manager = AppServices::instance().dialog_manager();
    dlg_manager.show_file_dialog(
        FileDialogType::OpenMultiple,
        _u8L("Import File"),
        this->default_dialog_folder(),
        "",
        specific_type == Wildcards::TypeFlag::None ? Wildcards::generate_wildcards(
                                                         import_file_types,
                                                         Wildcards::TypeFlag::AllImportFiles
                                                     ) :
                                                     Wildcards::generate_wildcards(specific_type),
        callback
    );
}

void MenuCommandRegistrar::load_shape_from_gallery(Domain::ModelVolumeType type) {}

void MenuCommandRegistrar::export_selection_as_stl_obj()
{
    IDialogManager::FileCallback callback =
        [this](const bool success, const std::vector<boost::filesystem::path>& file_paths)
    {
        if (!success || file_paths.empty()) {
            return;
        }

        MeshExportLogic::export_selection(
            file_paths.front(),
            m_project_interactor.scene_interactor(),
            &AppServices::instance().dialog_manager()
        );
    };

    AppServices::instance().dialog_manager().show_file_dialog(
        FileDialogType::Save,
        _u8L("Export as STL/OBJ"),
        this->default_dialog_folder(),
        MeshExportLogic::proposed_export_file_name(m_project_interactor.scene_interactor()),
        Wildcards::generate_wildcards(
            Wildcards::TypeFlag::Stl | Wildcards::TypeFlag::Obj,
            Wildcards::TypeFlag::Stl
        ),
        callback
    );
}

void MenuCommandRegistrar::replace_selected_volume_with_stl()
{
    IDialogManager::FileCallback callback =
        [this](const bool success, const std::vector<boost::filesystem::path>& file_paths)
    {
        if (!success || file_paths.empty()) {
            return;
        }

        const tl::expected<void, std::string> replace_result =
            VolumeReloadLogic::replace_selected_volume(
                file_paths.front(),
                m_project_interactor.scene_interactor(),
                &AppServices::instance().dialog_manager()
            );

        if (!replace_result) {
            AppServices::instance().dialog_manager().show_warning_dialog(
                replace_result.error(),
                _u8L("Error during replace")
            );

            return;
        }

        m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::ReplaceWithStl);
    };

    AppServices::instance().dialog_manager().show_file_dialog(
        FileDialogType::Open,
        _u8L("Select the new file:"),
        this->default_dialog_folder(),
        VolumeReloadLogic::proposed_replace_file_name(m_project_interactor.scene_interactor()),
        Wildcards::generate_wildcards(model_file_types, Wildcards::TypeFlag::AllImportFiles),
        callback
    );
}

void MenuCommandRegistrar::reload_selection_from_disk()
{
    IDialogManager& dialog_manager = AppServices::instance().dialog_manager();

    VolumeReloadLogic::MissingFileResolver missing_file_resolver =
        [this, &dialog_manager](
            const boost::filesystem::path& missing_file
        ) -> std::optional<boost::filesystem::path>
    {
        std::optional<boost::filesystem::path> selected_path;
        dialog_manager.show_file_dialog(
            FileDialogType::Open,
            _u8L("Please select the file to reload")
                + " ("
                + missing_file.filename().string()
                + "):",
            default_dialog_folder(),
            missing_file.filename().string(),
            Wildcards::generate_wildcards(model_file_types, Wildcards::TypeFlag::AllImportFiles),
            [&selected_path](bool success, const std::vector<boost::filesystem::path>& file_paths)
            {
                if (success && !file_paths.empty()) {
                    selected_path = file_paths.front();
                }
            }
        );
        return selected_path;
    };

    const VolumeReloadLogic::ReloadFromDiskResult reload_result =
        VolumeReloadLogic::reload_selection(
            missing_file_resolver,
            m_project_interactor.scene_interactor(),
            &dialog_manager
        );

    if (!reload_result.failed_volume_names.empty()) {
        std::string message = _u8L("Unable to reload:") + "\n";
        for (const std::string& failed_volume_name : reload_result.failed_volume_names) {
            message += failed_volume_name + "\n";
        }

        dialog_manager.show_warning_dialog(message, _u8L("Error during reload"));
    }

    if (reload_result.changed_volume_count > 0) {
        m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::ReloadFromDisk);
    }
}

void MenuCommandRegistrar::register_main_menu_edit_commands()
{
    m_menu_manager
        // Menu -> Edit -> Select All
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Edit, MenuItemName::SelectAll},
            std::make_unique<UIItemCommand>(
                CommandName::SelectAll,
                [this]()
                {
                    Biz::Scene::ObjectSelection selection{Biz::Scene::SelectionMode::Instance, {}};

                    const Domain::Model& model = m_project_interactor.selected_project().model();
                    for (const auto object : model.objects) {
                        for (const auto instance : object->instances) {
                            selection.elements.push_back({object->id().id, instance->id().id});
                        }
                    }
                    m_project_interactor.scene_interactor().set_object_selection(selection);
                },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::A

                    }}
                }
            )
        )
        // Menu -> Edit -> Deselect All
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Edit, MenuItemName::DeselectAll},
            std::make_unique<UIItemCommand>(
                CommandName::ClearSelection,
                [this]()
                {
                    auto& scene_interactor = m_project_interactor.scene_interactor();
                    if (!scene_interactor.object_selection().empty())
                        scene_interactor.clear_object_selection();
                },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Escape}
                        },
                    .enabled =
                        [this]()
                    {
                        const bool is_selection_empty{
                            m_project_interactor.scene_interactor().object_selection().empty()
                        };

                        auto plater_render_module{
                            dynamic_cast<Plater::PlaterRenderModule*>(&m_render_module)
                        };
                        if (is_selection_empty || !plater_render_module) {
                            return false;
                        }
                        return plater_render_module->gizmo_controller().current_tool_type()
                            == Scene::ToolType::None;
                    }
                }
            )
        )
        // Menu -> Edit -> Separator
        .register_menu_separator_item({MenuItemName::MainMenu, MenuItemName::Edit})
        // Menu -> Edit -> Delete Selected
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Edit, MenuItemName::DeleteSelected},
            std::make_unique<UIItemCommand>(
                CommandName::DeleteSelected,
                [this]()
                {
                    if (m_project_interactor.scene_interactor().object_selection().empty())
                        return;

                    std::optional<std::string> last_solid_part_name =
                        m_project_interactor.scene_interactor().delete_selected_elements();

                    if (last_solid_part_name) {
                        // Show warning dialog
                        auto& dlg_manager = App::AppServices::instance().dialog_manager();
                        dlg_manager.show_warning_dialog(
                            fmt::vformat(
                                _u8L(
                                    "Part {} could not be deleted from the object,\n"
                                    "as removing the last solid part is not permitted."
                                ),
                                fmt::make_format_args(last_solid_part_name.value())
                            ) + "\n",
                            _u8L("Delete selection")
                        );
                    }
                    m_project_interactor.undo_provider().take_snapshot(
                        UndoSnapshotType::DeleteSelection
                    );
                },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Backspace},
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Delete}
                        },
                    .enabled =
                        [this]()
                    {
                        const Biz::Scene::ObjectSelection& object_selection =
                            m_project_interactor.scene_interactor().object_selection();
                        return !object_selection.empty() && !object_selection.contains_wipe_tower();
                    }
                }
            )
        )
#ifdef SHOW_NOT_IMPLEMENTED_ITEMS
        // Menu -> Edit -> Separator
        .register_menu_separator_item({MenuItemName::MainMenu, MenuItemName::Edit})
        // Menu -> Edit -> Undo
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Edit, MenuItemName::Undo},
            std::make_unique<UIItemCommand>(
                CommandName::Undo,
                [this]()
                {
                    // TODO: Implement undo functionality
                },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::Z
                    }}
                }
            )
        )
        // Menu -> Edit -> Redo
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Edit, MenuItemName::Redo},
            std::make_unique<UIItemCommand>(
                CommandName::Redo,
                [this]()
                {
                    // TODO: Implement redo functionality
                },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::Y
                    }}
                }
            )
        )
#endif // SHOW_NOT_IMPLEMENTED_ITEMS
        // Menu -> Edit -> Separator
        .register_menu_separator_item({MenuItemName::MainMenu, MenuItemName::Edit})
        // Menu -> Edit -> Copy
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Edit, MenuItemName::Copy},
            std::make_unique<UIItemCommand>(
                CommandName::Copy,
                [this]()
                { m_clipboard_interactor.copy(m_project_interactor.selected_project_id()); },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::C
                    }},
                    .enabled            = [this]() { return m_clipboard_interactor.can_copy(); }
                }
            )
        )
        // Menu -> Edit -> Paste
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Edit, MenuItemName::Paste},
            std::make_unique<UIItemCommand>(
                CommandName::Paste,
                [this]()
                { m_clipboard_interactor.paste(m_project_interactor.selected_project_id()); },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::V
                    }},
                    .enabled            = [this]() { return m_clipboard_interactor.can_paste(); }
                }
            )
        )
#ifdef SHOW_NOT_IMPLEMENTED_ITEMS
        // Menu -> Edit -> Separator
        .register_menu_separator_item({MenuItemName::MainMenu, MenuItemName::Edit})
        // Menu -> Edit -> Reload From Disk
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Edit, MenuItemName::ReloadFromDisk},
            std::make_unique<UIItemCommand>(
                CommandName::ReloadFromDisk,
                [this]()
                {
                    // TODO: Implement reload from disk functionality
                }
            )
        )
#endif // SHOW_NOT_IMPLEMENTED_ITEMS
        // Menu -> Edit -> Search
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Edit, MenuItemName::Search},
            std::make_unique<UIItemCommand>(
                CommandName::Search,
                [this]() { m_navigator.request_search(); },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::F
                    }}
                }
            )
        )
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Edit, MenuItemName::Arrange},
            std::make_unique<UIItemCommand>(
                CommandName::Arrange,
                [this]() {
                    const auto it{m_render_module.gizmo_commands().find("arrange-gizmo-arrange")};
                    if (it != m_render_module.gizmo_commands().end()) {
                        it->second->execute();
                    }
                },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers{},
                        Platform::KeyCode::A
                    }}
                }
            )
        )
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Edit, MenuItemName::ArrangeSelection},
            std::make_unique<UIItemCommand>(
                CommandName::ArrangeSelection,
                [this]() {
                    const auto it{m_render_module.gizmo_commands().find("arrange-gizmo-arrange-selection")};
                    if (it != m_render_module.gizmo_commands().end()) {
                        it->second->execute();
                    }
                },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Shift),
                        Platform::KeyCode::A
                    }}
                }
            )
        );
}

void MenuCommandRegistrar::register_main_menu_view_commands()
{
    m_menu_manager
#ifdef SHOW_NOT_IMPLEMENTED_ITEMS
        // Menu -> View -> Show Label
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::View, MenuItemName::ShowLabel},
            std::make_unique<UIItemCommand>(
                CommandName::ShowLabel,
                [this]()
                {
                    // TODO: Implement show label functionality
                },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::E}
                        }
                }
            )
        )
#endif
        // Menu -> View -> ZoomIn
        .register_menu_item_from_command(
            {MenuItemName::MainMenu, MenuItemName::View, MenuItemName::ZoomIn},
            m_render_module.command(CommandName::ZoomIn)
        )
        // Menu -> View -> ZoomOut
        .register_menu_item_from_command(
            {MenuItemName::MainMenu, MenuItemName::View, MenuItemName::ZoomOut},
            m_render_module.command(CommandName::ZoomOut)
        )
        // Menu -> View -> Change Camera Type
        .register_menu_item_from_command(
            {MenuItemName::MainMenu, MenuItemName::View, MenuItemName::CameraProjectionSwitch},
            m_render_module.command(CommandName::CameraProjectionSwitch)
        )
        // Menu -> View -> LookAtActiveBed
        .register_menu_item_from_command(
            {MenuItemName::MainMenu, MenuItemName::View, MenuItemName::LookAtActiveBed},
            m_render_module.command(CommandName::LookAtActiveBed)
        )
        // Menu -> View -> CameraDefaultView
        .register_menu_item_from_command(
            {MenuItemName::MainMenu, MenuItemName::View, MenuItemName::CameraDefaultView},
            m_render_module.command(CommandName::CameraDefaultView)
        );
#ifndef __APPLE__
    // OSX adds its own menu item to toggle fullscreen.
    if (m_navigator.has_fullscreen()) {
        // Menu -> View -> Separator
        m_menu_manager
            .register_menu_separator_item({MenuItemName::MainMenu, MenuItemName::View})
            // Menu -> View -> Full Screen
            .register_menu_item(
                {MenuItemName::MainMenu, MenuItemName::View, MenuItemName::FullScreen},
                std::make_unique<UIItemCommand>(
                    CommandName::FullScreen,
                    [this]() { m_navigator.set_fullscreen(!m_navigator.is_fullscreen()); },
                    UIItemCommandExtraOpts{
                        .keyboard_shortcuts =
                            Platform::KeyboardShortcuts{
                                Platform::KeyboardShortcut{0, Platform::KeyCode::F11}
                            },
                        .checked = [this]() { return m_navigator.is_fullscreen(); }
                    }
                )
            );
    }
#endif // __APPLE__
}

void MenuCommandRegistrar::register_main_menu_config_commands()
{
#ifdef SHOW_NOT_IMPLEMENTED_ITEMS
    m_menu_manager
        // Menu -> Configuration -> Configuration Wizard
        .register_menu_item(
            {MenuItemName::MainMenu,
             MenuItemName::Configuration,
             MenuItemName::ConfigurationWizard},
            std::make_unique<UIItemCommand>(
                CommandName::ConfigurationWizard,
                [this]()
                {
                    // TODO: Implement configuration wizard functionality
                }
            )
        )
        // Menu -> Configuration -> Check Config Updates
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Configuration, MenuItemName::CheckConfigUpdates},
            std::make_unique<UIItemCommand>(
                CommandName::CheckConfigUpdates,
                [this]()
                {
                    // TODO: Implement check config updates functionality
                }
            )
        )
        // Menu -> Configuration -> Check App Updates
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Configuration, MenuItemName::CheckAppUpdates},
            std::make_unique<UIItemCommand>(
                CommandName::CheckAppUpdates,
                [this]()
                {
                    // TODO: Implement check app updates functionality
                }
            )
        )
        // Menu -> Configuration -> Flash Printer Firmware
        .register_menu_item(
            {MenuItemName::MainMenu,
             MenuItemName::Configuration,
             MenuItemName::FlashPrinterFirmware},
            std::make_unique<UIItemCommand>(
                CommandName::FlashPrinterFirmware,
                [this]()
                {
                    // TODO: Implement flash printer firmware functionality
                }
            )
        );
#endif
    m_menu_manager.register_menu_item(
        {MenuItemName::MainMenu,
         MenuItemName::Configuration,
         MenuItemName::PresetReposManagement},
        std::make_unique<UIItemCommand>(
            CommandName::PresetReposManagement,
            [this]() { m_navigator.set_modal_dialog(ModalDialog::PresetUpdater); }
        )
    );
}

void MenuCommandRegistrar::register_main_menu_help_commands()
{
    m_menu_manager
        // Menu -> Help -> Prusa Website
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Help, MenuItemName::PSWebsite},
            std::make_unique<UIItemCommand>(
                CommandName::PSWebsite,
                [this]()
                {
                    open_browser(
                        OpenBrowserParams{
                            .url              = "https://www.prusa3d.com/slicerweb",
                            .is_localized_url = true
                        }
                    );
                }
            )
        )
        // Menu -> Help -> Samples
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Help, MenuItemName::Samples},
            std::make_unique<UIItemCommand>(
                CommandName::Samples,
                [this]()
                {
                    open_browser(
                        OpenBrowserParams{
                            .url = "https://help.prusa3d.com/article/sample-g-codes_529630",
                            .force_remember_choice = false
                        }
                    );
                }
            )
        )
        // Menu -> Help -> Releases
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Help, MenuItemName::Releases},
            std::make_unique<UIItemCommand>(
                CommandName::Releases,
                [this]()
                {
                    open_browser(
                        OpenBrowserParams{
                            .url = "https://github.com/prusa3d/PrusaSlicer/releases",
                            .force_remember_choice = false
                        }
                    );
                }
            )
        )
        // Menu -> Help -> Separator
        .register_menu_separator_item({MenuItemName::MainMenu, MenuItemName::Help})
#ifdef SHOW_NOT_IMPLEMENTED_ITEMS
        // Menu -> Help -> System Info
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Help, MenuItemName::SystemInfo},
            std::make_unique<UIItemCommand>(
                CommandName::SystemInfo,
                [this]()
                {
                    // TODO: Implement system info functionality
                }
            )
        )
#endif
        // Menu -> Help -> Config Folder
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Help, MenuItemName::ConfigFolder},
            std::make_unique<UIItemCommand>(
                CommandName::ConfigFolder,
                []()
                {
                    AppServices::instance().file_explorer_handler().open_datadir_folder();
                }
            )
        )
        // Menu -> Help -> Report An Issue
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Help, MenuItemName::ReportAnIssue},
            std::make_unique<UIItemCommand>(
                CommandName::ReportAnIssue,
                [this]()
                {
                    open_browser(
                        OpenBrowserParams{
                            .url                   = "https://github.com/prusa3d/slic3r/issues/new/choose",
                            .force_remember_choice = false
                        }
                    );
                }
            )
        )
#ifdef SHOW_NOT_IMPLEMENTED_ITEMS
        // Menu -> Help -> About
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Help, MenuItemName::About},
            std::make_unique<UIItemCommand>(
                CommandName::About,
                [this]()
                {
                    // TODO: Implement about functionality
                }
            )
        )
        // Menu -> Help -> Tip Of The Day
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Help, MenuItemName::TipOfTheDay},
            std::make_unique<UIItemCommand>(
                CommandName::TipOfTheDay,
                [this]()
                {
                    // TODO: Implement tip of the day functionality
                }
            )
        )
        // Menu -> Help -> Separator
        .register_menu_separator_item({MenuItemName::MainMenu, MenuItemName::Help})
        // Menu -> Help -> Keyboard Shortcuts
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Help, MenuItemName::KeyboardShortcutsDialog},
            std::make_unique<UIItemCommand>(
                CommandName::KeyboardShortcutsDialog,
                [this]()
                {
                    // TODO: Implement keyboard shortcuts functionality
                }
            )
        )
#endif
        ;
}

void MenuCommandRegistrar::register_undo_redo_commands()
{
    m_menu_manager
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                CommandName::Undo,
                [&]()
                {
                    m_project_interactor.undo_provider().select_snapshot(
                        Biz::UndoSnapshotSelection::Prev{}
                    );
                },
                Platform::FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{
                                Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                                Platform::KeyCode::Z
                            },
                        },

                    .enabled =
                        [&]()
                    {
                        if (auto plater_render_module{
                                dynamic_cast<Plater::PlaterRenderModule*>(&m_render_module)
                            })
                        {
                            return m_project_interactor.undo_provider().is_undo_possible();
                        }
                        return false;
                    }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                CommandName::Redo,
                [&]()
                {
                    m_project_interactor.undo_provider().select_snapshot(
                        Biz::UndoSnapshotSelection::Next{}
                    );
                },
                Platform::FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{
                                Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                                Platform::KeyCode::Y
                            },
                        },
                    .enabled = [&]()
                    {
                        if (auto plater_render_module{
                                dynamic_cast<Plater::PlaterRenderModule*>(&m_render_module)
                            })
                        {
                            return m_project_interactor.undo_provider().is_redo_possible();
                        }
                        return false;
                    }
                }
            )
        );
}

void MenuCommandRegistrar::register_main_menu_commands(Lua::PluginSystem* plugin_system)
{
    register_main_menu_edit_commands();
    register_main_menu_view_commands();
    if (plugin_system != nullptr) {
        register_main_menu_plugin_commands(*plugin_system);
    }

    m_menu_manager
        // Menu -> Separator
        .register_menu_separator_item({MenuItemName::MainMenu})
        // Menu -> Preferences
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Preferences},
            std::make_unique<UIItemCommand>(
                CommandName::Preferences,
                [this]() { m_navigator.set_modal_dialog(ModalDialog::Preferences); },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
#ifdef __APPLE__
                        Platform::KeyCode::Comma
#else
                        Platform::KeyCode::P
#endif
                    }}
                }
            )
        );

    register_main_menu_config_commands();

    m_menu_manager
        // Menu -> Separator
        .register_menu_separator_item({MenuItemName::MainMenu});

    register_main_menu_help_commands();

    m_menu_manager
        // Menu -> Exit
        .register_menu_item(
            {MenuItemName::MainMenu, MenuItemName::Exit},
            std::make_unique<UIItemCommand>(
                CommandName::Exit,
                [this]() { m_navigator.close_application(); },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::Q
                    }}
                }
            )
        );
}

void MenuCommandRegistrar::register_file_menu_import_commands()
{
    m_menu_manager
        // File -> Import -> Import Geometry
        .register_menu_item(
            {MenuItemName::FileMenu, MenuItemName::Import, MenuItemName::ImportGeometry},
            std::make_unique<UIItemCommand>(
                CommandName::ImportGeometry,
                [this]()
                {
                    // The undo snapshot is taken inside load_models_to_project().
                    load_object();
                },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::I
                    }},
                    .enabled = [&]()
                    {
                        auto plater_render_module{
                                dynamic_cast<Plater::PlaterRenderModule*>(&m_render_module)
                            };
                        return plater_render_module != nullptr;
                    }
                }
            )
        );
}

void MenuCommandRegistrar::register_file_menu_export_commands()
{
    m_menu_manager
        // File -> Export -> Export G-code
        .register_menu_item(
            {MenuItemName::FileMenu, MenuItemName::Export, MenuItemName::ExportGcode},
            std::make_unique<UIItemCommand>(
                CommandName::ExportGcode,
                ExportActions::export_gcode(m_project_interactor),
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::G
                    }},
                    .enabled = [this]() { return ExportActions::can_export(m_project_interactor); }
                }
            )
        )
        // File -> Export -> Send G-code
        .register_menu_item(
            {MenuItemName::FileMenu, MenuItemName::Export, MenuItemName::SendGcode},
            std::make_unique<UIItemCommand>(
                CommandName::SendGcode,
                ExportActions::send_gcode_to_connect(m_project_interactor),
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl)
                            | Platform::KeyModifiers(Platform::KeyModifier::Shift),
                        Platform::KeyCode::G
                    }},
                    .enabled =
                        [this]()
                    {
                        return ExportActions::can_export(m_project_interactor)
                            && m_project_interactor.user_account_interactor().is_logged_in();
                    }
                }
            )
        )
        // File -> Export -> Export G-code to SD Card / Flash Drive
        .register_menu_item(
            {MenuItemName::FileMenu, MenuItemName::Export, MenuItemName::ExportGcodeToFlash},
            std::make_unique<UIItemCommand>(
                CommandName::ExportGcodeToFlash,
                ExportActions::export_gcode_to_flash(m_project_interactor),
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::U
                    }},
                    .enabled            = [this]()
                    {
                        return ExportActions::can_export(m_project_interactor)
                            && m_project_interactor.removable_drive_service()
                                   .has_removable_drives();
                    }
                }
            )
        );
}

void MenuCommandRegistrar::register_file_menu_commands()
{
    m_menu_manager
        // File -> New Project
        .register_menu_item(
            {MenuItemName::FileMenu, MenuItemName::NewProject},
            std::make_unique<UIItemCommand>(
                CommandName::NewProject,
                [this]() { m_project_interactor.new_project(); },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::N
                    }}
                }
            )
        )
        // File -> Open Project
        .register_menu_item(
            {MenuItemName::FileMenu, MenuItemName::OpenProject},
            std::make_unique<UIItemCommand>(
                CommandName::OpenProject,
                [this]() { load_project(); },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::O
                    }}
                }
            )
        )
        .register_menu_item(
            {MenuItemName::FileMenu, MenuItemName::RecentProjects},
            std::make_unique<UIItemCommand>("recent_projects", nullptr, UIItemCommandExtraOpts{})
        )
        // File -> Save Project
        .register_menu_item(
            {MenuItemName::FileMenu, MenuItemName::SaveProject},
            std::make_unique<UIItemCommand>(
                CommandName::SaveProject,
                [this]() { m_project_saver.save_selected_project(); },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::S
                    }}
                }
            )
        )
        // File -> Save Project As
        .register_menu_item(
            {MenuItemName::FileMenu, MenuItemName::SaveProjectAs},
            std::make_unique<UIItemCommand>(
                CommandName::SaveProjectAs,
                [this]() { m_project_saver.save_selected_project_as(); },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl)
                            | Platform::KeyModifiers(Platform::KeyModifier::Shift),
                        Platform::KeyCode::S
                    }}
                }
            )
        )
#ifdef SHOW_NOT_IMPLEMENTED_ITEMS
        // Menu -> Separator
        .register_menu_separator_item({MenuItemName::FileMenu})
        // Menu -> Shape Gallery
        .register_menu_item(
            {MenuItemName::FileMenu, MenuItemName::ShapeGallery},
            std::make_unique<UIItemCommand>(
                CommandName::ShapeGallery,
                [this]()
                {
                    // TODO: Implement shape gallery functionality
                }
            )
        )
#endif
        // File -> Separator
        .register_menu_separator_item({MenuItemName::FileMenu});

    register_file_menu_import_commands();
    register_file_menu_export_commands();
}

void MenuCommandRegistrar::update_main_menu_plugin_commands(Lua::PluginSystem& plugin_system)
{
    m_menu_manager.rebuild_submenu(MenuItemName::Plugins, [this, &plugin_system]()
    {
        register_main_menu_plugin_commands(plugin_system);
    });
}

void MenuCommandRegistrar::register_main_menu_plugin_commands(Lua::PluginSystem& plugin_system)
{
    bool any_plugin = false;
    for (auto& plugin : plugin_system.plugins() | std::views::values) {
        any_plugin = true;
        std::vector<UniversalMenuItemName> path;
        if (plugin.meta().menu.empty()) {
            path = {
                MenuItemName::MainMenu,
                MenuItemName::Plugins,
                plugin.meta().id,
            };
        } else {
            path = {
                MenuItemName::MainMenu,
                MenuItemName::Plugins
            };
            for (const auto& menu_item : plugin.meta().menu) {
                path.emplace_back(menu_item);
            }
        }
        m_menu_manager.register_menu_item(
            path,
            std::make_unique<UIItemCommand>(
                std::string(CommandName::PluginExecutePrefix) + plugin.meta().id,
                [this, id = plugin.meta().id, &plugin_system]()
                {
                    m_navigator.navigate_to_module_type(App::Render::ModuleType::Plater);
                    plugin_system.execute_plugin(id);
                }
            )
        );
    }
    if (any_plugin) {
        m_menu_manager.register_menu_separator_item({MenuItemName::Plugins});
    }
    m_menu_manager.register_menu_item(
        {MenuItemName::Plugins, MenuItemName::PluginRescan},
        std::make_unique<UIItemCommand>(
            CommandName::PluginRescan,
            [&plugin_system]
            {
                // as on Win/Linux this callback is triggered within render loop showing menu items,
                // we are going to recreate, we need to dispatch this action outside render loop
                // to prevent rendering disposed menu items
                Biz::Platform::PlatformServices::instance()
                    .main_thread_dispatcher()
                    .dispatch_on_main_thread([&plugin_system] { plugin_system.rescan(); });
            }
        )
    );
    m_menu_manager.register_menu_item(
        {MenuItemName::Plugins, MenuItemName::PluginInstall},
        std::make_unique<UIItemCommand>(
            CommandName::PluginInstall,
            [&]
            {
                install_plugin(plugin_system);
            }
        )
    );
    m_menu_manager.register_menu_item(
        {MenuItemName::Plugins, MenuItemName::PluginFolder},
        std::make_unique<UIItemCommand>(
            CommandName::PluginFolder,
            [&]
            {
                AppServices::instance().file_explorer_handler().open_folder(data_dir() + "/lua");
            }
        )
    );
}
} // namespace Slic3r::App
