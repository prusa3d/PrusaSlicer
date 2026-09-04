#pragma once
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/Preset/EvaluatedPreset.hpp"

namespace Slic3r::Domain::Preset {

using EvaluatedPresetMetadatas = std::vector<EvaluatedPresetMetadata>;
struct SelectedPresetMetadata
{
    HwPrinterConfig hw_config;
    EvaluatedPresetMetadata printer;
    EvaluatedPresetMetadata print;
    EvaluatedPresetMetadatas tools;
    EvaluatedPresetMetadatas materials;
};

struct SelectedPreset
{
    HwPrinterConfig hw_config;
    EvaluatedPrinterPreset::Preset printer;
    EvaluatedPrintPreset::Preset print;
    std::vector<EvaluatedToolPrintPreset::Preset> tools;
    std::vector<EvaluatedMaterialPreset::Preset> materials;

    PrinterTechnology technology() const
    {
        return hw_config.technology;
    }

    [[nodiscard]] std::string bed_model() const;
    [[nodiscard]] std::string bed_texture() const;

    ConfigPack config() const;

    static SelectedPreset make(const SelectedPresetMetadata& metadata, const ConfigPack& config);
    SelectedPresetMetadata metadata() const;
};

}
