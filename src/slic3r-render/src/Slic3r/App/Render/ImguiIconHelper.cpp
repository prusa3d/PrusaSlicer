#include "Slic3r/App/Render/ImguiIconHelper.hpp"

#include <Slic3r/Assert.hpp>

#include <unordered_map>
#include <unordered_set>

namespace Slic3r::App::Render {

static const std::unordered_map<Icon, const char*> ICON_FILENAMES = {
    {Icon::PrintIconMarker, "cog"},
    {Icon::PrinterIconMarker, "printer"},
    {Icon::PrinterSlaIconMarker, "sla_printer"},
    {Icon::FilamentIconMarker, "spool"},
    {Icon::MaterialIconMarker, "resin"},
    {Icon::SliderFloatEditBtnIcon, "edit_button"},
    {Icon::SliderFloatEditBtnPressedIcon, "edit_button_pressed"},
    {Icon::RevertButton, "undo"},
    {Icon::PlugMarker, "plug"},
    {Icon::DowelMarker, "dowel"},
    {Icon::SnapMarker, "snap"},
    {Icon::PrintIdle, "print_idle"},
    {Icon::EyeOpen, "dont_print"},
    {Icon::EyeClosed, "dont_print_active"},
    {Icon::SolidPartVolume, "union"},
    {Icon::NegativeVolume, "subtract"},
    {Icon::ModifierVolume, "exclude"},
    {Icon::SupportBlocker, "support_blocker"},
    {Icon::SupportModifier, "support_enforcer"},
    {Icon::TextSolidPartVolume, "add_text_part"},
    {Icon::TextNegativeVolume, "add_text_negative"},
    {Icon::TextModifierVolume, "add_text_modifier"},
    {Icon::SvgSolidPartVolume, "svg_part"},
    {Icon::SvgNegativeVolume, "svg_negative"},
    {Icon::SvgModifierVolume, "svg_modifier"},
    {Icon::ObjectIcon, "object_icon"},
    {Icon::HRModifier, "edit_layers_all"},
    {Icon::CustomSupports, "fdm_supports"},
    {Icon::CustomSeam, "seam_"},
    {Icon::CutConnectors, "cut_connectors"},
    {Icon::MmSegmentation, "mmu_segmentation_"},
    {Icon::Sinking, "sinking"},
    {Icon::FuzzySkin, "fuzzy_skin_painting"},
    {Icon::Details, "details"},
    {Icon::OpenArrow, "down_arrow"},
    {Icon::CloseArrow, "right_arrow"},
    {Icon::ConfigContainer, "config_container"},
    {Icon::InstancesIcon, "instances_icon"},
    {Icon::AddBedIcon, "add_bed"},
    {Icon::DelBedIcon, "del_bed"},
    {Icon::OverridesMarker, "overrides_marker"},
    {Icon::AllBeds, "all_beds"},
    {Icon::Lock, "lock_closed"},
    {Icon::Unlock, "lock_open"},
    {Icon::LightLockClosed, "light_lock_closed"},
    {Icon::LightLockOpened, "light_lock_opened"},
    {Icon::DSRevert, "undo_r"},
    {Icon::DSSettings, "cog_"},
    {Icon::ErrorTick, "error_tick"},
    {Icon::ErrorTickHovered, "error_tick_f"},
    {Icon::PausePrint, "pause_print"},
    {Icon::PausePrintHovered, "pause_print_f"},
    {Icon::EditGCode, "edit_gcode"},
    {Icon::EditGCodeHovered, "edit_gcode_f"},
    {Icon::RemoveTick, "colorchange_del"},
    {Icon::RemoveTickHovered, "colorchange_del_f"},
    {Icon::PhysicalPrinterIcon, "printer_physical"},
    {Icon::CheckMark, "checked"},
    // sidebar icons
    {Icon::SavePrint, "save_print"},
    {Icon::SavePrintToFlash, "save_print_to_flash"},
    {Icon::SavePrintToLocal, "save_print_to_local"},
    {Icon::SavePrintAddBookmark, "save_print_add_bookmark"},
    {Icon::LegendTravel, "legend_travel"},
    {Icon::LegendWipe, "legend_wipe"},
    {Icon::LegendRetract, "legend_retract"},
    {Icon::LegendDeretract, "legend_deretract"},
    {Icon::LegendSeams, "legend_seams"},
    {Icon::LegendToolChanges, "legend_toolchanges"},
    {Icon::LegendColorChanges, "legend_colorchanges"},
    {Icon::LegendPausePrints, "legend_pauseprints"},
    {Icon::LegendCustomGCodes, "legend_customgcodes"},
    {Icon::LegendCOG, "legend_cog"},
    {Icon::LegendShells, "legend_shells"},
    {Icon::LegendToolMarker, "legend_toolmarker"},
    {Icon::WarningMarker, "notification_warning"},
    {Icon::WarningMarkerWhite, "notification_warning_white"},
    {Icon::ErrorMarker, "notification_error"},
    {Icon::NotificationCloseGray, "notification_close_gray"},

    {Icon::MouseLeft, "mouse_left"},
    {Icon::MouseRight, "mouse_right"},
    {Icon::MouseWheel, "mouse_wheel"},
    {Icon::MouseDrag, "mouse_drag"},
    {Icon::KeyBackspace, "key_backspace"},

    {Icon::ClippyMarker, "notification_clippy"},
    {Icon::PrusaSlicerIcon, "PrusaSlicer"},
    {Icon::Printables, "printables"},
    {Icon::Connect, "connect"},
    {Icon::Cloud, "cloud"},
    // toolbar icons
    {Icon::ToolbarObjects, "toolbar_objects"},
    {Icon::ToolbarHistory, "toolbar_history"},
    {Icon::ToolbarGCode, "toolbar_gcode"},
    {Icon::ToolbarGraph, "toolbar_graph"},
    {Icon::ToolbarCut, "cut"},

    {Icon::TopBarUndo, "tb_undo"},
    {Icon::TopBarRedo, "tb_redo"},
    {Icon::TobBarLoad, "tb_load"},
    {Icon::TobBarSave, "tb_save"},
    {Icon::TobBarShowUI, "tb_show_ui"},
    {Icon::TobBarPlus, "plus_new"},
    {Icon::TopBarCross, "cross_new"},

    // Gizmo Paint-On-Supports
    {Icon::Circle, "circle"},
    {Icon::Triangle, "triangle"},
    {Icon::Square, "square"},
    {Icon::Hexagon, "hexagon"},
    {Icon::Sphere, "sphere"},
    {Icon::PaintBrush, "paintbrush"},
    {Icon::WandMagicSparkles, "wand_magic_sparkles"},
    {Icon::Prism, "prism"},
    {Icon::Frustum, "frustum"},
    {Icon::DividingLine, "dividing_line"},
    {Icon::Dove, "dove"},

    // Gizmo Multimaterial painting
    {Icon::FillDrip, "fill_drip"},
    {Icon::ColorReplace, "color_replace"},
    {Icon::LineHeight, "line_height"},
    {Icon::Switch, "switch"},

    {Icon::Calculator, "calculator"},
    {Icon::CopyForGizmo, "copy_for_gizmo"},
    {Icon::NewBtnIcon, "new_button"},
    {Icon::DeleteBtnIcon, "delete_button"},

    // Gizmo Arrange
    {Icon::ArrangeTopLeft, "arrange_top_left"},
    {Icon::ArrangeTopRight, "arrange_top_right"},
    {Icon::ArrangeBottomLeft, "arrange_bottom_left"},
    {Icon::ArrangeBottomRight, "arrange_bottom_right"},
    {Icon::ArrangeCenter, "arrange_center"},

    {Icon::MultipleSquares, "multiple_squares"},
    {Icon::SingleSquare, "single_square"},

    // Gizmo Layer Height
    {Icon::ArrowUpToLine, "arrow_up_to_line"},
    {Icon::ArrowUpFromLine, "arrow_up_from_line"},
    {Icon::LayersIcon, "layers_icon"},
    {Icon::MinusModifier, "minus_modifier"},
    {Icon::PlusHeightRange, "plus_height_range"},
    {Icon::PlusModifier, "plus_modifier"},

    // Physical printer
    {Icon::ExportToSD, "export_to_sd"},    
    {Icon::ConnectUpload, "connect_gcode"},
    {Icon::ExportToLocal, "save"},

    // Gizmo Emboss
    {Icon::AlignHLeftBtn, "align_horizontal_left"},
    {Icon::AlignHCenterBtn, "align_horizontal_center"},
    {Icon::AlignHRightBtn, "align_horizontal_right"},
    {Icon::AlignVTopBtn, "align_vertical_top"},
    {Icon::AlignVCenterBtn, "align_vertical_center"},
    {Icon::AlignVBottomBtn, "align_vertical_bottom"},

    // Gizmo Svg
    {Icon::Reload, "refresh"},
    {Icon::ReflectionX, "reflection_x"},
    {Icon::ReflectionY, "reflection_y"},

    {Icon::Layers, "layers"},
    {Icon::WallsPerimeters, "walls_and_perimeters"},
    {Icon::Infill, "infill"},
    {Icon::BedAdhesion, "bed_adhesion"},
    {Icon::SkirtBrim, "skirt_brim"},
    {Icon::Support, "support"},
    {Icon::Time, "time"},
    {Icon::Motions, "motions"},
    {Icon::ExtrusionRetraction, "extrusion_retraction"},
    {Icon::MultiMaterial, "multi-material"},
    {Icon::Precision, "precision"},
    {Icon::CustomGCode, "custom_gcode"},
    {Icon::Temperature, "temperature"},
    {Icon::ExtrusionCalibration, "extrusion_calibration"},
    {Icon::Vector, "vector"},
    {Icon::Bed, "bed"},
    {Icon::MachineLimits, "machine_limits"},
    {Icon::MultipleExtruders, "multiple_extruders"},
    {Icon::SingleExtruder, "single_extruder"},
    {Icon::Funnel, "funnel"},
    {Icon::Star, "star"},
    {Icon::StarSolid, "star_solid"},
    {Icon::Cog, "cog"},
    {Icon::Cogs, "cogs"},
    {Icon::Output, "output"},
    {Icon::Notes, "notes"},
    {Icon::CaretLeft, "caret_left"},
    {Icon::CaretRight, "caret_right"},
    {Icon::CaretUp, "caret_up"},
    {Icon::CaretDown, "caret_down"},
    {Icon::Search, "search_gray"},
    {Icon::Fan, "cooling"},
    {Icon::AddObject, "add_object"},
    {Icon::AddVolume, "add_volume"},
    {Icon::Minus, "minus"},
    {Icon::ChevronRight, "chevron_right"},
    {Icon::ChevronLeft, "chevron_left"},
    {Icon::Compare, "compare"},
    {Icon::Hollowing, "hollowing"},
    {Icon::Pad, "pad"},
    {Icon::Preview, "preview"},
    {Icon::Move, "move"},
    {Icon::Scale, "scale"},
    {Icon::PlaceOnFace, "place"},
    {Icon::Ellipsis, "elipsis"},
    {Icon::Cube, "cube"},
    {Icon::CubeAdd, "cube_add"},
    {Icon::Layout, "layout"},
    {Icon::LayersInspect, "layers_inspect"},
    {Icon::PaintSupports, "paint_supports"},
    {Icon::PaintSeams, "paint_seams"},
    {Icon::PaintFuzzySkin, "paint_fuzzy_skin"},
    {Icon::PaintMultiMaterial, "paint_multi_material"},
    {Icon::Ruler, "ruler"},
    {Icon::Text, "text"},
    {Icon::Svg, "toolbar_svg"},
    {Icon::Palette, "palette"},
    {Icon::Scissors, "scissors"},
    {Icon::Cut, "cut"},
    {Icon::OpenFolder, "open_folder"},
    {Icon::Shapes, "shapes"},
    {Icon::RecentProjects, "recent_projects"},
    {Icon::RectangleAdd, "rectangle_add"},
    {Icon::Rotate, "rotate"},
    {Icon::Simplify, "simplify"},
    {Icon::ExclamationTriangle, "exclamation_triangle"},
    {Icon::Plus, "plus"},
    {Icon::Chain, "chain"},
    {Icon::Unchain, "unchain"},
    {Icon::CloseGizmo, "close_gizmo"},
    {Icon::UndoGizmo, "undo_gizmo"},
    {Icon::VariableLayerHeight, "variable_layer_height"},
    {Icon::HeightRange, "height_range"},
    {Icon::ArrowRight, "arrow_right"},
    {Icon::Filter, "filter"},
    {Icon::Robot, "robot"},
};

static const std::unordered_set<Icon> ICON_PNG = {};

std::string ImguiIconHelper::icon_path(Icon icon)
{
    std::unordered_map<Icon, const char*>::const_iterator it = ICON_FILENAMES.find(icon);
    ASSERT(it != ICON_FILENAMES.cend(), "Icon doesn't have defined filename!", icon);

    std::string filename = it->second;
    if (ICON_PNG.contains(icon)) {
        filename.append(".png");
    } else {
        filename.append(".svg");
    }

    return "icons/" + filename;
}

std::string ImguiIconHelper::icon_name(Icon icon)
{
    std::unordered_map<Icon, const char*>::const_iterator it = ICON_FILENAMES.find(icon);
    if (it == ICON_FILENAMES.cend()) {
        return std::string();
    }
    return it->second;
}

} // namespace Slic3r::App::Render
