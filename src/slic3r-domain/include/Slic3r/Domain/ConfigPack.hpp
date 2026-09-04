#pragma once

#include "Slic3r/Domain/IConfigPackFDMViewer.hpp"
#include "Slic3r/Domain/ConfigBoxesFDM.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/VirtualExtruder.hpp"

namespace Slic3r::Domain {

struct ConfigPackFDM : public IConfigPackFDMViewer
{
    explicit ConfigPackFDM(const int extruder_count);
    ConfigPackFDM();

    Domain::PrinterSettings printer;
    Domain::PrintSettings print;
    std::vector<Domain::ToolPrintSettings> tool;
    std::vector<Domain::FilamentSettings> filament;
    Domain::ProjectSettings project;
    Domain::VirtualExtruders virtual_extruders;

    bool operator==(const ConfigPackFDM& other) const;

    const PrinterSettings& get_printer() const override;
    const PrintSettings& get_print() const override;
    const ToolPrintSettings& get_tool(size_t index) const override;
    const FilamentSettings& get_filament(size_t index) const override;
    const size_t tool_size() const override;
    const size_t filament_size() const override;

    FindResult contains(const std::string& key, size_t slot);
    ConstFindResult contains(const std::string& key, size_t slot) const;
    void resize_tool_parity_items(int extruder_count, bool ensure_down_size_only);
};

struct ConfigPackSLA
{
    Domain::SLAPrinterSettings sla_printer_settings;
    Domain::SLAMaterialSettings sla_material_settings;
    Domain::SLAPrintSettings sla_print_settings;

    bool operator==(const ConfigPackSLA&) const = default;

    FindResult contains(const std::string& key);
    ConstFindResult contains(const std::string& key) const;
};

using ConfigPack = std::variant<ConfigPackFDM, ConfigPackSLA>;
} // namespace Slic3r::Domain
