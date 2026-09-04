#ifndef slic3r_EmbossJob_hpp_
#define slic3r_EmbossJob_hpp_

#include <memory>
#include <string>
#include <tl/expected.hpp>
#include "Slic3r/Biz/Emboss/Emboss.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/EmbossShape.hpp" // ExPolygonsWithIds
#include "Slic3r/Biz/Emboss/ShapeProvider.hpp"

namespace Slic3r::Biz::Emboss {

enum class JobIssue {
    canceled, // canceled thread
    no_shape, // Font doesn't have any shape for given text
    no_surface, // There is no valid surface for text projection
    default_volume // Create function created default shape (shape was not used)
};

using TriMeshResult = tl::expected<Domain::TriangleMesh, JobIssue>;

struct TriMeshBaseData
{
    // Create shape
    ShapeProviderPtr shape_provider;

    // Define projection move
    // True (raised) .. move outside from surface (MODEL_PART)
    // False (engraved).. move into object (NEGATIVE_VOLUME & MODIFIER)
    bool is_outside = true;

    // [optional] Define distance for surface
    // It is used only for flat surface (not cutted)
    // Position of Zero(not set value) differ for MODEL_PART and NEGATIVE_VOLUME
    std::optional<float> per_glyph_surface_distance;
};

struct BaseData
{
    TriMeshBaseData tri_mesh;

    // Add volume into project
    // @janBartipan garanted it will be alive in the finalize part of the job.
    // Job manager will not call finalize when Project interactor is not alive.
    Biz::ProjectInteractor& project_interactor;

    // Project of the object
    Domain::SelectionId project_id;

    // new volume name
    std::string volume_name;

    // function called on main thread when issue during proccess job appear
    using IssueFn = std::function<void(JobIssue)>;
    IssueFn issue_fn;
};

/**
@brief shorten params for start_crate_volume functions
*/
struct CreateVolumeParams
{
    // base input data for job
    BaseData base;

    // New created volume type
    Domain::ModelVolumeType volume_type;

    // Wanted additionl move in Z(emboss) direction of new created volume
    std::optional<float> distance = {};

    // Wanted additionl rotation around Z of new created volume
    std::optional<float> angle = {};
};

bool start_create_object_job(CreateVolumeParams& input, const Domain::Vec2d& coor);

/**
@brief Start job for add new volume to object with given transformation
@param instance Define where to add
@param volume_tr Wanted volume transformation
@param data Define what to emboss - shape
@param volume_type Type of volume: Part, negative, modifier
@return Nullptr when job is sucessfully add to worker otherwise return data to be processed different way
*/
bool start_create_volume_job(const Domain::ModelInstance& instance, const Domain::Transform3d& volume_tr, BaseData& data, Domain::ModelVolumeType volume_type);

/**
@brief Parameters for call start_update_volume function
*/
struct UpdateVolumeParams
{
    // base input data for job
    BaseData base;

    // unique identifier of volume to change
    Domain::ObjectID volume_id;

    Domain::ObjectID instance_id;

    std::optional<Domain::ModelVolumeType> volume_type = std::nullopt;
};

/**
@brief Start job for update embossed volume
@param data define update data
@param volume volume to update
@return True when start job otherwise false
*/
bool start_update_volume(UpdateVolumeParams&& data, const Domain::ModelVolume& volume);

ProjectTransform create_projection(const Domain::EmbossShape& es, bool is_outside);
const Domain::ModelInstance* get_selected_instance(const Domain::ElementRefs& elms, const Domain::Project& project);
const Domain::ModelInstance* get_selected_instance(const Biz::ProjectInteractor& project_interactor);

const Domain::ExPolygons& create_shape(ShapeProvider& input);
TriMeshResult create_mesh(TriMeshBaseData& input);
} // namespace Slic3r::Biz::Emboss

#endif // slic3r_EmbossJob_hpp_
