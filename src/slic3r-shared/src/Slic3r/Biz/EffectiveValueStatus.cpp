#include "Slic3r/Biz/EffectiveValueStatus.hpp"

namespace Slic3r::Biz {

EffectiveValueStatus get_effective_value_status(
    const std::string& name,
    const Domain::ConfigLocation& current_location,
    const Domain::EffectiveValues& effective_values)
{
    const auto it{effective_values.print.find(name)};

    using Modification             = Domain::EffectiveValue::Modification;
    using OverridenBy              = Domain::EffectiveValue::OverriddenBy;
    using CompatibilityRuleApplied = Domain::EffectiveValue::CompatibilityRuleApplied;
    using Location                 = Domain::FDMConfigLocation;

    if (it != effective_values.print.end()) {
        const std::vector<Modification>& modifications{it->second.modifications};
        for (const Modification& modification : modifications) {
            const OverridenBy* overriden_by{std::get_if<OverridenBy>(&modification.payload)};
            if (!overriden_by) {
                continue;
            }
            const Location* override_location{std::get_if<Location>(&overriden_by->location)};
            const Location* this_location{std::get_if<Location>(&current_location)};
            if (!override_location || !this_location) {
                continue;
            }
            const auto override_location_index{static_cast<int>(*override_location)};
            const auto this_location_index{static_cast<int>(*this_location)};
            if (this_location_index < override_location_index) {
                return EffectiveValueStatus::Overriden;
            }
        }

        for (const Modification& modification : modifications) {
            const CompatibilityRuleApplied* compatibility_rule_applied{
                std::get_if<CompatibilityRuleApplied>(&modification.payload)};
            if (compatibility_rule_applied) {
                return EffectiveValueStatus::CompatibilityRuleApplied;
            }
        }
        return EffectiveValueStatus::AdditionalInfo;
    }
    return EffectiveValueStatus::None;
}

} // namespace Slic3r::Biz
