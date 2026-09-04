#include "Slic3r/Biz/Emboss/EmbossJob.hpp"
#include "Slic3r/Log.hpp"

#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"

// Provide job executor
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/CutSurface.hpp" // use surface cuts

// #include "libslic3r/Format/OBJ.hpp" // load_obj for default mesh
// #include <libslic3r/SLA/ReprojectPointsOnMesh.hpp>

#include "tl/expected.hpp"

using namespace Slic3r;

namespace {
using namespace Slic3r::Biz::Emboss;

bool check(const CreateVolumeParams& input);
bool check(const UpdateVolumeParams& input);

using namespace Slic3r::Biz::Emboss;
using Biz::JThread::StopToken;

// temporary interface for start job
class Job {
public:
    virtual ~Job()                   = default;
    virtual void process(StopToken&) = 0;
    virtual void finalize() {}
    virtual Domain::SelectionId project_id() const = 0;
};

/**
@brief Hold neccessary data to create ModelVolume in job
Volume is created on the surface of existing volume in object.
NOTE: EmbossDataBase::font_file doesn't have to be valid !!!
*/
struct DataCreateVolume
{
    // Hold data about shape
    BaseData base;

    // define embossed volume type
    Domain::ModelVolumeType volume_type;

    // parent ModelInstance index where to create volume
    // also instance contain object
    Domain::ObjectID instance_id;

    // new created volume transformation
    Domain::Transform3d transform;
};

// Offset of clossed side to model
constexpr double SAFE_SURFACE_OFFSET = 0.015; // [in mm]

/**
@brief Create new TextVolume on the surface of ModelObject
Should not be stopped
NOTE: EmbossDataBase::font_file doesn't have to be valid !!!
*/
class CreateVolumeJob : public Job
{
    DataCreateVolume m_input;
    TriMeshResult m_result;

public:
    explicit CreateVolumeJob(DataCreateVolume&& input);
    void process(StopToken& stop) override;
    void finalize() override;
    Domain::SelectionId project_id() const override { return m_input.base.project_id; }
};

/**
@brief Hold neccessary data to create ModelObject in job
Object is placed on bed under screen coor
OR to center of scene when it is out of bed shape
*/
struct DataCreateObject
{
    // Hold data about shape
    BaseData base;

    // Define coordinate on the bed
    Domain::Vec2d bed_coor;

    // additionl rotation around Z axe, given by style settings
    std::optional<float> angle = {};
};

/**
@brief Create new TextObject on the platter
Should not be stopped
*/
class CreateObjectJob : public Job
{
    DataCreateObject m_input;
    TriMeshResult m_result;
    Domain::Transformation m_transformation;

public:
    explicit CreateObjectJob(DataCreateObject&& input);
    void process(StopToken& stop) override;
    void finalize() override;
    Domain::SelectionId project_id() const override { return m_input.base.project_id; }
};

/**
@brief Triangle sources for cut surface from volume
used only with SurfaceVolumeData
*/
struct ModelSource
{
    // source volumes
    std::shared_ptr<const Domain::TriangleMesh> mesh;
    // Transformation of volume inside of object
    Domain::Transform3d tr;
};

using ModelSources = std::vector<ModelSource>;

/**
@brief Copied triangles from object to be able create mesh for cut surface from
@param volume Define embossed volume
@return Source data for cut surface from
*/
ModelSources create_volume_sources(const Domain::ModelVolume& volume);

struct SurfaceVolumeData
{
    // Transformation of volume inside of object
    Domain::Transform3d transform;
    ModelSources sources;
};

/**
@brief Hold neccessary data to create(cut) volume from surface object in job
*/
struct CreateSurfaceVolumeData : public SurfaceVolumeData
{
    // Hold data about shape
    BaseData base;

    // define embossed volume type
    Domain::ModelVolumeType volume_type;

    // parent ModelInstance index where to create volume
    Domain::ObjectID instance_id;
};

/**
@brief Cut surface from object and create cutted volume
Should not be stopped
*/
class CreateSurfaceVolumeJob : public Job
{
    CreateSurfaceVolumeData m_input;
    TriMeshResult m_result;

public:
    explicit CreateSurfaceVolumeJob(CreateSurfaceVolumeData&& input);
    void process(StopToken& stop) override;
    void finalize() override;
    Domain::SelectionId project_id() const override { return m_input.base.project_id; }
};

/**
@brief Hold neccessary data to update embossed text object in job
*/
struct UpdateSurfaceVolumeData : public UpdateVolumeParams, public SurfaceVolumeData{};

/**
@brief Update text volume to use surface from object
*/
class UpdateSurfaceVolumeJob : public Job
{
    UpdateSurfaceVolumeData m_input;
    TriMeshResult m_result;
public:
    // move params to private variable
    explicit UpdateSurfaceVolumeJob(UpdateSurfaceVolumeData&& input);
    void process(StopToken& stop) override;
    void finalize() override;
    Domain::SelectionId project_id() const override { return m_input.base.project_id; }
};

/**
@brief Update text shape in existing text volume
Predict that there is only one runnig(not canceled) instance of it
*/
class UpdateJob : public Job
{
    UpdateVolumeParams m_input;
    TriMeshResult m_result;

public:
    // move params to private variable
    explicit UpdateJob(UpdateVolumeParams&& input);

    /**
    @brief Create new embossed volume by m_input data and store to m_result
    @param ctl Control containing cancel flag
    */
    void process(StopToken& stop) override;

    /**
    @brief Update volume - change object_id
    @param canceled Was process canceled.
    NOTE: Be carefull it doesn't care about
    time between finished process and started finalize part.
    @param  unused
    */
    void finalize() override;

