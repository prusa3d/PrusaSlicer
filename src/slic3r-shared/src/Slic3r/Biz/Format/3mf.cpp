#include "Slic3r/Biz/Format/3mf.hpp"

#include <memory>
#include <set>

#include <boost/algorithm/string/predicate.hpp> // iends_with
#include <boost/filesystem.hpp> // storing exception
#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp" // mini zip archivator
#include "3mf/Relations.hpp"
#include "3mf/Model3mf.hpp"
// #include "3mf/BuildTicket.hpp"
#include "3mf/ModelMap.hpp"
#include "3mf/PrusaFile.hpp"

#include <boost/uuid/uuid_io.hpp> // to_string(uuid)
#include <boost/uuid/uuid_generators.hpp> // generators

#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/Geometry.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Algorithms/ImageUtils.hpp"
#include "Slic3r/Biz/Format/ProjectFileConstants.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/MiniZErrorTranslation.hpp"
#include "Slic3r/Domain/Image.hpp"
#include "Slic3r/Utils.hpp" // ScopeGuard

#include "stb_image_resize2.h"
#include "Slic3r/Biz/Algorithms/PNGReadWrite.hpp"

#include "LocalesUtils.hpp"

using Slic3r::Domain::SquareMatrix3d;

using Slic3r::Biz::_u8L;
using Slic3r::Domain::is_approx;
using Slic3r::Biz::Algorithms::open_zip_reader;
using Slic3r::Biz::Algorithms::close_zip_reader;
using Slic3r::Biz::Algorithms::open_zip_writer;
using Slic3r::Biz::Algorithms::close_zip_writer;

using ModelObject       = Slic3r::Domain::ModelObject;
using ModelVolume       = Slic3r::Domain::ModelVolume;
using ModelInstance     = Slic3r::Domain::ModelInstance;
using Model             = Slic3r::Domain::Model;
using ModelVolumeType   = Slic3r::Domain::ModelVolumeType;
using ModelVolumePtrs   = Slic3r::Domain::ModelVolumePtrs;
using ModelInstancePtrs = Slic3r::Domain::ModelInstancePtrs;
namespace ImageUtils = Slic3r::Biz::Algorithms::ImageUtils;

