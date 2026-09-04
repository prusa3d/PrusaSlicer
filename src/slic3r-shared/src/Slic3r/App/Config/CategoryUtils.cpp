#include "Slic3r/App/Config/CategoryUtils.hpp"
#include "Slic3r/App/Render/ImguiIconHelper.hpp"

namespace Slic3r::App {

namespace CategoryUtils {

Render::Icon category_render_icon(
    const Domain::ConfigItemDef::Category category,
    const Domain::PrinterTechnology pt
)
{
    Render::Icon icon = Render::Icon::None;

    switch (category) {
    case Domain::ConfigItemDef::Category::Print_LayersSurfaces:
        icon = Render::Icon::Layers;
        break;
    case Domain::ConfigItemDef::Category::Print_WallsPerimeters:
        icon = Render::Icon::WallsPerimeters;
        break;
    case Domain::ConfigItemDef::Category::Print_Infill:
        icon = Render::Icon::Infill;
        break;
    case Domain::ConfigItemDef::Category::Print_BedAdhesion:
        icon = Render::Icon::BedAdhesion;
        break;
    case Domain::ConfigItemDef::Category::Print_Supports:
        icon = Render::Icon::Support;
        break;
    case Domain::ConfigItemDef::Category::Print_Speed:
        icon = Render::Icon::Time;
        break;
    case Domain::ConfigItemDef::Category::Print_MotionDynamics:
        icon = Render::Icon::Motions;
        break;
    case Domain::ConfigItemDef::Category::Print_ExtrusionRetraction:
        icon = Render::Icon::ExtrusionRetraction;
        break;
    case Domain::ConfigItemDef::Category::Print_MultiMaterial:
    case Domain::ConfigItemDef::Category::Filament_MultiMaterial:
        icon = Render::Icon::MultiMaterial;
        break;
    case Domain::ConfigItemDef::Category::Print_PrecisionSlicing:
        icon = Render::Icon::Precision;
        break;
    case Domain::ConfigItemDef::Category::Print_CustomGCode:
    case Domain::ConfigItemDef::Category::Filament_CustomGCode:
    case Domain::ConfigItemDef::Category::Printer_CustomGCode:
        icon = Render::Icon::CustomGCode;
        break;
    case Domain::ConfigItemDef::Category::Print_OutputOptions:
        icon = Render::Icon::Output;
        break;
    case Domain::ConfigItemDef::Category::Print_Notes:
    case Domain::ConfigItemDef::Category::Filament_Notes:
    case Domain::ConfigItemDef::Category::Printer_Notes:
        icon = Render::Icon::Notes;
        break;
    case Domain::ConfigItemDef::Category::Filament_MaterialTemperatures:
        icon = Render::Icon::Temperature;
        break;
    case Domain::ConfigItemDef::Category::Filament_ExtrusionCalibration:
        icon = Render::Icon::ExtrusionCalibration;
        break;
    case Domain::ConfigItemDef::Category::Filament_Cooling:
        icon = Render::Icon::Fan;
        break;
    case Domain::ConfigItemDef::Category::Filament_Overrides:
        icon = Render::Icon::Vector;
        break;
    case Domain::ConfigItemDef::Category::Printer_General:
        icon = pt == Domain::PrinterTechnology::FFF ? Render::Icon::PrinterIconMarker :
                                                      Render::Icon::PrinterSlaIconMarker;
        break;
    case Domain::ConfigItemDef::Category::Printer_Bed:
        icon = Render::Icon::Bed;
        break;
    case Domain::ConfigItemDef::Category::Printer_MachineLimits:
        icon = Render::Icon::MachineLimits;
        break;
    case Domain::ConfigItemDef::Category::Printer_MultipleExtruders:
        icon = Render::Icon::MultipleExtruders;
        break;
    case Domain::ConfigItemDef::Category::Printer_SingleExtruderMMSetup:
        icon = Render::Icon::SingleExtruder;
        break;
    case Domain::ConfigItemDef::Category::AppConfig_General: [[fallthrough]];
    case Domain::ConfigItemDef::Category::AppConfig_Services: [[fallthrough]];
    case Domain::ConfigItemDef::Category::PhysicalPrinter_General :
        // No icon assigned on purpose.
        break;
    default:
        PANIC("Unhandled Category value", category);
    }

    return icon;
}

std::string category_icon_name(
    const Domain::ConfigItemDef::Category category,
    const Domain::PrinterTechnology pt
)
{
    Render::Icon icon = category_render_icon(category, pt);
    return Render::ImguiIconHelper::icon_name(icon);
}

} // namespace CategoryUtils

} // namespace Slic3r::App
