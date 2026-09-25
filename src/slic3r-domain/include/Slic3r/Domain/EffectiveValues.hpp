#pragma once

#include "Slic3r/Domain/ConfigDef.hpp"
#include "Slic3r/Domain/ConfigValue.hpp"
#include "Slic3r/Domain/ObjectID.hpp"

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace Slic3r::Domain {

struct EffectiveValue
{
    struct SetBy
    {
        ConfigLocation location;
        bool operator==(const SetBy&) const = default;
    };

    struct OverriddenBy
    {
        ConfigLocation location;
        bool operator==(const OverriddenBy&) const = default;
    };

    struct CompatibilityRuleApplied
    {
        CompatibilityRule rule;
        // If the values are all the same, no need to use the rule,
        // but the type of the ConfigValue still changes from vector to scalar.
        bool used_rule{false};
        bool operator==(const CompatibilityRuleApplied&) const = default;
    };

    struct SlotParityEnsured
    {
        bool operator==(const SlotParityEnsured&) const = default;
    };

    struct ExpandedFrom
    {
        std::string key;
        bool operator==(const ExpandedFrom&) const = default;
    };

    struct ResolvedFloatOrPercentage
    {
        std::string ratio_over;
        bool operator==(const ResolvedFloatOrPercentage&) const = default;
    };

    using ModificationPayload = std::variant<
        SetBy,
        OverriddenBy,
        CompatibilityRuleApplied,
        SlotParityEnsured,
        ExpandedFrom,
        ResolvedFloatOrPercentage>;

    struct Modification
    {
        ModificationPayload payload;
        ConfigValue value;

        bool operator==(const Modification&) const = default;
    };

    std::vector<Modification> modifications;

    bool operator==(const EffectiveValue&) const = default;
};

using EffectiveValueMap = std::map<std::string, EffectiveValue>;

struct EffectiveValues
{
    bool operator==(const EffectiveValues&) const = default;

    EffectiveValueMap print;
    std::map<ObjectID, EffectiveValueMap> objects;
    std::map<ObjectID, EffectiveValueMap> volumes;
};

} // namespace Slic3r::Domain