namespace {
using namespace Slic3r;
using namespace format_3MF;

// Files in 3mf
const std::string RELATIONSHIPS_FILE = "_rels/.rels";
const char* CONTENT_TYPES_FILE       = "[Content_Types].xml";
const std::string THUMBNAIL_FILE     = "Metadata/thumbnail.png";
// name is choosen for back compatibility(Cura + PS)
const std::string MODEL_FILE        = "3D/3dmodel.model";
const std::string BUILD_TICKET_FILE = "3D/Metadata/BuildTicket.mbt"; // for NX import

// NOTE: 3mf with build ticket contain also file "3D\_rels\3dmodel.model.rels" where is path to .mbt
// File looks constant and for now it is not processed

// Keep data from [Content_Types].xml file
// Knows Stored File Types
// OPC defined file
struct ContentTypes
{
    bool contain_svg = false;
};

void store_content_type(mz_zip_archive& archive, const ContentTypes& ct)
{
    std::stringstream stream;
    stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    stream << "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n";
    stream
        << " <Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n";
    stream
        << " <Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>\n";
    stream << " <Default Extension=\"png\" ContentType=\"image/png\"/>\n";
    // add description for project file
    stream << " <Default Extension=\"json\" ContentType=\"application/json\"/>\n";
    // stream << " <Default Extension=\"gcode\" ContentType=\"text/x.gcode\"/>\n";
    if (ct.contain_svg)
        stream << " <Default Extension=\"svg\" ContentType=\"image/svg+xml\"/>\n";
    stream << "</Types>";
    std::string out = stream.str();

    if (!mz_zip_writer_add_mem(
            &archive,
            CONTENT_TYPES_FILE,
            (const void*) out.data(),
            out.length(),
            MZ_DEFAULT_COMPRESSION
        ))
        throw boost::filesystem::filesystem_error("Unable to add content types to archive.", {});
}

void store_thumbnail(mz_zip_archive& archive, const Domain::Image& thumbnail, const char* filepath)
{
    size_t png_size = 0;
    void* png_data  = tdefl_write_image_to_png_file_in_memory_ex(
        thumbnail.pixels.data(),
        thumbnail.width(),
        thumbnail.height(),
        4,
        &png_size,
        MZ_DEFAULT_LEVEL,
        0
    );
    if (png_data == nullptr)
        throw boost::filesystem::filesystem_error(
            "Can't create PNG data for store thumbnail to archive.",
            {}
        );
    ScopeGuard sg_png_data([png_data]() { mz_free(png_data); });

    if (!mz_zip_writer_add_mem(&archive, filepath, (const void*) png_data, png_size, MZ_DEFAULT_COMPRESSION))
        throw boost::filesystem::filesystem_error("Can't add PNG data to archive.", {});
}

///////////////////////////////////////////////
///// READER ////////////////////////

/// <summary>
/// Check that transformation is valid for instances.
/// It means that transformation is only rotation around z axis with translation.
/// Instances are used for speed up slicing(result gcode is for each instance the same)
/// </summary>
/// <param name="tr1">First transformation</param>
/// <param name="tr2">Second transformation</param>
/// <returns>True for instance transformation otherwise false</returns>
bool can_be_instance(const Transform3d& tr1, const Transform3d& tr2)
{
    // check there is same z offset
    if (!is_approx(tr1.translation()[2], tr2.translation()[2]))
        return false;

    // transformation between without offset
    SquareMatrix3d between = tr1.linear() * tr2.linear().inverse();

    // only rotation around z axis is allowed
    // _____________________
    // |  cos  | -sin  |  0  | col(0) -> norm = 1
    // |  sin  |  cos  |  0  | col(1) -> norm = 1
    // |   0   |   0   |  1  | col(2) -> norm = 1
    // ---------------------
    if (!is_approx(between.coeffRef(0, 2), 0.)
        || !is_approx(between.coeffRef(1, 2), 0.)
        || !is_approx(between.coeffRef(2, 2), 1.)
        || !is_approx(between.coeffRef(2, 1), 0.)
        || !is_approx(between.coeffRef(2, 0), 0.)
        || !is_approx(between.coeffRef(0, 0), between.coeffRef(1, 1))
        || // cos
        !is_approx(between.coeffRef(1, 0), -between.coeffRef(0, 1))
        || // sin
        !is_approx(between.col(0).norm(), 1.)
        || // no scale
        !is_approx(between.col(1).norm(), 1.))
        return false;

    // check that there is NOT scale between
    assert(is_approx(between.col(2).norm(), 1.));

    // Found similar implementation - check that it is also positive
    assert(
        Slic3r::Biz::Algorithms::Geometry::trafos_differ_in_rotation_by_z_and_mirroring_by_xy_only(tr1, tr2)
    );

    return true;
}

ModelMap add_build_instances(Slic3r::Domain::Model& model, const CT_Items& items)
{
    ModelMap model_map;
    model_map.instances = InstanceMap(items.size(), {nullptr});
    BuildMap& build_map = model_map.build;
    for (const CT_Item& item : items) {
        // By 3mf spec obejct_id start from value 1 and increase
        assert(item.object_id != 0);
        if (item.object_id == 0)
            // issue(model_item_require_objectid_attr) is added during read
            continue;

        PathId path_id{item.object_id};
        if (!item.path.empty())
            // Addresing to another file!
            path_id.path = item.path; // copy

        ModelObject* model_object = nullptr;
        auto it                   = build_map.find(path_id);
        if (it != build_map.end()) {
            // same object_id in build could be interpreted as instance
            // it depends on transformation matrices
            for (ModelObject* mo : it->second) {
                const Transform3d& mi_tr = mo->instances.front()->get_matrix();
                if (can_be_instance(mi_tr, item.transform)) {
                    model_object = mo;
                    break;
                }
            }
        }
        // object_id is not in build_map yet
        // OR transformation is not compatible to instance
        if (model_object == nullptr) { // add new object()
            model_object = model.add_object();
            if (it == build_map.end()) {
                build_map[path_id] = {model_object};
            } else {
                it->second.push_back(model_object);
            }
        }

        // IMPROVE: keep part number in more reasonable shape
        // Concatenate name with part number
        if (!item.part_number.empty()) {
            if (model_object->name.empty())
                model_object->name = item.part_number;
            else
                model_object->name += "+" + item.part_number;
        }

        ModelInstance* instance_ptr = model_object->add_instance();
        instance_ptr->set_transformation(Domain::Transformation(item.transform));
        model_map.instances[&item - &items.front()] = instance_ptr;
    }
    return model_map;
}

struct ObjectCompose
{
    // for object with mesh
    // volumes are stored in temp_model.objects.front()
    const ModelVolume* volume = nullptr;
    // for object with components
    // objects are stored in temp_model.objects
    const ModelObject* object = nullptr;

