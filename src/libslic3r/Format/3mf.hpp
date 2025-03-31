///|/ Copyright (c) Prusa Research 2018 - 2022 Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Format_3mf_hpp_
#define slic3r_Format_3mf_hpp_

#include "libslic3r/Semver.hpp"
#include <boost/optional/optional.hpp>
namespace Slic3r {

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

    class Model;
    struct ConfigSubstitutionContext;
    class DynamicPrintConfig;
    struct ThumbnailData;

    // Returns true if the 3mf file with the given filename is a PrusaSlicer project file (i.e. if it contains a config).
    extern std::pair<bool, std::optional<Semver>> is_project_3mf(const std::string&);

    // Load the content of a 3mf file into the given model and preset bundle.
    extern bool load_3mf(
        const char* path,
        DynamicPrintConfig& config,
        ConfigSubstitutionContext& config_substitutions,
        Model* model,
        bool check_version,
        boost::optional<Semver> &prusaslicer_generator_version
    );

    // Save the given model and the config data contained in the given Print into a 3mf file.
    // The model could be modified during the export process if meshes are not repaired or have no shared vertices
    extern bool store_3mf(const char* path, Model* model, const DynamicPrintConfig* config, bool fullpath_sources, const ThumbnailData* thumbnail_data = nullptr, bool zip64 = true);

} // namespace Slic3r

#endif /* slic3r_Format_3mf_hpp_ */
