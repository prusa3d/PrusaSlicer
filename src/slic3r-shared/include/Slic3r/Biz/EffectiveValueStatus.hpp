#pragma once

#include <Slic3r/Domain/EffectiveValues.hpp>

#include <string>

namespace Slic3r::Biz {

enum class EffectiveValueStatus {
    None,
    Overriden,
    CompatibilityRuleApplied,
    AdditionalInfo
};

EffectiveValueStatus get_effective_value_status(
    const std::string& name,
    const Domain::ConfigLocation& current_location,
    const Domain::EffectiveValues& effective_values);

} // namespace Slic3r::Biz