    // Trace in tree of Object dependecy for volume -> first id is mesh id(object without component)
    using ObjectTrace = std::vector<PathId>;
    // same size and order as object::volumes
    // keep track tree of .model objectId
    using ObjectTraces = std::vector<ObjectTrace>;
    ObjectTraces volume_traces;

    // flag that this is root of object tree(defined by componnents)
    bool is_top = true;
};

using ObjectMap = std::unordered_map<Slic3r::PathId, ObjectCompose>;

bool process_object_with_components(
    const CT_Object& object_3mf,
    ObjectMap& object_map,
    Domain::Model& temp_model,
    const std::string* filepath = nullptr // set only when it is not root model
)
{
    // object with mesh SHOULD NOT contain componnents
    assert(object_3mf.mesh.its.empty());

    // process Object with componnents
    assert(!object_3mf.components.empty());
    if (object_3mf.components.empty())
        return true;

    PathId path_id{object_3mf.id};
    if (filepath != nullptr)
        path_id.path = *filepath;

    // current object can't be in object map yet
    assert(object_map.find(path_id) == object_map.end());
    ModelObject* mo = temp_model.add_object();
    mo->name        = object_3mf.name;

    // helper for source id from 3mf model
    ObjectCompose::ObjectTraces volume_traces;
    for (const CT_Component& component : object_3mf.components) {
        assert(component.object_id != 0);
        if (component.object_id == 0)
            continue; // issue(model_component_require_objectid_attr) is added during read

        // identification of object
        PathId path_id_c{component.object_id};
        if (!component.path.empty()) {
            path_id_c.path = component.path;
        } else if (filepath != nullptr) {
            path_id_c.path = *filepath;
        }

        ObjectMap::iterator it_object = object_map.find(path_id_c);
        if (it_object == object_map.end()) {
            // unfinishabled object now
            temp_model.delete_object(mo);
            return false;
        }

        ObjectCompose& compose = it_object->second;
        compose.is_top         = false; // exist addresing
        if (compose.volume != nullptr) {
            // component is mesh
            ModelVolume* mv = mo->add_volume(*compose.volume);
            // A single 3mf mesh/volume resource can be referenced by more than one
            // component (e.g. two objects sharing one mesh). add_volume() copies the
            // id of compose.volume, so every extra reference needs a fresh id to keep
            // ObjectIDs unique across the Model (see Model::assert_is_valid()).
            mv->set_new_unique_id();
            volume_traces.push_back({path_id_c});
            if (mv->type() != ModelVolumeType::MODEL_PART)
                mv->set_type(ModelVolumeType::MODEL_PART);
            mv->set_transformation(component.transform);
            continue;
        }

        // transform volumes to current object
        const ModelVolumePtrs component_volumes = compose.object->volumes;
        for (size_t vi = 0; vi < component_volumes.size(); vi++) {
            const ModelVolume* component_volume = component_volumes[vi];
            ModelVolume* mv                     = mo->add_volume(*component_volume); // push back
            // same object can be referenced by multiple components, give this copy its own id
            mv->set_new_unique_id();
            // transform by current transformation
            mv->set_transformation(component_volume->get_matrix() * component.transform);
            if (!compose.object->name.empty())
                mv->name = mv->name.empty() ? compose.object->name :
                                              (compose.object->name + " | " + mv->name);

            // trace source object id path for volume
            ObjectCompose::ObjectTrace volume_trace = compose.volume_traces[vi]; // copy
            volume_trace.push_back(path_id_c); // extend with current id
            volume_traces.push_back(volume_trace);
        }
    }

    object_map[path_id] = ObjectCompose{nullptr, mo, volume_traces};
    return true;
}

// Return true when processed otherwise false
bool move_mesh(CT_Object& object_3mf, ModelObject& temp_object, ObjectMap& object_map, const std::string* path)
{
    // By 3mf core spec CT_Object should contain mesh OR components
    if (object_3mf.mesh.its.empty() && object_3mf.components.empty())
        return true; // Can't be both empty

    if (object_3mf.id == 0)
        return true; // issue(model_object_require_id_attr) is added during read

    // is Object with mesh?
    if (!object_3mf.mesh.its.empty()) {

        // Check that no triangle references non-existent vertex.
        indexed_triangle_set& its = object_3mf.mesh.its;
        int vertices_cnt = int(its.vertices.size());
        for (size_t i=0; i<its.indices.size(); ++i) {
            for (size_t j=0; j<3; ++j) {
                if (its.indices[i][j] < 0 || its.indices[i][j] >= vertices_cnt)
                    throw Loaded3MFException(Read3mfIssue(Read3mfIssueType::unknown, "File contains a corrupted mesh."));
            }
        }

        Domain::TriangleMeshStats stats = Biz::Algorithms::TriangleMesh::calculate_stats(
            object_3mf.mesh.its
        );
        Domain::TriangleMesh tm(std::move(object_3mf.mesh.its), std::move(stats));
        
        ModelVolume* vol = Biz::Algorithms::ModelObject::add_volume(
            &temp_object,
            std::move(tm),
            Domain::ModelVolumeType::MODEL_PART
        );

        // copy name of volume
        vol->name = object_3mf.name;

        PathId path_id{object_3mf.id, path ? *path : std::string()};
        // object id should be unique and not rewrite data in map
        assert(object_map.find(path_id) == object_map.end());
        object_map[path_id] = ObjectCompose{vol};
        return true;
    }
    return false;
}

/// <summary>
/// Move objects triangles from objects_3mf into temp_model
/// NOTE: objects_3mf is not const because mesh(indexed_triangle_set) is moved into temp_model!
/// </summary>
/// <param name="objects_3mf">Source object</param>
/// <param name="temp_model">Temporary storage for object</param>
/// <param name="object_map">Conversion from object_3mf to Slic3r::ModelObject</param>
/// <param name="path">For root .model it is null otherwise it contains .model path</param>
/// <returns>Indices of not moved objects - not core 3mf</returns>
std::vector<size_t> move_objects(/*const*/ CT_Objects& objects_3mf,
                                 Domain::Model& temp_model,
                                 ObjectMap& object_map,
                                 const std::string* path = nullptr)
{
    // keep meshes (all from one .model file) inside of temp_object's volumes
    ModelObject& temp_object = *temp_model.add_object();

    // In case that component is defined before mesh,
    // index of CT_Object will be stored to queue to process later.
    // indices into resource.objects on unproccessabled objects
    // 3mf Core specification disallowe it!
    std::vector<size_t> queue_objects;

    for (CT_Object& object_3mf : objects_3mf) {
        // contain Object mesh?
        if (move_mesh(object_3mf, temp_object, object_map, path))
            continue;

        // contain Object componnets?
        if (!process_object_with_components(object_3mf, object_map, temp_model, path)) {
            // component with unknown object yet - set to process next loop
            queue_objects.push_back(&object_3mf - &objects_3mf.front());
        }
    }
    return queue_objects;
}

void add_unique(VolumeMap& volume_map, const PathId& path_id, ModelVolume* mv)
{
    auto it = volume_map.find(path_id);
    if (it == volume_map.end()) {
        // first initial insert
        volume_map.emplace(path_id, ModelVolumePtrs{mv});
        return;
    }
    ModelVolumePtrs& ptrs = it->second;
    auto bound            = std::upper_bound(ptrs.begin(), ptrs.end(), mv);
    if (bound != ptrs.end() && *bound == mv)
        return; // second insertation
    // sorted insert
    ptrs.insert(bound, mv);
}

VolumeMap fill_build_objects(const BuildMap& build_map, ObjectMap& object_map)
{
    VolumeMap volume_map;
    for (const auto& [path_id, objects] : build_map) {
        auto it = object_map.find(path_id);
        if (it == object_map.end())
            continue; // issue(model_item_unknown_objectid) is set during read

        ObjectCompose& compose = it->second;
        // flag used object
        compose.is_top = false;

        for (ModelObject* object : objects) {
            if (compose.volume != nullptr) {
                // direct geometry
                ModelVolume* mv = object->add_volume(*compose.volume);
                // objects vector may hold several independent build-plate duplicates of
                // the same 3mf object_id (non-instance duplicates); each needs its own id.
                mv->set_new_unique_id();
                add_unique(volume_map, path_id, mv);
                object->name = compose.volume->name;
                continue;
            }
            // compose MUST have volume OR object
            assert(compose.object != nullptr);
            if (compose.object == nullptr)
                continue;

            object->name            = compose.object->name;
            ModelVolumePtrs volumes = compose.object->volumes;
            // copy volumes
            for (size_t vi = 0; vi < volumes.size(); vi++) {
                ModelVolume* mv = object->add_volume(*volumes[vi]);
                // same reason as above: give each independent copy its own id
                mv->set_new_unique_id();
                // whole path will know ModelVolume pointer
                for (const PathId& source_path_id : compose.volume_traces[vi])
                    add_unique(volume_map, source_path_id, mv);
            }
        }
    }
    return volume_map;
}

void add_nonprintable_objects(Slic3r::Domain::Model& to, const ObjectMap& object_map)
{
    for (const auto& [id, compose] : object_map) {
        if (!compose.is_top)
            continue; // geometry is already in model

        if (compose.volume != nullptr) {
            // direct geometry
            ModelObject* mo   = to.add_object();
            ModelInstance* mi = mo->add_instance();
            mi->printable     = false;
            mo->add_volume(*compose.volume);
            continue;
        }

        // compose MUST have volume OR object
        assert(compose.object != nullptr);
        if (compose.object == nullptr)
            continue;

        ModelObject* mo   = to.add_object(*compose.object);
        ModelInstance* mi = mo->add_instance();
        mi->printable     = false;
    }
}

bool check_pointer(const ModelMap& mm, const Slic3r::Domain::Model& m)
{
    Domain::ModelObjectPtrs objects;
    for (const auto& [id, mos] : mm.build)
        objects.insert(objects.end(), mos.begin(), mos.end());
    ModelVolumePtrs volumes;
    for (const auto& [id, mvs] : mm.volumes)
        volumes.insert(volumes.end(), mvs.begin(), mvs.end());

    // Check that every build-mapped object/volume/instance actually lives in the model.
    // Note: m.objects also contains non-printable objects (added by add_nonprintable_objects)
    // that are intentionally absent from mm.build, so we must iterate mm.build, not m.objects.
    for (const ModelObject* mo : objects) {
        bool in_model = std::any_of(m.objects.begin(), m.objects.end(),
            [mo](const ModelObject* o) { return o == mo; });
        if (!in_model) {
            assert(false); // build-mapped object is missing from model
            return false;
        }

        for (const ModelVolume* mv : mo->volumes) {
            bool in_volumes = std::any_of(volumes.begin(), volumes.end(),
                [mv](const ModelVolume* v) { return v == mv; });
            if (!in_volumes) {
                assert(false); // volume is missing from volume map
                return false;
            }
        }

        for (const ModelInstance* mi : mo->instances) {
            bool in_instances = std::any_of(mm.instances.begin(), mm.instances.end(),
                [mi](const ModelInstance* i) { return i == mi; });
            if (!in_instances) {
                assert(false); // instance is missing from instance map
                return false;
            }
        }
    }
    return true;
}

bool contain_producution_extension(const format_3MF::Model& model)
{
    return !model.prod_ns.empty();
}

/// <summary>
/// Move geometry from 3mf format model into PrusaSlicer Model data type
/// </summary>
/// <param name="from">Model loaded from 3mf,
/// NOTE: from is not const because mesh(indexed_triangle_set) is moved out of it! </param>
/// <param name="to">PrusaSlicer model</param>
/// <returns>Object instances, Index corespond to item in build</returns>
ModelMap move_model(/*const*/ LoadedModel& from,
                    Slic3r::Domain::Model& to,
                    Read3mfIssues& collected_issues)
{
    format_3MF::Model& from_root = *from.model;
    // Keep first object as collector for all volumes(3mf object with geometry - without components)
    // [from 0 to N useages of volumes]
    // Other obejcts represents object compose from componenets which must point to volumes
    Slic3r::Domain::Model temp_model;

    // object map point into temporary model
    ObjectMap object_map;
    if (contain_producution_extension(from_root)) {
        // fill objects from submodels into object_map
        for (auto& [path, sub_model] : from.sub_models) {
            std::vector<size_t> not_processed_objects = move_objects(
                sub_model.resource.objects,
                temp_model,
                object_map,
                &path
            );
            if (!not_processed_objects.empty())
                collected_issues.add_issue(
                    Read3mfIssue(Read3mfIssueType::model_object_contain_unknown_componenet, path)
                );
        }
    }
    std::vector<size_t> not_processed_objects = move_objects(
        from_root.resource.objects,
        temp_model,
        object_map
    );
    if (!not_processed_objects.empty())
        collected_issues.add_issue(
            Read3mfIssue(Read3mfIssueType::model_object_contain_unknown_componenet)
        );

    // fill build and instances
    ModelMap model_map = add_build_instances(to, from_root.build.items);

    // fill build objects by object_map from temp_model
    model_map.volumes = fill_build_objects(model_map.build, object_map);

    // Add top most 3mf::object not referenced in build as invisible object into Slic3r::model
    add_nonprintable_objects(to, object_map);
    assert(check_pointer(model_map, to));
    return model_map;
}

const CT_Object* get_object(const PathId& path_id, const LoadedModel& loaded_model)
{
    const format_3MF::Model* model_3mf_ptr = &(*loaded_model.model);
    if (!path_id.path.empty()) {
        auto it_model = loaded_model.sub_models.find(path_id.path);
        assert(it_model != loaded_model.sub_models.end());
        if (it_model == loaded_model.sub_models.end())
            return nullptr;
        model_3mf_ptr = &it_model->second; // set model to sub model
    }
    const CT_Objects& objects_3mf = model_3mf_ptr->resource.objects;
    auto obj_it                   = std::find_if(
        objects_3mf.begin(),
        objects_3mf.end(),
        [id = path_id.id](const CT_Object& obj) { return obj.id == id; }
    );

    assert(obj_it != objects_3mf.end());
    if (obj_it == objects_3mf.end())
        return nullptr;
    return &(*obj_it);
}

} // namespace