    /**
    @brief Update text volume
    @param volume Volume to be updated
    @param mesh New Triangle mesh for volume
    @param base Data to write into volume
    @param instance_id Define instance for update selection
    */
    static void update_volume(Domain::ModelVolume& volume, Domain::TriangleMesh&& mesh, const Biz::Emboss::BaseData& base, const Domain::ObjectID& instance_id);

    Domain::SelectionId project_id() const override { return m_input.base.project_id; }
};

/**
@brief Copied triangles from object to be able create mesh for cut surface from
@param volumes Source object volumes for cut surface from
@param text_volume_id Source volume id
@return Source data for cut surface from
*/
ModelSources create_sources(const Domain::ModelVolumePtrs& volumes, std::optional<size_t> text_volume_id = {});

bool queue_job(std::unique_ptr<Job> job);

// <summary>
/// Try to create mesh from text
/// </summary>
/// NOTE: Cache glyphs is changed</param>
/// <param name="was_canceled">To check if process was canceled</param>
/// <returns>Triangle mesh model</returns>
template <typename Fnc>
TriMeshResult try_create_mesh(TriMeshBaseData& input, const Fnc& was_canceled);

} // namespace

namespace Slic3r::Biz::Emboss {
const Domain::ModelInstance* get_selected_instance(const Domain::ElementRefs& elms, const Domain::Project& project) {
    if (elms.empty())
        return nullptr;

    std::optional<Domain::ElementRef> selected;
    for (const Domain::ElementRef& el : elms) {
        if (!selected.has_value()) {
            selected = el;
            continue;
        }
        if (selected->object_id != el.object_id)
            return nullptr; // multiple object selection
    }
    if (!selected.has_value())
        return nullptr; // no object selected    
    return project.find_instance_by_id(selected->object_id, selected->instance_id);
}

const Domain::ModelInstance* get_selected_instance(const Biz::ProjectInteractor& project_interactor) {
    const Domain::ElementRefs& elms = project_interactor.scene_interactor().object_selection().elements;
    const Domain::Project& project = project_interactor.selected_project();
    return get_selected_instance(elms, project);
}

bool start_create_volume_job(const Domain::ModelInstance& instance, const Domain::Transform3d& volume_tr, BaseData& data, Domain::ModelVolumeType volume_type)
{
    const Domain::ModelObject& object = *instance.get_object();
    data.tri_mesh.shape_provider->create_text_lines(volume_tr, object); // only when needed

    bool use_surface = data.tri_mesh.shape_provider->get_projection().use_surface;
    std::unique_ptr<Job> job;
    if (use_surface) {
        // Model to cut surface from.
        ModelSources sources = create_sources(object.volumes);
        if (sources.empty()) {
            use_surface = false; // can't use surface, try CreateVolumeJob
        }
        else {
            SurfaceVolumeData sfvd{ volume_tr, std::move(sources) };
            CreateSurfaceVolumeData
                surface_data{ std::move(sfvd), std::move(data), volume_type, instance.id() };
            job = std::make_unique<CreateSurfaceVolumeJob>(std::move(surface_data));
        }
    }
    if (!use_surface) {
        // create volume
        DataCreateVolume create_volume_data{
            .base = std::move(data),
            .volume_type = volume_type,
            .instance_id = instance.id(),
            .transform = volume_tr };
        job = std::make_unique<CreateVolumeJob>(std::move(create_volume_data));
    }
    return queue_job(std::move(job));
}

bool start_create_object_job(CreateVolumeParams& input, const Domain::Vec2d& coor)
{
    ASSERT(check(input));
    // create transformation on the coordinate
    DataCreateObject data{ .base = std::move(input.base), .bed_coor = coor, .angle = input.angle };
    auto job = std::make_unique<CreateObjectJob>(std::move(data));
    return queue_job(std::move(job));
}

bool start_update_volume(UpdateVolumeParams&& data, const Domain::ModelVolume& volume)
{
    if (!data.base.tri_mesh.shape_provider->get_projection().use_surface) {
        // Without cutting model surface
        return queue_job(std::make_unique<UpdateJob>(std::move(data)));
    }

    // use surface

    // Model to cut surface from.
    ModelSources sources = create_volume_sources(volume);
    ASSERT(!sources.empty()); // volume can't be the only one part
    UpdateSurfaceVolumeData surface_data{std::move(data), {volume.get_matrix(), std::move(sources)}};
    return queue_job(std::make_unique<UpdateSurfaceVolumeJob>(std::move(surface_data)));
}

ProjectTransform create_projection(const Domain::EmbossShape& es, bool is_outside) {
    // NOTE: SHAPE_SCALE is applied in ProjectZ
    double scale = es.scale;
    double depth = es.projection.depth / scale;
    auto projectZ = std::make_unique<ProjectZ>(depth);
    double offset = is_outside ? -SAFE_SURFACE_OFFSET : (SAFE_SURFACE_OFFSET - es.projection.depth);
    Domain::Transform3d tr = Eigen::Translation<double, 3>(0., 0., offset) * Eigen::Scaling(scale);
    return ProjectTransform(std::move(projectZ), tr);
}

const Domain::ExPolygons& create_shape(ShapeProvider& input)
{
    // IMPROVE: use real size of volume for union delta value
    // ... need world matrix for volume
    // ... printer resolution will be fine too
    input.create_shape_with_union();
    return input.get_shape().final_shape.expolygons;
}

TriMeshResult create_mesh(TriMeshBaseData& input)
{
    return try_create_mesh(input, []{ return false; });
}


} // namespace Slic3r::Biz::Emboss

