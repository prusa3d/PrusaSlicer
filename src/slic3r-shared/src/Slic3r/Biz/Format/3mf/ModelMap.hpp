#ifndef slic3r_Format_3mf_ModelMap_hpp_
#define slic3r_Format_3mf_ModelMap_hpp_

#include <string>
#include <vector>
#include <unordered_map>
#include "Model3mf.hpp" // format_3MF::ST_ResourceID

namespace Slic3r {

// Production extension add option to keep object stored in separate .model files.
// Object id is unique only for one .model file not whole 3mf archive.
struct PathId
{
    // Identifier defined for object in 3mf .model file
    // .3mf/3D/*.model xmlPath: model/resources/object attribute id
    format_3MF::ST_ResourceID id;

    // File path inside 3mf archive to source .model file
    // NOTE: For root .model file is path empty
    std::string path;

    // The requirement to be key in unordered_map.
    bool operator==(const PathId &rhs) const { return (id == rhs.id && path == rhs.path); }
};
} // namespace Slic3r

// The requirement to be PathId a key in unordered_map.
template<> struct std::hash<Slic3r::PathId>{
size_t operator()(const Slic3r::PathId &path_id) const {
    return (std::hash<std::string>()(path_id.path) ^
        std::hash<Slic3r::format_3MF::ST_ResourceID>()(path_id.id));
}};

namespace Slic3r {
// forward decalaration

namespace Domain {
    class ModelInstance;
    class ModelObject;
    class ModelVolume;
}

using ModelInstance = Domain::ModelInstance;
using ModelObject = Domain::ModelObject;
using ModelVolume = Domain::ModelVolume;

//                                    3mf object_id, model objects
using BuildMap = std::unordered_map<PathId, std::vector<ModelObject *>>;
using VolumeMap = std::unordered_map<PathId, std::vector<ModelVolume *>>;
using InstanceMap = std::vector<ModelInstance *>; // same size and order as items in build

/// <summary>
/// Help data structure for mapping 3mf model object index to Slic3r::Model
/// </summary>
struct ModelMap
{
    BuildMap build;        // 3mf object id to object pointers
    VolumeMap volumes;     // 3mf object id to volume pointers
    InstanceMap instances; // same size as item in build - not all instances
};

} // namespace Slic3r
#endif // slic3r_Format_3mf_ModelMap_hpp_
