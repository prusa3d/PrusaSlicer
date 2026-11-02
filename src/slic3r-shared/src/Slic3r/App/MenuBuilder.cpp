#include "Slic3r/App/MenuBuilder.hpp"
#include "Slic3r/App/MenuManager.hpp"
#include "Slic3r/App/MenuItem.hpp"
#include "Slic3r/App/CommandBindingManager.hpp"
#include "Slic3r/App/UIItemCommand.hpp"

#include "Slic3r/App/Yoga/Menu.hpp"
#include "Slic3r/App/Yoga/MenuItem.hpp"

#include "Slic3r/App/Render/ImguiIconHelper.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App {

std::string dots{"..."};

std::string MenuBuilder::item_name_translated(UniversalMenuItemName menu_item_name)
{
    return std::visit(
        Domain::overloaded{
            [](const std::string& s) -> std::string { return s; },
            [](MenuItemName menu_item_name) -> std::string
            {
                switch (menu_item_name) {
                case MenuItemName::MainMenu:
                    return Biz::_u8L("Menu");
                case MenuItemName::Edit:
                case MenuItemName::EditSvgOrText:
                    return Biz::_u8L("Edit");
                case MenuItemName::SelectAll:
                    return Biz::_u8L("Select all");
                case MenuItemName::DeselectAll:
                    return Biz::_u8L("Deselect all");
                case MenuItemName::DeleteSelected:
                    return Biz::_u8L("Delete selected");
                case MenuItemName::Undo:
                    return Biz::_u8L("Undo");
                case MenuItemName::Redo:
                    return Biz::_u8L("Redo");
                case MenuItemName::Copy:
                case MenuItemName::CopyObject:
                case MenuItemName::CopyVolume:
                case MenuItemName::CopyMultiObjects:
                    return Biz::_u8L("Copy");
                case MenuItemName::Paste:
                case MenuItemName::PasteObject:
                case MenuItemName::PasteVolume:
                    return Biz::_u8L("Paste");
                case MenuItemName::ReloadFromDisk:
                case MenuItemName::ReloadObject:
                case MenuItemName::ReloadVolume:
                    return Biz::_u8L("Reload from disk");
                case MenuItemName::Arrange:
                    return Biz::_u8L("Arrange");
                case MenuItemName::ArrangeSelection:
                    return Biz::_u8L("Arrange selection");
                case MenuItemName::Search:
                    return Biz::_u8L("Search");
                case MenuItemName::Preferences:
#ifdef __APPLE__
                    return Biz::_u8L("Settings");
#else
                    return Biz::_u8L("Preferences");
#endif
                case MenuItemName::FileMenu:
                    return Biz::_u8L("File");
                case MenuItemName::NewProject:
                    return Biz::_u8L("New project");
                case MenuItemName::OpenProject:
                    return Biz::_u8L("Open project");
                case MenuItemName::SaveProject:
                    return Biz::_u8L("Save");
                case MenuItemName::SaveProjectAs:
                    return Biz::_u8L("Save as") + dots;
                case MenuItemName::Import:
                    return Biz::_u8L("Import");
                case MenuItemName::ImportGeometry:
                    return Biz::_u8L("Import STL/3MF");
                case MenuItemName::ImportZip:
                    return Biz::_u8L("Import ZIP archive");
                case MenuItemName::JumpToValue:
                    return Biz::_u8L("Jump to height");
                case MenuItemName::ShapeGallery:
                    return Biz::_u8L("Shape gallery");
                case MenuItemName::View:
                    return Biz::_u8L("View");
                case MenuItemName::ShowLabel:
                    return Biz::_u8L("Show labels");
                case MenuItemName::FullScreen:
                    return Biz::_u8L("Fullscreen");
                case MenuItemName::ZoomIn:
                    return Biz::_u8L("Zoom in");
                case MenuItemName::ZoomOut:
                    return Biz::_u8L("Zoom out");
                case MenuItemName::CameraProjectionSwitch:
                    return Biz::_u8L("Switch projection");
                case MenuItemName::LookAtActiveBed:
                    return Biz::_u8L("Look at active bed");
                case MenuItemName::CameraDefaultView:
                    return Biz::_u8L("Default view");
                case MenuItemName::CameraTopView:
                    return Biz::_u8L("Top view");
                case MenuItemName::CameraBottomView:
                    return Biz::_u8L("Bottom view");
                case MenuItemName::CameraFrontView:
                    return Biz::_u8L("Front view");
                case MenuItemName::CameraRearView:
                    return Biz::_u8L("Rear view");
                case MenuItemName::CameraLeftView:
                    return Biz::_u8L("Left view");
                case MenuItemName::CameraRightView:
                    return Biz::_u8L("Right view");
                case MenuItemName::Configuration:
                    return Biz::_u8L("Configuration");
                case MenuItemName::ConfigurationWizard:
                    return Biz::_u8L("Configuration wizard");
                case MenuItemName::CheckConfigUpdates:
                    return Biz::_u8L("Check for config updates");
                case MenuItemName::CheckAppUpdates:
                    return Biz::_u8L("Check for application updates");
                case MenuItemName::FlashPrinterFirmware:
                    return Biz::_u8L("Flash printer firmware");
                case MenuItemName::Help:
                    return Biz::_u8L("Help");
                case MenuItemName::PSWebsite:
                    return Biz::_u8L("PrusaSlicer website");
                case  MenuItemName::Samples:
                    return Biz::_u8L("Sample G-codes and models");
                case  MenuItemName::Releases:
                    return Biz::_u8L("Software releases");
                case MenuItemName::SystemInfo:
                    return Biz::_u8L("System info");
                case MenuItemName::ConfigFolder:
                    return Biz::_u8L("Show configuration folder");
                case MenuItemName::ReportAnIssue:
                    return Biz::_u8L("Report PrusaSlicer issue");
                case MenuItemName::About:
                    return Biz::_u8L("About PrusaSlicer");
                case MenuItemName::TipOfTheDay:
                    return Biz::_u8L("Show tip of the day");
                case MenuItemName::KeyboardShortcutsDialog:
                    return Biz::_u8L("Keyboard shortcuts");
                case MenuItemName::RecentProjects:
                    return Biz::_u8L("Recent projects");
                case MenuItemName::Exit:
                    return Biz::_u8L("Exit");
                case MenuItemName::Export:
                    return Biz::_u8L("Export");
                case MenuItemName::ExportGcode:
                    return Biz::_u8L("Export G-code");
                case MenuItemName::SendGcode:
                    return Biz::_u8L("Send G-code");
                case MenuItemName::ExportGcodeToFlash:
                    return Biz::_u8L("Export G-code to SD card / flash drive");
                case MenuItemName::OnlinePresetUpdate:
                    return Biz::_u8L("Update from online presets");
                case MenuItemName::PresetReposManagement:
                    // TRN Main menu item. Opens the preset sources and updates dialog.
                    return Biz::_u8L("Preset sources & updates");
                case MenuItemName::ArrangeBed:
                    return Biz::_u8L("Arrange bed");
                case MenuItemName::ArrangeSelectionBed:
                    return Biz::_u8L("Arrange selection");
                case MenuItemName::SelectAllOnBed:
                    return Biz::_u8L("Select all objects");
                case MenuItemName::DeleteBed:
                case MenuItemName::DeleteSelectedObject:
                case  MenuItemName::DeleteSelectedMultiObjects:
                case MenuItemName::DeleteSelectedSvgOrText:
                case MenuItemName::DeleteSelectedVolume:
                    return Biz::_u8L("Delete");
                case MenuItemName::RenameObject:
                case MenuItemName::RenameVolume:
                    return Biz::_u8L("Rename");
                case MenuItemName::SetNumberOfInstances:
                case MenuItemName::SetMultiObjectsNumberOfInstances:
                    return Biz::_u8L("Set number of instances");
                case MenuItemName::FillBedWithInstances:
                    return Biz::_u8L("Fill bed with instances");
                case MenuItemName::ExportObject:
                case MenuItemName::ExportVolume:
                    return Biz::_u8L("Export as STL/OBJ") + "...";
                case MenuItemName::ReplaceObject:
                case MenuItemName::ReplaceVolume:
                    return Biz::_u8L("Replace with STL");
                case MenuItemName::SplitObject:
                case MenuItemName::SplitVolume:
                    return Biz::_u8L("Split");
                case MenuItemName::SplitObjectToObjects:
                    return Biz::_u8L("To objects");
                case MenuItemName::SplitObjectToVolumes:
                    return Biz::_u8L("To parts");
                case MenuItemName::ScaleToPrintVolume:
                    return Biz::_u8L("Scale to print volume");
                case MenuItemName::FixMultiObjectWithRepairAlgorithm:
                case MenuItemName::FixObjectWithRepairAlgorithm:
                case MenuItemName::FixVolumeWithRepairAlgorithm:
                    return Biz::_u8L("Fix by Windows repair algorithm");
                case MenuItemName::PrintableObject:
                case MenuItemName::PrintableMultiObjects:
                    return Biz::_u8L("Printable");
                case MenuItemName::AddObjectShape:
                    return Biz::_u8L("Add shape");
                case MenuItemName::ObjectShapeLoad:
                return Biz::_u8L("Import");case MenuItemName::SolidPartVolumeShapeLoad:
                case MenuItemName::NegativeVolumeShapeLoad:
                case MenuItemName::ModifierVolumeShapeLoad:
                case MenuItemName::SupportBlockerShapeLoad:
                case MenuItemName::SupportModifierShapeLoad:
                    return Biz::_u8L("Load");
                case MenuItemName::ObjectShapeCube:
                case MenuItemName::SolidPartVolumeShapeCube:
                case MenuItemName::NegativeVolumeShapeCube:
                case MenuItemName::ModifierVolumeShapeCube:
                case MenuItemName::SupportBlockerShapeCube:
                case MenuItemName::SupportModifierShapeCube:
                    return Biz::_u8L("Cube");
                case MenuItemName::ObjectShapeCylinder:
                case MenuItemName::SolidPartVolumeShapeCylinder:
                case MenuItemName::NegativeVolumeShapeCylinder:
                case MenuItemName::ModifierVolumeShapeCylinder:
                case MenuItemName::SupportBlockerShapeCylinder:
                case MenuItemName::SupportModifierShapeCylinder:
                    return Biz::_u8L("Cylinder");
                case MenuItemName::ObjectShapeSphere:
                case MenuItemName::SolidPartVolumeShapeSphere:
                case MenuItemName::NegativeVolumeShapeSphere:
                case MenuItemName::ModifierVolumeShapeSphere:
                case MenuItemName::SupportBlockerShapeSphere:
                case MenuItemName::SupportModifierShapeSphere:
                    return Biz::_u8L("Sphere");
                case MenuItemName::ObjectShapeText:
                case MenuItemName::SolidPartVolumeShapeText:
                case MenuItemName::NegativeVolumeShapeText:
                case MenuItemName::ModifierVolumeShapeText:
                    return Biz::_u8L("Text");
                case MenuItemName::ObjectShapeSvg:
                case MenuItemName::SolidPartVolumeShapeSVG:
                case MenuItemName::NegativeVolumeShapeSVG:
                case MenuItemName::ModifierVolumeShapeSVG:
                    return Biz::_u8L("SVG");
                case MenuItemName::ObjectShapeFromGallery:
                case MenuItemName::SolidPartVolumeShapeFromGallery:
                case MenuItemName::NegativeVolumeShapeFromGallery:
                case MenuItemName::ModifierVolumeShapeFromGallery:
                case MenuItemName::SupportBlockerShapeFromGallery:
                case MenuItemName::SupportModifierShapeFromGallery:
                    return Biz::_u8L("Gallery");
                case MenuItemName::AddVolume:
                    return Biz::_u8L("Add volume");
                case MenuItemName::SolidPartVolume:
                    return Biz::_u8L("Solid part");
                case MenuItemName::NegativeVolume:
                    return Biz::_u8L("Negative volume");
                case MenuItemName::ModifierVolume:
                    return Biz::_u8L("Modifier");
                case MenuItemName::SupportBlocker:
                    return Biz::_u8L("Support blocker");
                case MenuItemName::SupportModifier:
                    return Biz::_u8L("Support modifier");
                case MenuItemName::SetAsSeparateObject:
                    return Biz::_u8L("Separate to object(s)");
                case MenuItemName::MergeMultiObjects:
                    return Biz::_u8L("Merge");

                case MenuItemName::InvalidateCutInfo:
                    return Biz::_u8L("Invalidate cut info");

                case MenuItemName::Plugins:
                    return Biz::_u8L("Plugins");
                case MenuItemName::PluginRescan:
                    return Biz::_u8L("Rescan plugins");
                case MenuItemName::PluginFolder:
                    return Biz::_u8L("Show user plugins folder");
                case MenuItemName::PluginInstall:
                    return Biz::_u8L("Install plugin bundle");
                case MenuItemName::PluginMarketplace:
                    return Biz::_u8L("Plugin Marketplace");

                default:
                    return std::string();
                }
            }
        },
        menu_item_name
    );
};

