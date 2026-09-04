#pragma once

#include <cstdint>
#include <string>
#include <map>
#include <unordered_map>
#include <variant>
#include <vector>

#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/Percentage.hpp"
#include "Slic3r/Domain/JsonValue.hpp"

namespace Slic3r::Domain::Preset {

enum class PresetKind : uint8_t
{
    FdmPrinter = 0,
    FdmPrint,
    FdmToolPrint,
    FdmMaterial,
    SlaPrinter = 8,
    SlaPrint,
    SlaToolPrint,
    SlaMaterial
};

inline PresetKind printer_kind(PrinterTechnology technology)
{ return technology == PrinterTechnology::FFF ? PresetKind::FdmPrinter : PresetKind::SlaPrinter; }

inline PresetKind print_kind(PrinterTechnology technology)
{ return technology == PrinterTechnology::FFF ? PresetKind::FdmPrint : PresetKind::SlaPrint; }

inline PresetKind tool_print_kind(PrinterTechnology technology)
{ return technology == PrinterTechnology::FFF ? PresetKind::FdmToolPrint : PresetKind::SlaToolPrint; }

inline PresetKind material_kind(PrinterTechnology technology)
{ return technology == PrinterTechnology::FFF ? PresetKind::FdmMaterial : PresetKind::SlaMaterial; }

namespace Details {
template <PresetKind fdm, PresetKind sla>
bool is_either(PresetKind kind)
{
    return kind == fdm || kind == sla;
}
}

inline bool is_printer(PresetKind kind)
{
    return Details::is_either<
        PresetKind::FdmPrinter,
        PresetKind::SlaPrinter>(kind);
}

inline bool is_print(PresetKind kind)
{
    return Details::is_either<
        PresetKind::FdmPrint,
        PresetKind::SlaPrint>(kind);
}

inline bool is_tool_print(PresetKind kind)
{
    return Details::is_either<
        PresetKind::FdmToolPrint,
        PresetKind::SlaToolPrint>(kind);
}

inline bool is_material(PresetKind kind)
{
    return Details::is_either<
        PresetKind::FdmMaterial,
        PresetKind::SlaMaterial>(kind);
}

using FeatureValue = JsonValue;
using FeatureValueMap = std::unordered_map<std::string, FeatureValue>;

using Bools = std::vector<bool>;
using Strings = std::vector<std::string>;
using Doubles = std::vector<double>;
using Ints = std::vector<int>;
using OptInts = std::vector<std::optional<int>>;
using FloatOrPercentages = std::vector<FloatOrPercentage>;

using PresetValue = std::variant<
    std::monostate,
    Bools, Doubles, Ints, OptInts, FloatOrPercentages, Vec2ds, Strings,
    bool, double, int, Percentage, Vec2d, std::string
>;
using PresetValueMap = std::unordered_map<std::string, PresetValue>;

enum class PresetSaveStrategy
{
    StoreChangedValuesOnly,
    StoreAllValues,
};

}
