#pragma once

#include <string>
#include <optional>
#include "Slic3r/Biz/Algorithms/ZipArchive.hpp"
#include "Slic3r/Biz/Format/ResultLoad3mf.hpp"

#include "tl/expected.hpp"

namespace Slic3r {

using Biz::Algorithms::ZipArchive;

struct Relationship{
    std::string target; // path to target file in archive
    std::string type; // schema
};
using Relationships = std::vector<Relationship>;

// Keep data from root relationships file
struct RootRelations
{
    std::string main_model_path;
    std::string thumbnail_path;
    std::string project_file_path;
};
Relationships get_relationships(const RootRelations &root_relations);
Relationships get_relationships(const std::vector<std::string>& model_paths);

struct LoadedRelations {
    std::optional<RootRelations> relations;
    int realtions_file_index = 0;
    const char *get_main_model_path() const;

    LoadedRelations() = default;
    LoadedRelations(RootRelations &&relations) : relations{relations} {}
};

// 3MF Import / Export functions
void store(ZipArchive &archive, const Relationships &relationships, const char *filepath);

// Read relations XML from "_rels/.rels" get index of root model
tl::expected<LoadedRelations, Read3mfIssue> load_relations(ZipArchive &archive, const char *filepath, Read3mfIssues& collected_issues);

} // namespace Slic3r