namespace Slic3r {

Loaded3MF load_3mf(const std::string& filepath_3mf)
{
    Read3mfIssues collected_issues;

    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);
    std::string filepath_str{filepath_3mf};
    // TODO: change interface to acceppt string_view
    if (!open_zip_reader(&archive, filepath_str))
        throw Loaded3MFException(Read3mfIssue(
            Read3mfIssueType::zip_error,
            _u8L("Unable to open archive: ") + Biz::translate_miniz_error(archive.m_last_error)
        ));

    ScopeGuard sg_archive([&archive]() { close_zip_reader(&archive); });

    tl::expected<LoadedRelations, Read3mfIssue> relations = load_relations(
        archive,
        RELATIONSHIPS_FILE.c_str(),
        collected_issues
    );
    LoadedModel loaded_model = read_model3mf(
        archive,
        relations.has_value() ? relations.value().get_main_model_path() : "3D/3dmodel.model",
        collected_issues
    );

    if (collected_issues.has_issue(Read3mfIssueType::legacy_loader_required))
        throw Loaded3MFException(Read3mfIssue(Read3mfIssueType::legacy_loader_required));

    ModelMap model_map;
    Domain::Model model;

    if (!loaded_model.model.has_value()) {
        // model is not loaded
        // Is it necessary to do anything?
        // return set_result(model, filepath_3mf, std::move(result));
    } else {
        // When not PS 3mf convert model_3mf into model by general rules
        model_map = move_model(loaded_model, model, collected_issues);

        // Set the source file path for each loaded model object.
        const std::string fallback_name = boost::filesystem::path(filepath_3mf).stem().string();
        for (size_t object_idx = 0; object_idx < model.objects.size(); ++object_idx) {
            ModelObject* model_object = model.objects[object_idx];
            model_object->input_file  = filepath_3mf;

            // Use the 3MF filename as a fallback name for objects without one (e.g. BambuStudio/OrcaSlicer 3MFs).
            if (model_object->name.empty()) {
                model_object->name = model.objects.size() > 1 ?
                    fallback_name + "_" + std::to_string(object_idx + 1) :
                    fallback_name;
            }
        }
    }

