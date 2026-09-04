#pragma once

#include <optional>
#include <unordered_map>
#include <string_view>
#include <vector>
#include <set>

#include "Slic3r/Domain/Preset/Types.hpp"
#include "Slic3r/Domain/Preset/ParsedExpr.hpp"

namespace Slic3r::Domain::Preset {

enum class ConditionMatchMode
{
    FirstMatch,
    AllMatches
};

enum class PresetOrigin : uint8_t
{
    System,
    User,
    Runtime
};

struct PresetNode
{
    std::string id;
    std::optional<std::string> name;
    std::vector<std::string> inherits;
    std::vector<std::string> unconditional_inherits;
    std::optional<ParsedExpr> condition;
    std::optional<ConditionMatchMode> match_mode;
    PresetValueMap values;
    FeatureValueMap features;
    std::vector<PresetNode> variants;
    SourceLocation source_location;

    std::optional<std::string_view> short_name() const;

};

struct RootPresetNode : PresetNode
{
    PresetKind kind{PresetKind::FdmPrinter};
    PresetOrigin origin{PresetOrigin::System};
    std::optional<std::string> user_file;
};

using Presets = std::vector<RootPresetNode>;
using PresetCollection = std::map<PresetKind, Presets>;

using PresetNodePath = std::vector<const PresetNode*>;
using IdentifiedPresets = std::unordered_map<std::string, PresetNodePath>;
using IdentifiedPresetsCollection = std::map<PresetKind, IdentifiedPresets>;

struct PresetName
{
    std::string name;
    std::set<std::string> id;
    std::set<std::string> root_id;
    PresetOrigin origin;
};

using PresetNames = std::vector<PresetName>;
using PresetNamesCollection = std::map<PresetKind, PresetNames>;


bool is_public_name(const std::string& name);
std::string derive_name(std::string_view name, std::string_view parent_name);
std::string_view short_name(const std::string& name);

} // namespace Slic3r::Domain::Presets
