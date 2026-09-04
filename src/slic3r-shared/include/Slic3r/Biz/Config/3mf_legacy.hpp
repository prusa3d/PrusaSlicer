#ifndef slic3r_Format_3mfLegacy_hpp_
#define slic3r_Format_3mfLegacy_hpp_

#include "Slic3r/Biz/VirtualExtrudersConfig.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/CustomGCode.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/OnBeds.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/VirtualExtruder.hpp"
#include "Slic3r/Semver.hpp"
#include <optional>
#include <map>

namespace Slic3r {
namespace Biz {
struct LegacyPresetMetadata;
} // namespace Biz
struct ConfigSubstitutionContext;
    class DynamicPrintConfig;
    namespace Domain {
        class Image;
    }
}

namespace Slic3rLegacy {

    /* The format for saving the SLA points was changing in the past. This enum holds the latest version that is being currently used.
     * Examples of the Slic3r_PE_sla_support_points.txt for historically used versions:

     *  version 0 : object_id=1|-12.055421 -2.658771 10.000000
                    object_id=2|-14.051745 -3.570338 5.000000
        // no header and x,y,z positions of the points)

     * version 1 :  ThreeMF_support_points_version=1
                    object_id=1|-12.055421 -2.658771 10.000000 0.4 0.0
                    object_id=2|-14.051745 -3.570338 5.000000 0.6 1.0
        // introduced header with version number; x,y,z,head_size,type)
        // before 2.9.1 fifth float means is_island (bool flag) -> value from 0.9999f to 1.0001f means it is support for island otherwise not. User edited points has always value zero.
        // since 2.9.1 fifth float means type -> starts show user edited points
        // type range value meaning
        // (float is used only for compatibility, string will be better)
        // from    | to     | meaning
        // --------------------------------
        // 0.9999f | 1.0001 | island (no change)
        // 1.9999f | 2.0001 | manual edited points loose info about island
        // 2.9999f | 3.0001 | generated point by slope ration
        // all other values are readed also as slope type

    */

    enum {
        support_points_format_version = 1
    };
    
    enum {
        drain_holes_format_version = 1
    };


    // Returns true if the 3mf file with the given filename is a PrusaSlicer project file (i.e. if it contains a config).
    extern std::pair<bool, std::optional<Slic3r::Semver>> is_project_3mf(const std::string&);

    // Load the content of a 3mf file into the given model and preset bundle.
    extern bool load_3mf_legacy(
        const char* path,
        Slic3r::Domain::ConfigPack& config,
        Slic3r::Biz::LegacyPresetMetadata& preset_metadata,
        Slic3r::Domain::Model* model,
        bool check_version,
        boost::optional<Slic3r::Semver> &prusaslicer_generator_version,
        Slic3r::Domain::WipeTowersOnBeds& wipe_towers,
        Slic3r::Domain::CustomGCodesOnBeds& custom_gcodes,
        Slic3r::Biz::VirtualExtrudersConfig& out_virtual_extruders_config
    );

    // Save the given model and the config data contained in the given Print into a 3mf file.
    // The model could be modified during the export process if meshes are not repaired or have no shared vertices
    extern bool store_3mf_legacy(
        const char* path,
        const Slic3r::Domain::Model* model,
        const Slic3r::Domain::ConfigPack& config,
        const Slic3r::Domain::Preset::HwPrinterConfig& hw_config,
        bool fullpath_sources,
        const Slic3r::Domain::WipeTowersOnBeds& wipe_towers,
        const Slic3r::Domain::CustomGCodesOnBeds& custom_gcodes,
        const Slic3r::Domain::VirtualExtruders& virtual_extruders = {},
        const Slic3r::Domain::Image* thumbnail_data = nullptr,
        bool zip64 = true
    );

} // namespace Slic3r

#endif /* slic3r_Format_3mf_hpp_ */