    PrusaFilesResult prusa_files_result = load_prusa_files(archive, model_map, collected_issues);

    std::vector<bool>& used_files = prusa_files_result.used_file_indices;
    assert(used_files.size() == mz_zip_reader_get_num_files(&archive));
    assert(used_files.size() == loaded_model.used_files.size());
    for (size_t i = 0; i < loaded_model.used_files.size(); ++i)
        if (loaded_model.used_files[i]) {
            assert(!used_files[i]);
            used_files[i] = true;
        }
    if(relations.has_value())
        used_files[relations->realtions_file_index] = true;
    bool found_content_file = false;

    // Loop all files in archive
    mz_zip_archive_file_stat stat;
    mz_uint num_entries = static_cast<mz_uint>(used_files.size());
    for (mz_uint i = 0; i < num_entries; ++i) {
        if (used_files[i])
            continue; // already processed

        if (!mz_zip_reader_file_stat(&archive, i, &stat)) {
            collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::cant_read_file_stats, std::to_string(i)));
            continue; // can't read filename
        }

        std::string name(stat.m_filename);

        // QUESTION: When it appears(OR on which platform??), that miniz change notation of the filepath?
        // TODO: Next line is unneccessary and SHOULD be removed. (@Filip opinion)
        std::replace(name.begin(), name.end(), '\\', '/');

        if (boost::algorithm::iequals(name, CONTENT_TYPES_FILE)) {
            // Do not use content types file, so App skip it for now
            found_content_file = true;
            continue;
        } else if (boost::algorithm::iequals(name, THUMBNAIL_FILE)) {
            // Do not report unprocessed thumbnail file
            continue;
        } else if (boost::algorithm::ends_with(name, ".svg") &&
            process_embossed_svg(archive, stat, model, collected_issues)) {
            continue;
        //} else if (boost::algorithm::iequals(name, BUILD_TICKET_FILE)) {
        //    process_build_ticket(archive, stat, model_3mf.build.items, model_map.instances, config, config_substitutions);
        } else {
            collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::unprocessed_file_in_3mf, name, std::to_string(i)));
        }
    }

    if (!found_content_file)
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::content_types_file_missing, std::string(CONTENT_TYPES_FILE)));

    Loaded3MF loaded_3mf;
    loaded_3mf.metadata               = prusa_files_result.project_metadata;
    loaded_3mf.model                  = std::move(model);
    loaded_3mf.filepath_3mf           = filepath_3mf;
    loaded_3mf.config_containers_data = prusa_files_result.config_containers_data;
    loaded_3mf.issues_map             = std::move(collected_issues);

    if (loaded_model.model) {
        const auto& meta = loaded_model.model->metadata;
        auto it          = std::find_if(meta.begin(), meta.end(), [](const ModelMetadata& m) {
            return (
                std::holds_alternative<ModelMetadataNames>(m.name)
                && std::get<ModelMetadataNames>(m.name) == ModelMetadataNames::Application
                && boost::starts_with(m.value, "PrusaSlicer-")
            );
        });
        if (it != meta.end()) {
            Semver version;
            version.parse(it->value.substr(12));
            if (version.valid())
                loaded_3mf.version = version;
        }
    }

    // Fail fast here rather than later (e.g. on the next preset change), where the
    // stack trace no longer points at the 3mf loading code that produced the bad Model.
    loaded_3mf.model.assert_is_valid();

    return loaded_3mf;
}

