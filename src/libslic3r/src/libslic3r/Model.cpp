#include "Model.hpp"
#include "libslic3r.h"
#include "Slic3r/Exception.hpp"

#include "Geometry/ConvexHull.hpp"
#include "MTUtils.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

#include <float.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/iostream.hpp>

#include <oneapi/tbb/parallel_for.h>
#include <tbb/concurrent_vector.h>

#include "Slic3r/Biz/Algorithms/ModelInstance.hpp"
#include "Slic3r/Biz/Algorithms/SVG.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include <Eigen/Dense>
#include "libslic3r/GCode/GCodeWriter.hpp"

namespace Slic3r {

using Biz::Algorithms::BoundingBox::merge;
using Biz::Algorithms::BoundingBox::center;
using Biz::Algorithms::BoundingBox::sizes;
using Biz::Algorithms::BoundingBox::translated;
using Domain::Transformation;
using Biz::Algorithms::Geometry::rotation_diff_z;
using Biz::Algorithms::ModelInstance::transformed_bounding_box;
using Biz::Algorithms::ModelInstance::transform_mesh;
using Domain::extract_rotation;
using Biz::Algorithms::TriangleSelector;
using Domain::BoundingBox3d;
using Domain::TriangleMesh;
using Domain::TriangleSelector::TriangleStateType;
using Domain::indexed_triangle_set_with_color;

namespace tm = Slic3r::Biz::Algorithms::TriangleMesh;

// Test whether the two models contain the same number of ModelObjects with the same set of IDs
// ordered in the same order. In that case it is not necessary to kill the background processing.
bool model_object_list_equal(const Domain::ModelObjectPtrs &old_objects, const Domain::ModelObjectPtrs &new_objects)
{
    if (old_objects.size() != new_objects.size())
        return false;
    for (size_t i = 0; i < old_objects.size(); ++ i)
        if (old_objects[i]->id() != new_objects[i]->id())
            return false;
    return true;
}

// Test whether the new model is just an extension of the old model (new objects were added
// to the end of the original list. In that case it is not necessary to kill the background processing.
bool model_object_list_extended(const Domain::Model &model_old, const Domain::Model &model_new)
{
    if (model_old.objects.size() >= model_new.objects.size())
        return false;
    for (size_t i = 0; i < model_old.objects.size(); ++ i)
        if (model_old.objects[i]->id() != model_new.objects[i]->id())
            return false;
    return true;
}

template<typename TypeFilterFn>
bool model_volume_list_changed(const Domain::ModelObject &model_object_old, const Domain::ModelObject &model_object_new, TypeFilterFn type_filter)
{
    size_t i_old, i_new;
    for (i_old = 0, i_new = 0; i_old < model_object_old.volumes.size() && i_new < model_object_new.volumes.size();) {
        const Domain::ModelVolume &mv_old = *model_object_old.volumes[i_old];
        const Domain::ModelVolume &mv_new = *model_object_new.volumes[i_new];
        if (! type_filter(mv_old.type())) {
            ++ i_old;
            continue;
        }
        if (! type_filter(mv_new.type())) {
            ++ i_new;
            continue;
        }
        if (mv_old.type() != mv_new.type() || mv_old.id() != mv_new.id())
            return true;
        //FIXME test for the content of the mesh!
        if (! mv_old.get_matrix().isApprox(mv_new.get_matrix()))
            return true;
        ++ i_old;
        ++ i_new;
    }
    for (; i_old < model_object_old.volumes.size(); ++ i_old) {
        const Domain::ModelVolume &mv_old = *model_object_old.volumes[i_old];
        if (type_filter(mv_old.type()))
            // ModelVolume was deleted.
            return true;
    }
    for (; i_new < model_object_new.volumes.size(); ++ i_new) {
        const Domain::ModelVolume &mv_new = *model_object_new.volumes[i_new];
        if (type_filter(mv_new.type()))
            // ModelVolume was added.
            return true;
    }
    return false;
}

bool model_volume_list_changed(const Domain::ModelObject &model_object_old, const Domain::ModelObject &model_object_new, const Domain::ModelVolumeType type)
{
    return model_volume_list_changed(model_object_old, model_object_new, [type](const Domain::ModelVolumeType t) { return t == type; });
}

bool model_volume_list_changed(const Domain::ModelObject &model_object_old, const Domain::ModelObject &model_object_new, const std::initializer_list<Domain::ModelVolumeType> &types)
{
    return model_volume_list_changed(model_object_old, model_object_new, [&types](const Domain::ModelVolumeType t) {
        return std::find(types.begin(), types.end(), t) != types.end();
    });
}

template< typename TypeFilterFn, typename CompareFn>
bool model_property_changed(const Domain::ModelObject &model_object_old, const Domain::ModelObject &model_object_new, TypeFilterFn type_filter, CompareFn compare)
{
    assert(! model_volume_list_changed(model_object_old, model_object_new, type_filter));
    size_t i_old, i_new;
    for (i_old = 0, i_new = 0; i_old < model_object_old.volumes.size() && i_new < model_object_new.volumes.size();) {
        const Domain::ModelVolume &mv_old = *model_object_old.volumes[i_old];
        const Domain::ModelVolume &mv_new = *model_object_new.volumes[i_new];
        if (! type_filter(mv_old.type())) {
            ++ i_old;
            continue;
        }
        if (! type_filter(mv_new.type())) {
            ++ i_new;
            continue;
        }
        assert(mv_old.type() == mv_new.type() && mv_old.id() == mv_new.id());
        if (! compare(mv_old, mv_new))
            return true;
        ++ i_old;
        ++ i_new;
    }
    return false;
}

bool model_custom_supports_data_changed(const Domain::ModelObject& mo, const Domain::ModelObject& mo_new)
{
    return model_property_changed(mo, mo_new, 
        [](const Domain::ModelVolumeType t) { return t == Domain::ModelVolumeType::MODEL_PART; },
        [](const Domain::ModelVolume &mv_old, const Domain::ModelVolume &mv_new){ return mv_old.supported_facets.timestamp_matches(mv_new.supported_facets); });
}

bool model_custom_seam_data_changed(const Domain::ModelObject& mo, const Domain::ModelObject& mo_new)
{
    return model_property_changed(mo, mo_new, 
        [](const Domain::ModelVolumeType t) { return t == Domain::ModelVolumeType::MODEL_PART; },
        [](const Domain::ModelVolume &mv_old, const Domain::ModelVolume &mv_new){ return mv_old.seam_facets.timestamp_matches(mv_new.seam_facets); });
}

bool model_mmu_segmentation_data_changed(const Domain::ModelObject& mo, const Domain::ModelObject& mo_new)
{
    return model_property_changed(mo, mo_new, 
        [](const Domain::ModelVolumeType t) { return t == Domain::ModelVolumeType::MODEL_PART; },
        [](const Domain::ModelVolume &mv_old, const Domain::ModelVolume &mv_new){ return mv_old.mm_segmentation_facets.timestamp_matches(mv_new.mm_segmentation_facets); });
}

bool model_fuzzy_skin_data_changed(const Domain::ModelObject &mo, const Domain::ModelObject &mo_new)
{
    return model_property_changed(mo, mo_new,
        [](const Domain::ModelVolumeType t) { return t == Domain::ModelVolumeType::MODEL_PART; },
        [](const Domain::ModelVolume &mv_old, const Domain::ModelVolume &mv_new){ return mv_old.fuzzy_skin_facets.timestamp_matches(mv_new.fuzzy_skin_facets); });
}

bool model_has_parameter_modifiers_in_objects(const Domain::Model &model)
{
    for (const auto& model_object : model.objects)
        for (const auto& volume : model_object->volumes)
            if (volume->is_modifier())
                return true;
    return false;
}

bool model_has_advanced_features(const Domain::Model &model)
{
    auto config_is_advanced = [](const Domain::ConfigBox& config) {
        return !(config.overrides.empty() || (config.overrides.size() == 1 && config.overrides.get("extruder").has_value()));
    };

    for (const Domain::ModelObject* model_object : model.objects) {
        // Is there more than one instance or advanced config data?
        if (model_object->instances.size() > 1 || config_is_advanced(model_object->object_settings)) {
            return true;
        }

        // Is there any modifier or advanced config data?
        for (const Domain::ModelVolume* model_volume : model_object->volumes) {
            if (!model_volume->is_model_part() || config_is_advanced(model_volume->volume_settings)) {
                return true;
            }
        }
    }

    return false;
}

#ifndef NDEBUG
// Verify whether the IDs of Model / ModelObject / ModelVolume / ModelInstance are valid and unique.
void check_model_ids_validity(const Domain::Model &model)
{
    std::set<Domain::ObjectID> ids;
    auto check = [&ids](Domain::ObjectID id) {
        assert(id.valid());
        assert(ids.find(id) == ids.end());
        ids.insert(id);
    };
    for (const Domain::ModelObject *model_object : model.objects) {
        check(model_object->id());
        for (const Domain::ModelVolume *model_volume : model_object->volumes) {
            check(model_volume->id());
        }
        for (const Domain::ModelInstance *model_instance : model_object->instances)
            check(model_instance->id());
    }
}

void check_model_ids_equal(const Domain::Model &model1, const Domain::Model &model2)
{
    // Verify whether the IDs of model1 and model match.
    assert(model1.objects.size() == model2.objects.size());
    for (size_t idx_model = 0; idx_model < model2.objects.size(); ++ idx_model) {
        const Domain::ModelObject &model_object1 = *model1.objects[idx_model];
        const Domain::ModelObject &model_object2 = *  model2.objects[idx_model];
        assert(model_object1.id() == model_object2.id());
        assert(model_object1.volumes.size() == model_object2.volumes.size());
        assert(model_object1.instances.size() == model_object2.instances.size());
        for (size_t i = 0; i < model_object1.volumes.size(); ++ i) {
            assert(model_object1.volumes[i]->id() == model_object2.volumes[i]->id());
        }
        for (size_t i = 0; i < model_object1.instances.size(); ++ i)
            assert(model_object1.instances[i]->id() == model_object2.instances[i]->id());
    }
}

#endif /* NDEBUG */

}
