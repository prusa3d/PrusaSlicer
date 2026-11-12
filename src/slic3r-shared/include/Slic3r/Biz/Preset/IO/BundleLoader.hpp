#pragma once
#include "Slic3r/Domain/Preset/Bundle.hpp"
#include "Slic3r/Biz/Preset/IO/BundlePaths.hpp"

namespace Slic3r::Biz::Preset::IO {

void populate_local_bundle(const BundlePaths& bundle_paths);
Domain::Preset::Bundle load_bundle(const BundlePaths& bundle_paths);

void save_bundle_configs(const Domain::Preset::Bundle& bundle, const std::string& config_path);

void serialize_bundle(
    const std::string& filename,
    const Domain::Preset::Bundle& bundle,
    const BundlePaths& bundle_paths,
    const std::string& slicer_version);
std::optional<Domain::Preset::Bundle> deserialize_bundle(
    const std::string& filename,
    const BundlePaths& bundle_paths,
    const std::string& slicer_version);

}