void store_3mf(const std::string& filepath, const Domain::Project& project, const Store3mfParam& param)
{
    // check input
    assert(!filepath.empty());
    if (filepath.empty())
        throw boost::filesystem::filesystem_error("Empty filepath", {});

    // All export should use "C" locales for number formatting.
    CNumericLocalesSetter locales_setter;

    // open zip archive
    mz_zip_archive archive;
    mz_zip_archive* archive_ptr = &archive;
    mz_zip_zero_struct(archive_ptr);
    if (!open_zip_writer(archive_ptr, filepath))
        throw boost::filesystem::filesystem_error("Unable to open Zip writer to the file.", {});
    ScopeGuard sg_archive([&archive_ptr]() {
        if (archive_ptr)
            close_zip_writer(archive_ptr);
    });

    // First of all should be in archive stored relations,
    // when you read it you need to know where is root model
    RootRelations relations{
        // main_model_path
        (!project.model().objects.empty()) ? MODEL_FILE : std::string(),
        // thumbnail_path
        (param.thumbnail != nullptr && ImageUtils::is_valid(*param.thumbnail)) ? THUMBNAIL_FILE : std::string(),
        // project_file_path
        std::string{Biz::Format::ProjectFileConstants::PRUSA_PROJECT_FILEPATH}
    };
    store(archive, get_relationships(relations), RELATIONSHIPS_FILE.c_str());

    // Adds content types file ("[Content_Types].xml";).
    // The content of this file is the same for each PrusaSlicer 3mf.
    ContentTypes content_types; // set default values
    store_content_type(archive, content_types);

    // Write thumbnail into 3mf file
    if (!relations.thumbnail_path.empty() && param.thumbnail != nullptr && ImageUtils::is_valid(*param.thumbnail))
    {
        // Adds the file Metadata/thumbnail.png.
        store_thumbnail(archive, *param.thumbnail, relations.thumbnail_path.c_str());
    }

    // Adds model file ("3D/3dmodel.model").
    StoredStructure stored_structure = store_model3mf(archive, project.model(), MODEL_FILE.c_str(), param);

    // Add Prusa project files as structured JSONs
    store_prusa_files(
        archive,
        project.model(),
        project.metadata(),
        project.config_containers(),
        stored_structure
    );

    if (!mz_zip_writer_finalize_archive(archive_ptr))
        throw boost::filesystem::filesystem_error("Unable to finalize the archive.", {});

    // Set off ScopeGuard sg_archive
    archive_ptr = nullptr;
    if (!close_zip_writer(&archive))
        throw boost::filesystem::filesystem_error("Unable to close zip writer.", {});
}