Render::Icon MenuBuilder::item_icon(UniversalMenuItemName menu_item_name)
{
    if (!std::holds_alternative<MenuItemName>(menu_item_name)) {
        return Render::Icon::None;
    }

    switch (std::get<MenuItemName>(menu_item_name)) {
    case MenuItemName::MainMenu:
        return Render::Icon::PrusaSlicerIcon;
    case MenuItemName::DeleteSelected:
    case MenuItemName::DeleteBed:
    case MenuItemName::DeleteSelectedObject:
        return Render::Icon::DeleteBtnIcon;
    case MenuItemName::Search:
        return Render::Icon::Search;
    case MenuItemName::Arrange:
        return Render::Icon::AllBeds;
    case MenuItemName::Preferences:
        return Render::Icon::Cog;
    case MenuItemName::FileMenu:
    case MenuItemName::NewProject:
        return Render::Icon::NewBtnIcon;
    case MenuItemName::OpenProject:
        return Render::Icon::OpenFolder;
    case MenuItemName::SaveProject:
    case MenuItemName::SaveProjectAs:
        return Render::Icon::TobBarSave;
    case MenuItemName::ImportGeometry:
    case MenuItemName::ImportZip:
        return Render::Icon::AddObject;
    case MenuItemName::ShapeGallery:
    case MenuItemName::AddObjectShape:
        return Render::Icon::Shapes;
    case MenuItemName::RecentProjects:
        return Render::Icon::RecentProjects;
    case MenuItemName::AddVolume:
        return Render::Icon::AddVolume;
    case MenuItemName::SolidPartVolume:
        return Render::Icon::SolidPartVolume;
    case MenuItemName::NegativeVolume:
        return Render::Icon::NegativeVolume;
    case MenuItemName::ModifierVolume:
        return Render::Icon::ModifierVolume;
    case MenuItemName::SupportBlocker:
        return Render::Icon::SupportBlocker;
    case MenuItemName::SupportModifier:
        return Render::Icon::SupportModifier;

    case MenuItemName::Import:
    case MenuItemName::Edit:
    case MenuItemName::DeselectAll:
    case MenuItemName::JumpToValue:
    case MenuItemName::BedContextMenu:
    case MenuItemName::ObjectContextMenu:
    case MenuItemName::MultiObjectsContextMenu:
    case MenuItemName::SvgOrTextContextMenu:
    case MenuItemName::VolumeContextMenu:
    case MenuItemName::SolidPartVolumeShapeLoad:
    default:
        return Render::Icon::None;
    }
}

