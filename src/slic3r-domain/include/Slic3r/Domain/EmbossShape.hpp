#pragma once

#include <string>
#include <optional>
#include <memory> // unique_ptr
#include "Slic3r/Domain/Types.hpp" // Transform3d
#include "Slic3r/Domain/ExPolygon.hpp"
#include <nanosvg/nanosvg.h>

namespace Slic3r::Domain {

struct EmbossProjection{
    // Emboss depth, Size in local Z direction
    double depth = 1.; // [in loacal mm] 
    // NOTE: User should see and modify mainly world size not local

    // Flag that result volume use surface cutted from source objects
    bool use_surface = false;

    bool operator==(const EmbossProjection &other) const {
        return depth == other.depth && use_surface == other.use_surface;
    }

};

// Extend expolygons with information whether it was successfull healed
struct HealedExPolygons{
    ExPolygons expolygons;
    bool is_healed;
    operator ExPolygons&() { return expolygons; }
};

// Help structure to identify expolygons grups
// e.g. emboss -> per glyph -> identify character
struct ExPolygonsWithId
{ 
    // Identificator for shape
    // In text it separate letters and the name is unicode value of letter
    // Is svg it is id of path
    unsigned id;

    // shape defined by integer point contain only lines
    // Curves are converted to sequence of lines
    ExPolygons expoly;

    // flag whether expolygons are fully healed(without duplication)
    bool is_healed = true;
};
using ExPolygonsWithIds = std::vector<ExPolygonsWithId>;

/// <summary>
/// Contain plane shape information to be able emboss it and edit it
/// </summary>
struct EmbossShape 
{
    // shapes to to emboss separately over surface
    ExPolygonsWithIds shapes_with_ids;

    // Only cache for final shape
    // It is calculated from ExPolygonsWithIds
    // Flag is_healed --> whether union of shapes is healed
    // Healed mean without selfintersection and point duplication
    HealedExPolygons final_shape;

    // scale of shape, multiplier to get 3d point in mm from integer shape
    double scale = SCALING_FACTOR;

    // Define how to emboss shape
    EmbossProjection projection;

    // Used only to load legacy 3mf !
    // Before PS 3.0 was used to fix baked volume into object
    std::optional<Transform3d> legacy_fix_3mf_tr;

    struct SvgFile {
        // File(.svg) path on local computer 
        // When empty can't reload from disk
        std::string path;

        // File path into .3mf(.zip)
        // When empty svg is not stored into .3mf file yet.
        // and will create dialog to delete private data on save.
        std::string path_in_3mf;

        // Loaded svg file data.
        // !!! It is not serialized on undo/redo stack 
        std::shared_ptr<NSVGimage> image;

        // Loaded string data from file
        std::shared_ptr<std::string> file_data;

    };
    // When embossing shape is made by svg file this is source data
    std::optional<SvgFile> svg_file;
};
} // namespace Slic3r