static Domain::Image resize_and_crop(
    const std::vector<unsigned char>& data,
    const int width,
    const int height,
    const int width_new,
    const int height_new
)
{
    const float scale_x     = float(width_new) / width;
    const float scale_y     = float(height_new) / height;
    const float scale       = std::max(scale_x, scale_y); // Choose the larger scale to fill the box
    const int resized_width = int(width * scale);
    const int resized_height = int(height * scale);

    std::vector<unsigned char> resized_rgba(resized_width * resized_height * 4);
    stbir_resize_uint8_linear(
        data.data(),
        width,
        height,
        4 * width,
        resized_rgba.data(),
        resized_width,
        resized_height,
        4 * resized_width,
        STBIR_RGBA
    );

    Domain::Image th(Domain::PixelFormat::RGBA8, width_new, height_new);

    const int crop_x = (resized_width - width_new) / 2;
    const int crop_y = (resized_height - height_new) / 2;

    for (int y = 0; y < height_new; ++y) {
        std::memcpy(
            th.pixels.data() + y * width_new * 4,
            resized_rgba.data() + ((y + crop_y) * resized_width + crop_x) * 4,
            width_new * 4
        );
    }

    return th;
}

std::vector<Domain::Image> get_thumbnail_images_from_3mf(const std::string& input_file, const std::vector<Domain::Size>& sizes)
{
    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);

    if (!open_zip_reader(&archive, input_file))
        return {};

    int index = mz_zip_reader_locate_file(&archive, "Metadata/thumbnail.png", nullptr, 0);
    if (index < 0) {
        close_zip_reader(&archive);
        return {};
    }

    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&archive, index, &stat)) {
        close_zip_reader(&archive);
        return {};
    }

    std::string buffer;
    buffer.resize(int(stat.m_uncomp_size));
    mz_bool res = mz_zip_reader_extract_file_to_mem(&archive, stat.m_filename, buffer.data(), (size_t)stat.m_uncomp_size, 0);
    close_zip_reader(&archive);

    if (res == 0)
        return {};

    std::vector<unsigned char> data;
    unsigned width = 0;
    unsigned height = 0;
    if (!png::decode_png(buffer, data, width, height))
        return {};

    std::vector<Domain::Image> results;
    for (const Domain::Size& size : sizes) {
        results.emplace_back(resize_and_crop(data, width, height, size.width, size.height));
    }

    return results;
}

} // namespace Slic3r
