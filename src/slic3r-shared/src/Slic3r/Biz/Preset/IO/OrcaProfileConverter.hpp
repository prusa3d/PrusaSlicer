#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Slic3r::Biz::Preset::IO::Orca {

// The application supplies its current schema; no frozen PS3 schema is shipped.
using Json = nlohmann::json;
using Schema = std::map<std::string, Json>;

struct Vendor
{
    std::string id;
    std::string name;
    std::string version;
    bool cache_hit = false;
    std::map<std::string, std::filesystem::path> assets;
    Json machines = Json::array();
    Json presets = Json::array();
    Json diagnostics = Json::array();
    std::map<std::pair<std::string, std::string>, size_t> diagnostic_indices;
};

// PrusaSlicer ships native Prusa presets. Conversion of that vendor is only
// enabled explicitly by regression tests.
// An optional persistent conversion cache, rooted at presets/local. Each vendor
// is validated independently against its source content and conversion schema.
std::vector<Vendor> convert(const std::filesystem::path& root, const Schema& schema, bool include_prusa = false,
    const std::filesystem::path& cache_root = {}, const std::set<std::string>& loaded_vendors = {});
std::string rewrite_gcode(const std::string& source, const std::map<std::string, std::string>& context = {});

} // namespace Slic3r::Biz::Preset::IO::Orca
