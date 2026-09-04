#pragma once

#include <map>
#include <string>
#include <variant>

#include "Slic3r/Domain/ProjectMetadata.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"

namespace Slic3r::Domain {


struct GCodeMetadata
{
    struct General
    {
        std::string producer;
        std::string producer_version;
        std::string time;
    };

    struct Presets
    {
        std::string vendor;
        std::string repo_id;
        std::string version;

        Preset::EvaluatedPresetMetadata printer;
        Preset::EvaluatedPresetMetadata print;
        Preset::EvaluatedPresetMetadatas tools;
        Preset::EvaluatedPresetMetadatas materials;
    };

    struct Config
    {
        Preset::HwPrinterConfig printer;
        Presets presets;
    };

    using StatsValue = std::variant<double, std::string>;
    struct Stats : std::map<std::string, StatsValue> {};

    General general;
    Config config;
    ConfigPack presets;
    ProjectMetadata project;
    Stats stats;
};


}
