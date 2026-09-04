#ifndef slic3r_Format_3mf_buildticket_hpp_
#define slic3r_Format_3mf_buildticket_hpp_

#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp" // mz_zip_archive + mz_zip_archive_file_stat
#include "Model3mf.hpp" // CT_Items
#include "ModelMap.hpp" // InstanceMap

namespace Slic3r {
class DynamicPrintConfig;
struct ConfigSubstitutionContext;

/// <summary>
/// Load build ticket and apply data to configuration.
/// Build ticket is extension of 3mf made by Siemens NX application
/// to set slicing params for models(and objects).
/// </summary>
/// <param name="archive">3mf zip archiv</param>
/// <param name="stat">File statistics of build ticket file</param>
/// <param name="items">Data from '.model' build</param>
/// <param name="instance_map">same size as items keep pointers on instances in model, 
/// [output] for object specific configuration</param>
/// <param name="config">Loaded configuration will be overriden</param>
/// <param name="config_substitutions">used to set specific configuration for object</param>
void process_build_ticket(
    mz_zip_archive& archive,
    const mz_zip_archive_file_stat& stat,
    const format_3MF::CT_Items& items,
    const InstanceMap& instance_map,
    DynamicPrintConfig &config,
    ConfigSubstitutionContext &config_substitutions
);
} // namespace Slic3r
#endif // slic3r_Format_3mf_buildticket_hpp_
