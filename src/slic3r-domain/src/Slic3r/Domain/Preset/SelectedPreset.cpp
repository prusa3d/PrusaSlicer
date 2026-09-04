#include "Slic3r/Domain/Preset/SelectedPreset.hpp"

namespace Slic3r::Domain::Preset {
std::string SelectedPreset::bed_model() const
{
    return hw_config.visual.bed_model.value_or(std::string());
}

std::string SelectedPreset::bed_texture() const
{
    return hw_config.visual.bed_texture.value_or(std::string());
}

ConfigPack SelectedPreset::config() const
{
    if (printer.kind == PresetKind::FdmPrinter) {
        ConfigPackFDM config;
        config.printer = std::get<PrinterSettings>(printer.values);
        config.print   = std::get<PrintSettings>(print.values);
        config.tool.resize(tools.size());
        for (size_t i = 0, n = tools.size(); i < n; i++)
            config.tool[i] = std::get<ToolPrintSettings>(tools[i].values);

        config.filament.resize(materials.size());
        for (size_t i = 0, n = materials.size(); i < n; i++)
            config.filament[i] = std::get<FilamentSettings>(materials[i].values);
        return config;
    }
    if (printer.kind == PresetKind::SlaPrinter) {
        ConfigPackSLA config;
        config.sla_printer_settings = std::get<SLAPrinterSettings>(printer.values);
        config.sla_print_settings   = std::get<SLAPrintSettings>(print.values);
        ASSERT(materials.size() == 1);
        config.sla_material_settings = std::get<SLAMaterialSettings>(materials.at(0).values);
        return config;
    }

    PANIC("Unsupported technology");
}

SelectedPreset SelectedPreset::make(const SelectedPresetMetadata& metadata, const ConfigPack& config)
{
    return std::visit(
        overloaded{
            [&](const ConfigPackFDM& typed_config) -> SelectedPreset {
                ASSERT(metadata.hw_config.technology == PrinterTechnology::FFF);

                SelectedPreset ret{
                    .hw_config = metadata.hw_config,
                    .printer   = EvaluatedPrinterPreset::Preset::make(
                        PresetKind::FdmPrinter,
                        metadata.printer,
                        typed_config.printer
                    ),
                    .print = EvaluatedPrintPreset::Preset::make(
                        PresetKind::FdmPrint,
                        metadata.print,
                        typed_config.print
                    ),
                };

                ASSERT(metadata.hw_config.tools.size() == metadata.tools.size());
                ASSERT(typed_config.tool.size() == metadata.tools.size());
                ret.tools.reserve(metadata.tools.size());
                for (size_t i = 0, n = metadata.tools.size(); i < n; i++) {
                    const auto& t  = metadata.tools.at(i);
                    const auto& tc = typed_config.tool.at(i);
                    ret.tools.emplace_back(
                        EvaluatedToolPrintPreset::Preset::make(PresetKind::FdmToolPrint, t, tc)
                    );
                }

                ASSERT(typed_config.filament.size() == metadata.materials.size());
                ret.materials.reserve(metadata.materials.size());
                for (size_t i = 0, n = metadata.materials.size(); i < n; i++) {
                    const auto& t  = metadata.materials.at(i);
                    const auto& tc = typed_config.filament.at(i);
                    ret.materials.emplace_back(
                        EvaluatedMaterialPreset::Preset::make(PresetKind::FdmMaterial, t, tc)
                    );
                }

                return ret;
            },
            [&](const ConfigPackSLA& typed_config) -> SelectedPreset {
                ASSERT(metadata.hw_config.technology == PrinterTechnology::SLA);
                ASSERT(1 == metadata.materials.size());
                return {
                    .hw_config = metadata.hw_config,
                    .printer   = EvaluatedPrinterPreset::Preset::make(
                        PresetKind::SlaPrinter,
                        metadata.printer,
                        typed_config.sla_printer_settings
                    ),
                    .print = EvaluatedPrintPreset::Preset::make(
                        PresetKind::SlaPrint,
                        metadata.printer,
                        typed_config.sla_print_settings
                    ),
                    .materials = {{EvaluatedMaterialPreset::Preset::make(
                        PresetKind::SlaMaterial,
                        metadata.materials.at(0),
                        typed_config.sla_material_settings
                    )}}
                };
            },
        },
        config
    );
}

SelectedPresetMetadata SelectedPreset::metadata() const
{
    SelectedPresetMetadata ret {
        .hw_config = hw_config,
        .printer = printer.metadata(),
        .print = print.metadata(),
    };

    for (const auto& tool : tools)
        ret.tools.emplace_back(tool.metadata());
    for (const auto& material : materials)
        ret.materials.emplace_back(material.metadata());

    return ret;
}

} // namespace Slic3r::Domain::Preset
