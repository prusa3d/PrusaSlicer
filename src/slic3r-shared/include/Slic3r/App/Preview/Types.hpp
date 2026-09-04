#pragma once

#include <Slic3r/App/libvgcode/Types.hpp>

#include <functional>
#include <set>

namespace Slic3r::App::Preview {

struct ExtrudersSequence;

enum class PrinterTechnology : uint8_t
{
    Unknown,
    FFF,
    SLA,
    Any,
    COUNT
};

static constexpr size_t PRINTER_TECHNOLOGIES_COUNT = size_t(PrinterTechnology::COUNT);

typedef std::function<void(void)>                                   InvalidateSliceCallback;
typedef std::function<void(void)>                                   TicksChangedCallback;
typedef std::function<void(const Domain::CustomGCode::Info&)>               UpdateLayersSlider;
typedef std::function<libvgcode::Palette(void)>                     GetExtruderColorsCallback;
typedef std::function<bool(void)>                                   AutoColorChangeCallback;
typedef std::function<void()>                                       NotifyEmptyColorChangeGCodeCallback;
typedef std::function<void()>                                       NotifyEmptyAutoColorChangeCallback;
typedef std::function<bool(ExtrudersSequence&)>                     GetExtrudersSequenceCallback;
typedef std::function<int(const std::string&, int)>                 ShowInfoMsgCallback;
typedef std::function<std::string(Domain::CustomGCode::Type)>               GetGCodeCallback;
typedef std::function<std::set<int>(float)>                         GetUsedExtrudersInPrintCallback;
typedef std::function<void(void)>                                   GCodeViewTypeChangedCallback;
typedef std::function<void(void)>                                   ExtrusionRoleVisibilityChangedCallback;
typedef std::function<void(const std::string&, bool)>               AppConfigChangedCallback;

std::string to_string(Biz::libpgcode::MoveType type);
std::string to_string(Domain::GCodeExtrusionRole role);
std::string to_string(Biz::libpgcode::OptionType type);
std::string to_string(libvgcode::ViewType type, std::optional<Biz::libpgcode::UnitsSystem> sys = std::nullopt);

} // namespace Slic3r::App::Preview
