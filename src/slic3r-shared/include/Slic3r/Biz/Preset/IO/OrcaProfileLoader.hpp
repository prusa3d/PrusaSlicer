#pragma once

#include "Slic3r/Biz/Preset/IO/BundlePaths.hpp"
#include "Slic3r/Domain/Preset/Bundle.hpp"

namespace Slic3r::Biz::Preset::IO {
std::vector<boost::filesystem::path> orca_profile_roots(const BundlePaths& paths);
// include_prusa is reserved for regression tests; normal imports use native Prusa presets.
void load_orca_profiles(const BundlePaths& paths, Domain::Preset::Bundle& bundle, bool include_prusa = false,
    bool deferred = false);
}
