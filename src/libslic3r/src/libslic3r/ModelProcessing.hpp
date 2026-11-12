#pragma once

#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include <vector>

namespace Slic3r::Domain {
class Model;
class ModelObject;
class ModelVolume;
} // namespace Slic3r::Domain

namespace Slic3r {

enum class ConversionType : int {
    CONV_TO_INCH,
    CONV_FROM_INCH,
    CONV_TO_METER,
    CONV_FROM_METER,
};

namespace ModelProcessing
{
    static constexpr const double volume_threshold_inches = 9.0;    // 9 = 3*3*3;
    static constexpr const double volume_threshold_meters = 0.001;  // 0.001 = 0.1*0.1*0.1

    void    convert_to_multipart_object(Domain::Model& model, unsigned int max_extruders);

    void    convert_from_imperial_units(Domain::Model& model, bool only_small_volumes);
    void    convert_from_imperial_units(Domain::ModelVolume* volume);

    void    convert_from_meters(Domain::Model& model, bool only_small_volumes);
    void    convert_from_meters(Domain::ModelVolume* volume);

    void    convert_units(Domain::Model& model_to, Domain::ModelObject* object_from, ConversionType conv_type, std::vector<int> volume_idxs);

    // Get full stl statistics for all object's meshes
    Domain::TriangleMeshStats   get_object_mesh_stats(const Domain::ModelObject* object);
    // Get count of errors in the mesh
    int     get_repaired_errors_count(const Domain::ModelVolume* volume);
    // Get count of errors in the mesh( or all object's meshes, if volume index isn't defined)
    int     get_repaired_errors_count(const Domain::ModelObject* object, const int vol_idx = -1);
    void    merge(Domain::ModelObject* object);
}

} // namespace Slic3r::ModelProcessing
