#ifndef slic3r_Format_3mf_prusafile_hpp_
#define slic3r_Format_3mf_prusafile_hpp_

#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp" // mz_zip_archive + mz_zip_archive_file_stat
#include "Slic3r/Biz/Format/ResultLoad3mf.hpp" // Error handling
#include "ModelMap.hpp"
#include "Slic3r/Domain/Project.hpp"

namespace Slic3r {
struct ConfigSubstitutionContext;

/// <summary>
/// Write Prusa Slic3r project data, which are not well defined in 3mf specification
/// NOTE: Prusa project file is stored in UTF-8 encoding
/// </summary>
/// <param name="archive">Zip archive of 3mf file for store file</param>
/// <param name="model">Data to store</param>
/// <param name="config">Configuration for whole model</param>
/// <param name="stored_structure">Hierarchy of store model into .model file of 3mf</param>
void store_prusa_files(
    mz_zip_archive &archive,
    const Domain::Model &model,
    const Domain::ProjectMetadata& project_metadata,
    const Domain::Project::ConfigContainerList& config_containers,
    const StoredStructure &stored_structure
);

struct PrusaFilesResult {
    // Flag for each file in 3mf zip archive
    std::vector<bool> used_file_indices; // for detection of unproccessed files
    Domain::ProjectMetadata project_metadata;
    std::vector<Loaded3MF::ConfigContainerData> config_containers_data; // Pack for each config container.
};

/// <summary>
/// Read and apply data stored by function "store_prusa_files"
/// </summary>
/// <param name="archive">Content of 3mf zip archive</param>
/// <param name="model_map">[output]Conversion from 3mf model into current loaded Slic3r::Model</param>
/// <param name="config">[output] configuration for whole model</param>
/// <param name="config_substitutions">Help backward compatibility of configuration(not 100% sure)</param>
/// <returns>Indices of used files </returns>
PrusaFilesResult load_prusa_files(
    mz_zip_archive &archive,
    const ModelMap& model_map,
    Read3mfIssues& collected_issues
);

/// <summary>
/// Read svg file from archive when it is used in model
/// </summary>
/// <param name="archive">Zip archive with svg file. NOTE: read function is not const</param>
/// <param name="stat">File statisctis about svg file - archive file path</param>
/// <param name="model">Contain alredy loaded voloumes</param>
/// <param name="result">List of issues, add issue when svg can't be open</param>
/// <returns>True when svg is used for emboss shape otherwise false</returns>
bool process_embossed_svg(
    /*const*/ mz_zip_archive &archive,
    const mz_zip_archive_file_stat &stat,
    Domain::Model &model,
    Read3mfIssues& collected_issues
);

} // namespace Slic3r
#endif // slic3r_Format_3mf_prusafile_hpp_