// Private implementation for create volume and objects jobs
namespace {
/**
@brief Assert check of inputs data
*/
bool check(const DataCreateVolume& input);
bool check(const DataCreateObject& input);
bool check(const SurfaceVolumeData& input);
bool check(const CreateSurfaceVolumeData& input);
bool check(const UpdateSurfaceVolumeData& input);

template <typename Fnc>
static const Domain::ExPolygons& create_shape(ShapeProvider& input, Fnc was_canceled);

// create sure that emboss object is bigger than source object [in mm]
constexpr float safe_extension = 1.0f;

/**
@brief Create default mesh for embossed text
@return Not empty model(index trinagle set - its)
*/
Domain::TriangleMesh create_default_mesh();

/**
@brief Must be called on main thread
@param mesh New mesh data
@param data Text configuration, ...,
Note: NOT CONST -> contain Project interactor to change on the poroject
containing updated volume
*/
void final_update_volume(Domain::TriangleMesh&& mesh, UpdateVolumeParams& data);

/**
@brief Add new volume to object
@param mesh triangles of new volume
@param instance_id Define object where to add volume
@param type Type of new volume
@param trmat Transformation of volume inside of object
@param data Project interactor + project_id + gizmo
*/
void create_volume(Domain::TriangleMesh&& mesh, const Domain::ObjectID& instance_id, const Domain::ModelVolumeType type, const Domain::Transform3d& trmat, const BaseData& data);

/**
@brief Create projection for cut surface from mesh
@param tr Volume transformation in object
@param shape_scale Convert shape to milimeters
@param z_range Bounding box 3d of model volume for projection ranges
@return Orthogonal cut_projection
*/
OrthoProject create_projection_for_cut(Domain::Transform3d tr, double shape_scale, const std::pair<float, float>& z_range);

/**
@brief Create tranformation for emboss Cutted surface
@param is_outside True .. raise, False .. engrave
@param emboss Depth of embossing
@param tr Text voliume transformation inside object
@param cut Cutted surface from model
@return Projection
*/
OrthoProject3d
create_emboss_projection(bool is_outside, float emboss, Domain::Transform3d tr, Biz::CGAL::Algorithms::SurfaceCut& cut);

/**
@brief Cut surface into triangle mesh
@param base (can't be const - cache of font)
@param input2 SurfaceVolume data
@param was_canceled Check to interupt execution
@return Extruded object from cuted surace
*/
template <typename Fnc>
TriMeshResult cut_surface(/*const*/ BaseData& input1, const SurfaceVolumeData& input2, const Fnc& was_canceled
);

auto was_canceled(StopToken& stop)
{
    return [&stop]() { return stop.stop_requested(); };
}

std::string to_string(JobIssue issue) {
    switch (issue) {
    case JobIssue::no_shape: return "no shape";
    case JobIssue::no_surface: return "no surface";
    case JobIssue::default_volume: return "default volume";
    case JobIssue::canceled: return "canceled";
    default: return "unknown issue";
    };
}

/////////////////
/// Create Volume
CreateVolumeJob::CreateVolumeJob(DataCreateVolume&& input) : m_input(std::move(input))
{
    ASSERT(check(m_input));
}

void CreateVolumeJob::process(StopToken& stop)
{
    m_result = ::try_create_mesh(m_input.base.tri_mesh, was_canceled(stop));
}

void CreateVolumeJob::finalize()
{
    if (!m_result.has_value()) {
        m_input.base.issue_fn(JobIssue::default_volume);
        m_result = create_default_mesh();
    }

    create_volume(
        std::move(m_result.value()),
        m_input.instance_id,
        m_input.volume_type,
        m_input.transform,
        m_input.base
    );
}

/////////////////
/// Create Object
CreateObjectJob::CreateObjectJob(DataCreateObject&& input) : m_input(std::move(input))
{
    ASSERT(check(m_input));
}

void CreateObjectJob::process(StopToken& stop)
{
    auto was_canceled = ::was_canceled(stop);
    m_result = ::try_create_mesh(m_input.base.tri_mesh, was_canceled);

    // check point is on build plate:
    double z = m_input.base.tri_mesh.shape_provider->get_projection().depth / 2;
    Domain::Vec3d offset(m_input.bed_coor.x(), m_input.bed_coor.y(), z);

    Domain::BoundingBox3d bb3 = m_result.has_value()?
        m_result->bounding_box() : create_default_mesh().bounding_box();
    offset -= (bb3.max + bb3.min) * .5; // result mesh center

    Domain::Transform3d::TranslationType tt(offset.x(), offset.y(), offset.z());
    Domain::Transform3d transformation(tt);

    // rotate around Z by style settings
    if (m_input.angle.has_value()) {
        std::optional<float> distance; // new object ignore surface distance from style settings
        Biz::Emboss::apply_transformation(m_input.angle, distance, transformation);
    }
    m_transformation = Domain::Transformation(transformation);
}

void CreateObjectJob::finalize()
{
    if (!m_result.has_value()) {
        m_input.base.issue_fn(JobIssue::default_volume);
        m_result = create_default_mesh();
    }

    const BaseData& base = m_input.base;
    if (base.project_interactor.selected_project_id() != base.project_id)
        m_input.base.project_interactor.select_project(base.project_id);

    Biz::Scene::SceneInteractor& scene_interactor = base.project_interactor.scene_interactor();

    Biz::Scene::SceneInteractor::UpdateObjectFn update_object = [&](Domain::ModelObject& object)
    {
        object.name                 = m_input.base.volume_name;
        Domain::ModelVolume& volume = *object.volumes.front();
        volume.name                 = m_input.base.volume_name;
        m_input.base.tri_mesh.shape_provider->write(volume);

        Domain::ModelInstance& instance = *object.instances.front();
        instance.set_transformation(m_transformation);
    };
    scene_interactor.new_object_from_mesh(std::move(m_result.value()), base.project_id, update_object);
}

/////////////////
/// Update Volume

UpdateJob::UpdateJob(UpdateVolumeParams&& input) : m_input(std::move(input))
{
    ASSERT(check(m_input));
}

void UpdateJob::process(StopToken& stop)
{
    m_result = ::try_create_mesh(m_input.base.tri_mesh, ::was_canceled(stop));
}

void UpdateJob::finalize()
{
    if (!m_result.has_value()) {
        return m_input.base.issue_fn(m_result.error());
    }
    ::final_update_volume(std::move(m_result.value()), m_input);
}

void UpdateJob::update_volume(Domain::ModelVolume& volume, Domain::TriangleMesh&& mesh, const Biz::Emboss::BaseData& base, const Domain::ObjectID& instance_id)
{
    // check inputs
    bool is_valid_input = !mesh.empty();
    assert(is_valid_input);
    if (!is_valid_input)
        return;

    // write data from base into volume
    base.tri_mesh.shape_provider->write(volume);
    if (volume.name != base.volume_name && !base.volume_name.empty()) {
        volume.name = base.volume_name;
    }

    const Domain::ModelObject* object = volume.get_object();
    assert(object != nullptr);
    if (object == nullptr)
        return;

    Domain::ElementRef ref(
        object->id().id, 
        instance_id.id, 
        volume.id().id);

    Biz::Scene::SceneInteractor::RefMeshes meshes;
    meshes.emplace_back(ref, std::move(mesh));

    Biz::Scene::SceneInteractor& scene_interactor = base.project_interactor.scene_interactor();
    scene_interactor.change_volume_meshes(std::move(meshes));
}

/////////////////
/// Create Surface volume

CreateSurfaceVolumeJob::CreateSurfaceVolumeJob(CreateSurfaceVolumeData&& input) :
    m_input(std::move(input))
{
    ASSERT(check(m_input));
}

void CreateSurfaceVolumeJob::process(StopToken& stop)
{
    m_result = ::cut_surface(m_input.base, m_input, was_canceled(stop));
}

void CreateSurfaceVolumeJob::finalize()
{
    if (!m_result.has_value()) {
        m_input.base.issue_fn(JobIssue::default_volume);
        m_result = create_default_mesh();
    }

    create_volume(
        std::move(m_result.value()),
        m_input.instance_id,
        m_input.volume_type,
        m_input.transform,
        m_input.base
    );
}

/////////////////
/// Cut Surface
UpdateSurfaceVolumeJob::UpdateSurfaceVolumeJob(UpdateSurfaceVolumeData&& input) :
    m_input(std::move(input))
{
    ASSERT(check(m_input));
}

void UpdateSurfaceVolumeJob::process(StopToken& stop)
{
    m_result = cut_surface(m_input.base, m_input, was_canceled(stop));
}

void UpdateSurfaceVolumeJob::finalize()
{
    if (!m_result.has_value()) {
        return m_input.base.issue_fn(m_result.error());
    }

    // when start using surface it is wanted to move text origin on surface of model
    // also when repeteadly move above surface result position should match
    ::final_update_volume(std::move(m_result.value()), m_input);
}

////////////////////////////
/// private namespace implementation

ModelSources create_volume_sources(const Domain::ModelVolume& text_volume)
{
    const Domain::ModelVolumePtrs& volumes = text_volume.get_object()->volumes;
    // no other volume in object
    if (volumes.size() <= 1)
        return {};
    return ::create_sources(volumes, text_volume.id().id);
}

bool check(const Domain::ObjectID& object_id)
{
    assert(object_id.valid());
    return object_id.valid();
}

bool check(const BaseData& base)
{
    assert(base.tri_mesh.shape_provider != nullptr);
    bool res = base.tri_mesh.shape_provider != nullptr;
    const Domain::EmbossProjection& projection = base.tri_mesh.shape_provider->get_projection();
    res &= projection.depth > 0;
    const Domain::EmbossShape& shape = base.tri_mesh.shape_provider->get_shape();
    res &= shape.scale > 0;
    assert(base.project_id != Domain::INVALID_ID);
    res &= (base.project_id != Domain::INVALID_ID);
    return res;
}

bool check(Domain::ModelVolumeType volume_type)
{
    assert(volume_type != Domain::ModelVolumeType::INVALID);
    assert(volume_type == Domain::ModelVolumeType::MODEL_PART || volume_type == Domain::ModelVolumeType::NEGATIVE_VOLUME || volume_type == Domain::ModelVolumeType::PARAMETER_MODIFIER);
    if (volume_type == Domain::ModelVolumeType::MODEL_PART || volume_type == Domain::ModelVolumeType::NEGATIVE_VOLUME || volume_type == Domain::ModelVolumeType::PARAMETER_MODIFIER)
        return true;
    SPDLOG_ERROR("Can't create embossed volume with this type: {}", (int)volume_type);
    return false;
}

bool check(const CreateVolumeParams& input)
{
    bool res = check(input.volume_type);
    res &= check(input.base);
    return res;
}

bool check(const DataCreateVolume& input)
{
    bool check_fontfile = false;
    assert(input.base.tri_mesh.shape_provider != nullptr);
    bool res = input.base.tri_mesh.shape_provider != nullptr;
    res &= check(input.volume_type);
    return res;
}

bool check(const DataCreateObject& input)
{
    bool check_fontfile = false;
    assert(input.base.tri_mesh.shape_provider != nullptr);
    bool res = input.base.tri_mesh.shape_provider != nullptr;
    return res;
}

bool check(const UpdateVolumeParams& input)
{
    bool res = check(input.base);
    assert(!input.volume_id.invalid());
    res &= !input.volume_id.invalid();
    assert(!input.instance_id.invalid());
    res &= !input.instance_id.invalid();
    return res;
}

bool check(const SurfaceVolumeData& input)
{
    assert(!input.sources.empty());
    bool res = !input.sources.empty();
    return res;
}

bool check(const CreateSurfaceVolumeData& input)
{
    bool res = check((const SurfaceVolumeData&) input);
    res &= check(input.base);
    // res &= check(input.volume_type);
    res &= check(input.instance_id);
    assert(!input.sources.empty());
    res &= !input.sources.empty();
    assert(input.base.tri_mesh.shape_provider->get_projection().use_surface);
    res &= input.base.tri_mesh.shape_provider->get_projection().use_surface;
    return res;
}

bool check(const UpdateSurfaceVolumeData& input)
{
    const UpdateVolumeParams& data_update = input;
    const SurfaceVolumeData& surface      = input;
    return check(data_update) && check(surface);
}


template <typename Fnc>
const Domain::ExPolygons& create_shape(ShapeProvider& input, Fnc was_canceled)
{
    return create_shape(input);
}

// #define STORE_SAMPLING
#ifdef STORE_SAMPLING
#include "libslic3r/SVG.hpp"
#endif // STORE_SAMPLING

std::vector<Domain::BoundingBoxes2crd> create_line_bounds(const Domain::ExPolygonsWithIds& shapes, size_t count_lines = 0)
{
    if (count_lines == 0)
        count_lines = get_count_lines(shapes);
    assert(count_lines == get_count_lines(shapes));

    std::vector<Domain::BoundingBoxes2crd> result(count_lines);
    size_t text_line_index = 0;
    // s_i .. shape index
    for (const Domain::ExPolygonsWithId& shape_id : shapes) {
        const Domain::ExPolygons& shape = shape_id.expoly;
        Domain::BoundingBox2crd bb;
        if (!shape.empty()) {
            bb = Biz::Algorithms::ExPolygon::get_extents(shape);
        }
        Domain::BoundingBoxes2crd& line_bbs = result[text_line_index];
        line_bbs.push_back(bb);
        if (shape_id.id == ENTER_UNICODE) {
            // skip enters on beginig and tail
            ++text_line_index;
        }
    }
    return result;
}

template <typename Fnc>
TriMeshResult create_mesh_per_glyph(TriMeshBaseData& input, Fnc was_canceled)
{
    // method use square of coord stored into int64_t
    // Domain::Point::coord_type
    static_assert(std::is_same<Domain::coord_t, int32_t>());

    if(!input.shape_provider->create_shape())
        return tl::unexpected{ JobIssue::no_shape };

    const Domain::EmbossShape& shape = input.shape_provider->get_shape();
    if (shape.shapes_with_ids.empty())
        return tl::unexpected{ JobIssue::no_shape };

    // Precalculate bounding boxes of glyphs
    // Separate lines of text to vector of Bounds
    const TextLines& text_lines = input.shape_provider->get_text_lines();
    size_t count_lines = text_lines.size();
    assert(get_count_lines(shape.shapes_with_ids) == count_lines);
    std::vector<Domain::BoundingBoxes2crd> bbs = create_line_bounds(shape.shapes_with_ids, count_lines);

    double depth  = shape.projection.depth / shape.scale;
    auto scale_tr = Eigen::Scaling(shape.scale);

    size_t s_i_offset = 0; // shape index offset(for next lines)
    indexed_triangle_set result;
    for (size_t text_line_index = 0; text_line_index < count_lines; ++text_line_index)
    {
        const Domain::BoundingBoxes2crd& line_bbs = bbs[text_line_index];
        const TextLine& line              = text_lines[text_line_index];
        Biz::Emboss::PolygonPoints samples = sample_slice(line, line_bbs, shape.scale);
        std::vector<double> angles         = calculate_angles(line_bbs, samples, line.polygon);

        for (size_t i = 0; i < line_bbs.size(); ++i) {
            const Domain::BoundingBox2crd& letter_bb = line_bbs[i];
            if (!letter_bb.defined)
                continue;

            Domain::Vec2d to_zero_vec = Biz::Algorithms::BoundingBox::center(letter_bb).cast<double>() * shape.scale; // [in mm]
            float surface_offset = input.is_outside ? -SAFE_SURFACE_OFFSET : (-shape.projection.depth + SAFE_SURFACE_OFFSET);
            if (const std::optional<float>& distance = input.per_glyph_surface_distance;
                distance.has_value())
                surface_offset += *distance;
            Eigen::Translation<double, 3> to_zero(-to_zero_vec.x(), 0., static_cast<double>(surface_offset));

            const Biz::Emboss::PolygonPoint& sample = samples[i];
            Domain::Vec2d offset_vec = Biz::Algorithms::Scaling::unscaled<double>(sample.point); // [in mm]
            Eigen::Translation<double, 3> offset_tr(offset_vec.x(), 0., -offset_vec.y());

            const double& angle = angles[i];
            Eigen::AngleAxisd rotate(angle + M_PI_2, Domain::Vec3d::UnitY());
            Domain::Transform3d tr = offset_tr * rotate * to_zero * scale_tr;

            const Domain::ExPolygons& letter_shape = shape.shapes_with_ids[s_i_offset + i].expoly;
            assert(Biz::Algorithms::ExPolygon::get_extents(letter_shape) == letter_bb);
            auto projectZ = std::make_unique<ProjectZ>(depth);
            ProjectTransform project(std::move(projectZ), tr);
            indexed_triangle_set glyph_its = polygons2model(letter_shape, project);
            Domain::its_merge(result, std::move(glyph_its));

            if (((s_i_offset + i) % 15) && was_canceled())
                return tl::unexpected{ JobIssue::canceled };
        }
        s_i_offset += line_bbs.size();

#ifdef STORE_SAMPLING
        { // Debug store polygon
            // std::string stl_filepath = "C:/data/temp/line" + std::to_string(text_line_index) + "_model.stl";
            // bool suc = its_write_stl_ascii(stl_filepath.c_str(), "label", result);

            Domain::BoundingBox2crd bbox = get_extents(line.polygon);
            std::string file_path = "C:/data/temp/line" + std::to_string(text_line_index) + "_letter_position.svg";
            SVG svg(file_path, bbox);
            svg.draw(line.polygon);
            int32_t radius = bbox.size().x() / 300;
            for (size_t i = 0; i < samples.size(); i++) {
                const PolygonPoint& pp = samples[i];
                const Point& p         = pp.point;
                svg.draw(p, "green", radius);
                std::string label = std::string(" ") + tc.text[i];
                svg.draw_text(p, label.c_str(), "black");

                double a      = angles[i];
                double length = 3.0 * radius;
                Point n(length * std::cos(a), length * std::sin(a));
                svg.draw(Slic3r::Line(p - n, p + n), "Lime");
            }
        }
#endif // STORE_SAMPLING
    }

    if (result.empty()) // Whole text do not contain any shape.
        return tl::unexpected{ JobIssue::no_shape };         

    return Biz::Algorithms::TriangleMesh::construct(std::move(result));
}

template <typename Fnc>
TriMeshResult try_create_mesh(TriMeshBaseData& input, const Fnc& was_canceled)
{
    if (!input.shape_provider->get_text_lines().empty())
        return create_mesh_per_glyph(input, was_canceled);

    const Domain::ExPolygons& shapes = create_shape(*input.shape_provider, was_canceled);
    if (shapes.empty())
        return tl::unexpected{ JobIssue::no_shape };

    if (was_canceled())
        return tl::unexpected{ JobIssue::canceled };

    ProjectTransform project = create_projection(input.shape_provider->get_shape(), input.is_outside);
    return Biz::Algorithms::TriangleMesh::construct(polygons2model(shapes, project));
}

Domain::TriangleMesh create_default_mesh()
{
    return Biz::Algorithms::TriangleMesh::construct(Biz::Algorithms::TriangleMesh::its_make_cube(36., 4., 2.5));
    //// When cant load any font use default object loaded from file
    // std::string  path = Slic3r::resources_dir() + "/data/embossed_text.obj";
    // Domain::TriangleMesh triangle_mesh;
    // if (!load_obj(path.c_str(), &triangle_mesh)) {
    // // when can't load mesh use cube
    // return Biz::Algorithms::TriangleMesh::construct(
    // Biz::Algorithms::TriangleMesh::its_make_cube(36., 4., 2.5));
    //}
    // return triangle_mesh;
}

Domain::ModelVolume* get_volume(const Domain::Project& project, const Domain::ObjectID& volume_id)
{
    const Domain::Model& model = project.model();
    for (const Domain::ModelObject* object_ptr : model.objects) {
        for (Domain::ModelVolume* volume_ptr : object_ptr->volumes) {
            if (volume_ptr->id() == volume_id)
                return volume_ptr;
        }
    }
    return nullptr;
}

void final_update_volume(Domain::TriangleMesh&& mesh, UpdateVolumeParams& data)
{
    // all used symbol in text conain only empty glyphs
    if (mesh.its.empty())
        return; // Empty mesh can't be created.

    // select project
    Biz::ProjectInteractor& project_interactor = data.base.project_interactor;
    if (project_interactor.selected_project_id() != data.base.project_id)
        project_interactor.select_project(data.base.project_id);
    Domain::Project& project        = project_interactor.selected_project();
    Domain::ModelVolume* volume_ptr = get_volume(project, data.volume_id);
    // could appear when user delete edited volume
    if (volume_ptr == nullptr)
        return;
    Domain::ModelVolume& volume = *volume_ptr;

    if (data.volume_type.has_value()) {
        volume.set_type(*data.volume_type);
    }

    UpdateJob::update_volume(volume, std::move(mesh), data.base, data.instance_id);
}

const Domain::ModelInstance* find_instance(const Domain::Project& project, const Domain::ObjectID& instance_id) {
    for(const Domain::ModelObject* object: project.model().objects)
        for (const Domain::ModelInstance* instance : object->instances) 
            if(instance->id() == instance_id)
                return instance;
    return nullptr;
}

void create_volume(Domain::TriangleMesh&& mesh, const Domain::ObjectID& instance_id, const Domain::ModelVolumeType type, const Domain::Transform3d& trmat, const BaseData& data)
{
    // create volume
    if (mesh.its.empty())
        return;

    auto& scene_interactor = data.project_interactor.scene_interactor();
    scene_interactor.add_volume(
        data.project_id,
        instance_id.id,
        [&](Domain::ModelObject& obj)
        {
            // Biz::Algorithms::ModelObject::add_volume - without centering
            Domain::ModelVolume* vol =
                Biz::Algorithms::ModelVolume::construct_ptr(&obj, std::move(mesh), type);
            obj.volumes.push_back(vol);
            obj.invalidate_bounding_box();

            vol->name                           = data.volume_name; // copy
            vol->source.is_from_builtin_objects = true; // disallow model reload from disk
            vol->set_transformation(trmat);

            data.tri_mesh.shape_provider->write(*vol);
            return vol;
        }
    );

}

OrthoProject create_projection_for_cut(Domain::Transform3d tr, double shape_scale, const std::pair<float, float>& z_range)
{
    double min_z = z_range.first - safe_extension;
    double max_z = z_range.second + safe_extension;
    assert(min_z < max_z);
    // range between min and max value
    double projection_size                           = max_z - min_z;
    Domain::SquareMatrix3d transformation_for_vector = tr.linear();
    // Projection must be negative value.
    // System of text coordinate
    // X .. from left to right
    // Y .. from bottom to top
    // Z .. from text to eye
    Domain::Vec3d untransformed_direction(0., 0., projection_size);
    Domain::Vec3d project_direction = transformation_for_vector * untransformed_direction;

    // Projection is in direction from far plane
    tr.translate(Domain::Vec3d(0., 0., min_z));
    tr.scale(shape_scale);
    return OrthoProject(tr, project_direction);
}

OrthoProject3d create_emboss_projection(bool is_outside, float emboss, Domain::Transform3d tr, Biz::CGAL::Algorithms::SurfaceCut& cut)
{
    float front_move = (is_outside) ? emboss : SAFE_SURFACE_OFFSET, 
           back_move = -((is_outside) ? SAFE_SURFACE_OFFSET : emboss);
    its_transform(cut, tr.pretranslate(Domain::Vec3d(0., 0., front_move)));
    Domain::Vec3d from_front_to_back(0., 0., back_move - front_move);
    return OrthoProject3d(from_front_to_back);
}

indexed_triangle_set
cut_surface_to_its(const Domain::ExPolygons& shapes, const Domain::Transform3d& tr, const ModelSources& sources, const BaseData& input, std::function<bool()> was_canceled)
{
    assert(!sources.empty());
    BoundingBox bb     = Biz::Algorithms::ExPolygon::get_extents(shapes);
    double shape_scale = input.tri_mesh.shape_provider->get_shape().scale;

    const ModelSource* biggest = &sources.front();

    size_t biggest_count = 0;
    // convert index from (s)ources to (i)ndexed (t)riangle (s)ets
    std::vector<size_t> s_to_itss(sources.size(), std::numeric_limits<size_t>::max());
    std::vector<indexed_triangle_set> itss;
    itss.reserve(sources.size());
    for (const ModelSource& s : sources) {
        Domain::Transform3d mesh_tr_inv       = s.tr.inverse();
        Domain::Transform3d cut_projection_tr = mesh_tr_inv * tr;
        std::pair<float, float> z_range{0., 1.};
        OrthoProject cut_projection = create_projection_for_cut(cut_projection_tr, shape_scale, z_range);
        // copy only part of source model
        indexed_triangle_set its = Biz::CGAL::Algorithms::its_cut_AoI(s.mesh->its, bb, cut_projection);
        if (its.indices.empty())
            continue;
        if (biggest_count < its.vertices.size()) {
            biggest_count = its.vertices.size();
            biggest       = &s;
        }
        size_t source_index     = &s - &sources.front();
        size_t its_index        = itss.size();
        s_to_itss[source_index] = its_index;
        itss.emplace_back(std::move(its));
    }
    if (itss.empty())
        return {};

    Domain::Transform3d tr_inv            = biggest->tr.inverse();
    Domain::Transform3d cut_projection_tr = tr_inv * tr;

    size_t itss_index     = s_to_itss[biggest - &sources.front()];
    BoundingBoxf3 mesh_bb = Domain::bounding_box(itss[itss_index]);
    for (const ModelSource& s : sources) {
        itss_index = s_to_itss[&s - &sources.front()];
        if (itss_index == std::numeric_limits<size_t>::max())
            continue;
        if (&s == biggest)
            continue;

        Domain::Transform3d tr    = tr_inv * s.tr;
        bool fix_reflected        = true;
        indexed_triangle_set& its = itss[itss_index];
        its_transform(its, tr, fix_reflected);
        BoundingBoxf3 its_bb = Domain::bounding_box(its);
        mesh_bb = Biz::Algorithms::BoundingBox::merge(mesh_bb, its_bb);
    }

    // tr_inv = transformation of mesh inverted
    Domain::Transform3d emboss_tr    = cut_projection_tr.inverse();
    BoundingBoxf3 mesh_bb_tr = Biz::Algorithms::BoundingBox::transformed(mesh_bb, emboss_tr);
    std::pair<float, float> z_range{mesh_bb_tr.min.z(), mesh_bb_tr.max.z()};
    OrthoProject cut_projection = create_projection_for_cut(cut_projection_tr, shape_scale, z_range);
    float projection_ratio = (-z_range.first + safe_extension) / (z_range.second - z_range.first + 2 * safe_extension);

    Domain::ExPolygons shapes_data; // is used only when text is reflected to reverse polygon points order
    const Domain::ExPolygons* shapes_ptr = &shapes;
    bool is_text_reflected       = has_reflection(tr);
    if (is_text_reflected) {
        // revert order of points in expolygons
        // CW --> CCW
        shapes_data = shapes; // copy
        for (Domain::ExPolygon& shape : shapes_data) {
            shape.contour.reverse();
            for (Domain::Polygon& hole : shape.holes)
                hole.reverse();
        }
        shapes_ptr = &shapes_data;
    }

    // Use CGAL to cut surface from triangle mesh
    Biz::CGAL::Algorithms::SurfaceCut cut = Biz::CGAL::Algorithms::cut_surface(*shapes_ptr, itss, cut_projection, projection_ratio);

    if (is_text_reflected) {
        for (Biz::CGAL::Algorithms::SurfaceCut::Contour& c : cut.contours)
            std::reverse(c.begin(), c.end());
        for (Domain::Index3& t : cut.indices)
            std::swap(t[0], t[1]);
    }

    if (cut.empty())
        return {}; // There is no valid surface for text projection.
    if (was_canceled())
        return {};

    // !! Projection needs to transform cut
    double depth = input.tri_mesh.shape_provider->get_projection().depth;
    OrthoProject3d projection = create_emboss_projection(input.tri_mesh.is_outside, depth, emboss_tr, cut);
    return cut2model(cut, projection);
}

TriMeshResult cut_per_glyph_surface(const BaseData& input1, const SurfaceVolumeData& input2, std::function<bool()> was_canceled)
{
    // Precalculate bounding boxes of glyphs
    // Separate lines of text to vector of Bounds

    const Domain::EmbossShape& es = input1.tri_mesh.shape_provider->get_shape();
    if (was_canceled())
        return tl::unexpected{ JobIssue::canceled };

    if (es.shapes_with_ids.empty())
        return tl::unexpected{ JobIssue::no_shape };

    const Biz::Emboss::TextLines& text_lines = input1.tri_mesh.shape_provider->get_text_lines();
    assert(get_count_lines(es.shapes_with_ids) == text_lines.size());
    size_t count_lines             = text_lines.size();
    std::vector<BoundingBoxes> bbs = create_line_bounds(es.shapes_with_ids, count_lines);

    size_t s_i_offset = 0; // shape index offset(for next lines)
    indexed_triangle_set result;
    for (size_t text_line_index = 0; text_line_index < text_lines.size(); ++text_line_index) {
        const BoundingBoxes& line_bbs = bbs[text_line_index];
        const TextLine& line          = text_lines[text_line_index];
        PolygonPoints samples         = sample_slice(line, line_bbs, es.scale);
        std::vector<double> angles    = calculate_angles(line_bbs, samples, line.polygon);

        for (size_t i = 0; i < line_bbs.size(); ++i) {
            const BoundingBox& glyph_bb = line_bbs[i];
            if (!glyph_bb.defined)
                continue;

            const double& angle = angles[i];
            auto rotate         = Eigen::AngleAxisd(angle + M_PI_2, Domain::Vec3d::UnitY());

            const PolygonPoint& sample = samples[i];
            Domain::Vec2d offset_vec = Biz::Algorithms::Scaling::unscaled<double>(sample.point); // [in mm]
            auto offset_tr = Eigen::Translation<double, 3>(offset_vec.x(), 0., -offset_vec.y());

            Domain::ExPolygons glyph_shape = es.shapes_with_ids[s_i_offset + i].expoly;
            assert(Biz::Algorithms::ExPolygon::get_extents(glyph_shape) == glyph_bb);

            Domain::Point offset(-Biz::Algorithms::BoundingBox::center(glyph_bb).x(), 0);
            for (Domain::ExPolygon& s : glyph_shape)
                s.translate(offset);

            Domain::Transform3d modify = offset_tr * rotate;
            Domain::Transform3d tr     = input2.transform * modify;
            indexed_triangle_set glyph_its = cut_surface_to_its(glyph_shape, tr, input2.sources, input1, was_canceled);
            // move letter in volume on the right position
            its_transform(glyph_its, modify);

            // Improve: union instead of merge
            Domain::its_merge(result, std::move(glyph_its));

            if (((s_i_offset + i) % 15) && was_canceled())
                return tl::unexpected{ JobIssue::canceled };
        }
        s_i_offset += line_bbs.size();
    }

    if (was_canceled())
        return tl::unexpected{ JobIssue::canceled };

    if (result.empty())
        return tl::unexpected{ JobIssue::no_surface };

    return Biz::Algorithms::TriangleMesh::construct(std::move(result));
}

// input can't be const - cache of font
template <typename Fnc>
TriMeshResult cut_surface(BaseData& input1, const SurfaceVolumeData& input2, const Fnc& was_canceled)
{
    if (!input1.tri_mesh.shape_provider->get_text_lines().empty()) {
        input1.tri_mesh.shape_provider->create_shape();
        return cut_per_glyph_surface(input1, input2, was_canceled);
    }

    const Domain::ExPolygons& shapes = create_shape(*input1.tri_mesh.shape_provider, was_canceled);
    if (was_canceled())
        return tl::unexpected{JobIssue::canceled};

    if (shapes.empty())
        return tl::unexpected{JobIssue::no_shape};

    indexed_triangle_set its = cut_surface_to_its(shapes, input2.transform, input2.sources, input1, was_canceled);
    if (was_canceled())
        return tl::unexpected{JobIssue::canceled};

    if (its.empty())
        return tl::unexpected{JobIssue::no_surface};

    return Biz::Algorithms::TriangleMesh::construct(std::move(its));
}

ModelSources create_sources(const Domain::ModelVolumePtrs& volumes, std::optional<size_t> text_volume_id)
{
    ModelSources result;
    result.reserve(volumes.size() - 1);
    for (const Domain::ModelVolume* v : volumes) {
        if (text_volume_id.has_value() && v->id().id == *text_volume_id)
            continue;
        // skip modifiers and negative volumes, ...
        if (!v->is_model_part())
            continue;
        const Domain::TriangleMesh& tm = v->mesh();
        if (tm.empty())
            continue;
        if (tm.its.empty())
            continue;
        result.push_back({v->mesh_ptr(), v->get_matrix()});
    }
    return result;
}

bool queue_job(std::unique_ptr<Job> job)
{
    if (job == nullptr)
        return false;

    const Domain::SelectionId project_id = job->project_id();

    std::function<std::unique_ptr<Job>(StopToken, std::unique_ptr<Job>&&)> process =
        [](StopToken stop_token, std::unique_ptr<Job>&& job) -> std::unique_ptr<Job>
    {
        job->process(stop_token);
        return job;
    };
    std::function<void(std::unique_ptr<Job>&&)> finalize = [](std::unique_ptr<Job>&& job)
    { job->finalize(); };

    Biz::Platform::PlatformServices::instance()
        .job_manager()
        .create_job("EmbossJob", process, std::move(job))
        .set_project_id(project_id)
        .on_result(finalize)
        .start();
    return true;
}

} // namespace
