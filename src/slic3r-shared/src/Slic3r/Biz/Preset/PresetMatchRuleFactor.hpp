#pragma once

#include "Slic3r/Domain/Preset/Types.hpp"
#include "Slic3r/Domain/Preset/MatDbMatchRule.hpp"

namespace Slic3r::Biz::Preset {

std::optional<Domain::Preset::MatDbMatchRule> make_match_rule(const Domain::Preset::FeatureValueMap& features);

}