std::string MenuBuilder::icon_name(UniversalMenuItemName menu_item_name)
{
    Render::Icon icon = item_icon(menu_item_name);
    return icon == Render::Icon::None ? std::string{} : Render::ImguiIconHelper::icon_name(icon);
}

static std::string get_item_name_translated(App::MenuItem* menu_item)
{
    std::string item_name = MenuBuilder::item_name_translated(menu_item->name());
    if (menu_item->command() && menu_item->command()->has_todo_state()) {
        item_name += fmt::format("   ({})", Biz::_u8L("TODO"));
    }
    return item_name;
}

void MenuBuilder::add_submenu(Yoga::MenuItem* yoga_menu_item, App::MenuItem* menu_item)
{
    for (App::MenuItem* sub_menu_item : menu_item->children()) {
        if (sub_menu_item->is_separator()) {
            yoga_menu_item->append_sub_menu_separator();
            continue;
        }

        Yoga::MenuItem* new_yoga_menu_item = yoga_menu_item->append_sub_menu_item(
            get_item_name_translated(sub_menu_item),
            item_icon(sub_menu_item->name())
        );
        if (sub_menu_item->children().empty()) {
            m_command_binding_manager.bind_menu_item(sub_menu_item->command(), new_yoga_menu_item);
        } else {
            add_submenu(new_yoga_menu_item, sub_menu_item);
        }
    }
}

Yoga::MenuItem* MenuBuilder::add_menu_item(Yoga::Menu* menu, App::MenuItem* menu_item)
{
    Yoga::MenuItem* yoga_menu_item = menu->append_item(
        get_item_name_translated(menu_item),
        item_icon(menu_item->name())
    );
    m_command_binding_manager.bind_menu_item(menu_item->command(), yoga_menu_item);

    return yoga_menu_item;
}

void MenuBuilder::add_menu_items(Yoga::Menu* menu, App::MenuItem* root_menu_item)
{
    ASSERT(root_menu_item);

    for (App::MenuItem* menu_item : root_menu_item->children()) {
        if (menu_item->is_separator()) {
            menu->append_separator();
        } else if (menu_item->children().empty()) {
            add_menu_item(menu, menu_item);
        } else {
            Yoga::MenuItem* yoga_menu_item_with_submenu =
                menu->append_item(get_item_name_translated(menu_item));
            add_submenu(yoga_menu_item_with_submenu, menu_item);
        }
    }
}

Yoga::MenuItem* MenuBuilder::add_menu_item(Yoga::Menu* menu, MenuItemName menu_item_name)
{
    return add_menu_item(menu, m_menu_manager.menu_item(menu_item_name));
}
} // namespace Slic3r::App
