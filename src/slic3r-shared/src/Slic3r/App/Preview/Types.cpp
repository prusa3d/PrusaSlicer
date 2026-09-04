#include "Slic3r/App/Preview/Types.hpp"

#include "Slic3r/Biz/Units.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

using namespace Slic3r::Biz::libpgcode;
using namespace Slic3r::App::libvgcode;
using namespace Slic3r::Biz;
using namespace Slic3r::Domain;

namespace Slic3r::App::Preview {

std::string to_string(MoveType type)
{
    switch (type)
    {
    // TRN: Following strings are labels in the vertex properties dialog.
    case MoveType::Noop:        { return _u8L("Noop"); }
    case MoveType::Retract:     { return _u8L("Retract"); }
    case MoveType::Unretract:   { return _u8L("Unretract"); }
    case MoveType::Seam:        { return _u8L("Seam"); }
    case MoveType::ToolChange:  { return _u8L("Tool Change"); }
    case MoveType::ColorChange: { return _u8L("Color Change"); }
    case MoveType::PausePrint:  { return _u8L("Pause Print"); }
    case MoveType::CustomGCode: { return _u8L("Custom G-code"); }
    case MoveType::Travel:      { return _u8L("Travel"); }
    case MoveType::Wipe:        { return _u8L("Wipe"); }
    case MoveType::Extrude:     { return _u8L("Extrude"); }
    case MoveType::Flush:       { return _u8L("Flush"); }
    default:                    { return _u8L("Unknown"); }
    }
}

std::string to_string(GCodeExtrusionRole role)
{
    switch (role)
    {
    default:
    // TRN: Following strings are labels in the G-code Viewer legend.
    case GCodeExtrusionRole::None:                     { return _u8L("Unknown"); }
    case GCodeExtrusionRole::Perimeter:                { return _u8L("Perimeter"); }
    case GCodeExtrusionRole::ExternalPerimeter:        { return _u8L("External perimeter"); }
    case GCodeExtrusionRole::OverhangPerimeter:        { return _u8L("Overhang perimeter"); }
    case GCodeExtrusionRole::InternalInfill:           { return _u8L("Internal infill"); }
    case GCodeExtrusionRole::SolidInfill:              { return _u8L("Solid infill"); }
    case GCodeExtrusionRole::TopSolidInfill:           { return _u8L("Top solid infill"); }
    case GCodeExtrusionRole::Ironing:                  { return _u8L("Ironing"); }
    case GCodeExtrusionRole::BridgeInfill:             { return _u8L("Bridge infill"); }
    case GCodeExtrusionRole::GapFill:                  { return _u8L("Gap fill"); }
    case GCodeExtrusionRole::Skirt:                    { return _u8L("Skirt/Brim"); }
    case GCodeExtrusionRole::SupportMaterial:          { return _u8L("Support material"); }
    case GCodeExtrusionRole::SupportMaterialInterface: { return _u8L("Support material interface"); }
    case GCodeExtrusionRole::WipeTower:                { return _u8L("Wipe tower"); }
    case GCodeExtrusionRole::Custom:                   { return _u8L("Custom"); }
    }
}

std::string to_string(OptionType type)
{
    switch (type)
    {
    // TRN: Following strings are labels in the G-code Viewer legend.
    case OptionType::Travels:         { return _u8L("Travels"); }
    case OptionType::Wipes:           { return _u8L("Wipes"); }
    case OptionType::Retractions:     { return _u8L("Retractions"); }
    case OptionType::Unretractions:   { return _u8L("Deretractions"); }
    case OptionType::Seams:           { return _u8L("Seams"); }
    case OptionType::ToolChanges:     { return _u8L("Tool Changes"); }
    case OptionType::ColorChanges:    { return _u8L("Color Changes"); }
    case OptionType::PausePrints:     { return _u8L("Pause Prints"); }
    case OptionType::CustomGCodes:    { return _u8L("Custom G-codes"); }
    case OptionType::CenterOfGravity: { return _u8L("Center Of Gravity"); }
    case OptionType::ToolMarker:      { return _u8L("Tool Marker"); }
    default:                          { return _u8L("Unknown"); }
    }
}

std::string to_string(libvgcode::ViewType type, std::optional<Biz::libpgcode::UnitsSystem> sys)
{
    switch (type)
    {
    // TRN: Following strings are labels in the G-code Viewer legend.
    case ViewType::FeatureType:
    {
        return _u8L("Feature type");
    }
    case ViewType::Height:
    {
        if (sys.has_value()) {
            std::string units = format_units((sys == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches);
            return _u8L("Height") + " (" + units + ")";
        }
        else
            return _u8L("Height");
    }
    case ViewType::Width:
    {
        if (sys.has_value()) {
            std::string units = format_units((sys == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches);
            return _u8L("Width") + " (" + units + ")";
        }
        else
            return _u8L("Width");
    }
    case ViewType::Speed:
    {
        if (sys.has_value()) {
            std::string units = format_units((sys == UnitsSystem::SI) ? UnitsType::MillimetersPerSecond : UnitsType::InchesPerSecond);
            return _u8L("Speed") + " (" + units + ")";
        }
        else
            return _u8L("Speed");
    }
    case ViewType::ActualSpeed:
    {
        if (sys.has_value()) {
            std::string units = format_units((sys == UnitsSystem::SI) ? UnitsType::MillimetersPerSecond : UnitsType::InchesPerSecond);
            return _u8L("Actual speed") + " (" + units + ")";
        }
        else
            return _u8L("Actual speed");
    }
    case ViewType::FanSpeed:
    {
        return sys.has_value() ? _u8L("Fan speed") + " (%)" : _u8L("Fan speed");
    }
    case ViewType::Temperature:
    {
        if (sys.has_value()) {
            std::string units = format_units((sys == UnitsSystem::SI) ? UnitsType::Celsius : UnitsType::Farhenheit);
            return _u8L("Temperature") + " (" + units + ")";
        }
        else
            return _u8L("Temperature");
    }
    case ViewType::VolumetricFlowRate:
    {
        if (sys.has_value()) {
            std::string units = format_units((sys == UnitsSystem::SI) ? UnitsType::MillimetersCube : UnitsType::InchesCube);
            units += "/" + format_units(UnitsType::Seconds);
            return _u8L("Volumetric flow rate") + " (" + units + ")";
        }
        else
            return _u8L("Volumetric flow rate");
    }
    case ViewType::ActualVolumetricFlowRate:
    {
        if (sys.has_value()) {
            std::string units = format_units((sys == UnitsSystem::SI) ? UnitsType::MillimetersCube : UnitsType::InchesCube);
            units += "/" + format_units(UnitsType::Seconds);
            return _u8L("Actual volumetric flow rate") + " (" + units + ")";
        }
        else
            return _u8L("Actual volumetric flow rate");
    }
    case ViewType::LayerTimeLinear:
    {
        return _u8L("Layer time (linear)");
    }
    case ViewType::LayerTimeLogarithmic:
    {
        return _u8L("Layer time (logarithmic)");
    }
    case ViewType::Tool:
    {
        return _u8L("Tool");
    }
    case ViewType::ColorPrint:
    {
        return _u8L("ColorPrint");
    }
    default:
    {
        return _u8L("Unknown");
    }
    }
}

} // namespace Slic3r::App::Preview
