#ifndef slic3r_Model_hpp_
#define slic3r_Model_hpp_

#include "Slic3r/Domain/FacetsAnnotation.hpp"
#include "Slic3r/Domain/SLA/DrainHole.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/Geometry.hpp"
#include "Slic3r/Domain/CustomGCode.hpp"
#include "Slic3r/Domain/TextConfiguration.hpp"
#include "Slic3r/Domain/EmbossShape.hpp"
#include "Slic3r/Biz/Algorithms/TriangleSelector.hpp"
#include "Slic3r/Domain/TriangleSelector.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/FullConfigFDM.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/CutConnector.hpp"
#include "Slic3r/Domain/LayerHeightProfile.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <optional>
#include <random>

namespace Slic3r {

class BuildVolume;
class Print;


inline void model_volumes_sort_by_id(Domain::ModelVolumePtrs &model_volumes)
{
    std::sort(model_volumes.begin(), model_volumes.end(), [](const Domain::ModelVolume *l, const Domain::ModelVolume *r) { return l->id() < r->id(); });
}

inline const Domain::ModelVolume* model_volume_find_by_id(const Domain::ModelVolumePtrs &model_volumes, const Domain::ObjectID id)
{
    auto it = std::ranges::lower_bound(model_volumes, id, {}, [](const Domain::ModelVolume* mv) {
        return mv->id();
    });

    return it != model_volumes.end() && (*it)->id() == id ? *it : nullptr;
}

// Test whether the two models contain the same number of ModelObjects with the same set of IDs
// ordered in the same order. In that case it is not necessary to kill the background processing.
bool model_object_list_equal(const Domain::ModelObjectPtrs &old_objects, const Domain::ModelObjectPtrs &new_objects);

// Test whether the new model is just an extension of the old model (new objects were added
// to the end of the original list. In that case it is not necessary to kill the background processing.
bool model_object_list_extended(const Domain::Model &model_old, const Domain::Model &model_new);

// Test whether the new ModelObject contains a different set of volumes (or sorted in a different order)
// than the old ModelObject.
bool model_volume_list_changed(const Domain::ModelObject &model_object_old, const Domain::ModelObject &model_object_new, const Domain::ModelVolumeType type);
bool model_volume_list_changed(const Domain::ModelObject &model_object_old, const Domain::ModelObject &model_object_new, const std::initializer_list<Domain::ModelVolumeType> &types);

// Test whether the now ModelObject has newer custom supports data than the old one.
// The function assumes that volumes list is synchronized.
bool model_custom_supports_data_changed(const Domain::ModelObject& mo, const Domain::ModelObject& mo_new);

// Test whether the now ModelObject has newer custom seam data than the old one.
// The function assumes that volumes list is synchronized.
bool model_custom_seam_data_changed(const Domain::ModelObject& mo, const Domain::ModelObject& mo_new);

// Test whether the now ModelObject has newer MMU segmentation data than the old one.
// The function assumes that volumes list is synchronized.
extern bool model_mmu_segmentation_data_changed(const Domain::ModelObject& mo, const Domain::ModelObject& mo_new);

// Test whether the now ModelObject has newer fuzzy skin data than the old one.
// The function assumes that volumes list is synchronized.
extern bool model_fuzzy_skin_data_changed(const Domain::ModelObject &mo, const Domain::ModelObject &mo_new);

// If the model has object(s) which contains a modofoer, then it is currently not supported by the SLA mode.
// Either the model cannot be loaded, or a SLA printer has to be activated.
bool model_has_parameter_modifiers_in_objects(const Domain::Model& model);
// If the model has advanced features, then it cannot be processed in simple mode.
bool model_has_advanced_features(const Domain::Model &model);

#ifndef NDEBUG
// Verify whether the IDs of Model / ModelObject / ModelVolume / ModelInstance are valid and unique.
void check_model_ids_validity(const Domain::Model &model);
void check_model_ids_equal(const Domain::Model &model1, const Domain::Model &model2);
#endif /* NDEBUG */

} // namespace Slic3r

#endif /* slic3r_Model_hpp_ */
