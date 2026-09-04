#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/FullConfigFDM.hpp"

namespace Slic3r::Domain {

using ConfigBoxRefs      = std::vector<std::reference_wrapper<ConfigBox>>;
using ConstConfigBoxRefs = std::vector<std::reference_wrapper<const ConfigBox>>;

static void
ensure_tool_parity(Domain::MutBoxRef& box, int extruder_count, bool ensure_down_size_only)
{
    for (ConfigItem& item : box.get().items.all_items()) {
        item.visit(
            [&](auto& value)
            {
                if (!item.def().require_tool_parity) {
                    return;
                }
                using Type = std::remove_cvref_t<decltype(value)>;
                if constexpr (Domain::is_std_vector_v<Type>) {
                    if (ensure_down_size_only) {
                        ASSERT(extruder_count <= value.size());
                    }
                    value.resize(extruder_count);
                }
            }
        );
    }
}

ConfigPackFDM::ConfigPackFDM(const int extruder_count) :
    tool{std::vector<Domain::ToolPrintSettings>(extruder_count)},
    filament{std::vector<Domain::FilamentSettings>(extruder_count)}
{
    resize_tool_parity_items(extruder_count, false);
}

void ConfigPackFDM::resize_tool_parity_items(int extruder_count, bool ensure_down_size_only)
{
    for (auto& box_or_boxes : as_mut_boxes(*this)) {
        std::visit(
            Domain::overloaded{
                [&](Domain::MutBoxRef& box)
                { ensure_tool_parity(box, extruder_count, ensure_down_size_only); },
                [&](Domain::MutBoxRefs& boxes)
                {
                    for (Domain::MutBoxRef& box : boxes) {
                        ensure_tool_parity(box, extruder_count, ensure_down_size_only);
                    }
                }
            },
            box_or_boxes
        );
    }
}

ConfigPackFDM::ConfigPackFDM() : ConfigPackFDM{1} {}

bool ConfigPackFDM::operator==(const ConfigPackFDM& other) const
{
    return printer == other.printer
        && print == other.print
        && tool == other.tool
        && filament == other.filament
        && project == other.project
        && virtual_extruders == other.virtual_extruders;
}

const PrinterSettings& ConfigPackFDM::get_printer() const
{
    return printer;
}

const PrintSettings& ConfigPackFDM::get_print() const
{
    return print;
}

const ToolPrintSettings& ConfigPackFDM::get_tool(size_t index) const
{
    return tool.at(index);
}

const FilamentSettings& ConfigPackFDM::get_filament(size_t index) const
{
    return filament.at(index);
}

const size_t ConfigPackFDM::tool_size() const
{
    return tool.size();
}

const size_t ConfigPackFDM::filament_size() const
{
    return filament.size();
}

FindResult ConfigPackFDM::contains(const std::string& key, size_t slot)
{
    ASSERT(slot < tool.size());
    ASSERT(slot < filament.size());
    for (auto& box : ConfigBoxRefs{filament.at(slot), tool.at(slot), print, printer}) {
        if (auto result = box.get().find(key); result.item)
            return result;
    }
    return {};
}

ConstFindResult ConfigPackFDM::contains(const std::string& key, size_t slot) const
{
    ASSERT(slot < tool.size());
    ASSERT(slot < filament.size());
    for (const auto& box : ConstConfigBoxRefs{filament.at(slot), tool.at(slot), print, printer}) {
        if (const auto result = box.get().find(key); result.item)
            return result;
    }
    return {};
}

FindResult ConfigPackSLA::contains(const std::string& key)
{
    for (auto& box : ConfigBoxRefs{sla_material_settings, sla_print_settings, sla_printer_settings})
    {
        if (const auto result = box.get().find(key); result.item)
            return result;
    }
    return {};
}

ConstFindResult ConfigPackSLA::contains(const std::string& key) const
{
    for (const auto& box :
         ConstConfigBoxRefs{sla_material_settings, sla_print_settings, sla_printer_settings})
    {
        if (const auto result = box.get().find(key); result.item)
            return result;
    }
    return {};
}

} // namespace Slic3r::Domain
