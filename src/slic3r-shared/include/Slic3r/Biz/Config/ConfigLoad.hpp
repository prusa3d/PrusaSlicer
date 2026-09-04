#pragma once

#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <tl/expected.hpp>

namespace Slic3r::Biz::Config {

enum ItemParsingIssueType {
    InvalidFormat,
    NotFound,
    ExtraKey,
};

struct ItemParsingIssue {
    ItemParsingIssueType type;
    std::string message;
};

enum GlobalParsingIssue {
    UnableToDeducePrinterTechnology,
    NotAJsonObject,
    InvalidFDMPrinterSettings,
    InvalidFDMToolSettings,
    InvalidFDMPrintSettings,
    InvalidFDMFilamentSettings,
    InvalidFDMProjectSettings,
    InvalidSLAPrinterSettings,
    InvalidSLAMaterialSettings,
    InvalidSLAPrintSettings,
    FilamentsAndToolsCountIsNotEqual
};


using BoxIssues = std::map<std::string, ItemParsingIssue>;
using LocationIssues = std::variant<BoxIssues, std::vector<BoxIssues>>;
using IssuesPerLocation = std::map<Domain::ConfigLocation, LocationIssues>;

struct LoadResult {
    Domain::ConfigPack config;
    IssuesPerLocation issues;
};

tl::expected<LoadResult, GlobalParsingIssue>
load(const nlohmann::ordered_json&, const Domain::Preset::HwPrinterConfig& hw_config);

BoxIssues load_box(const nlohmann::ordered_json& json, Domain::ConfigBox& box);

struct PresetAndConfig
{
    Domain::Preset::SelectedPresetMetadata preset_metadata;
    Domain::ConfigPack config_pack;
};

/**
 * @brief Loads the preset metadata and its configuration from a project-config JSON
 * object that has a "preset" and a "configuration" key.
 *
 * @return The metadata and configuration, or an error message.
 */
tl::expected<PresetAndConfig, std::string> load_preset_and_config(
    const nlohmann::ordered_json& project_config_json
);

}
