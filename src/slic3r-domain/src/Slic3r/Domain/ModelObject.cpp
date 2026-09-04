#include "Slic3r/Domain/ModelObject.hpp"

#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Utils.hpp"

#include <set>

namespace Slic3r::Domain {

ModelObject* ModelObject::new_copy(const ModelObject& rhs)
{
    auto* ret = new ModelObject(rhs);
    assert(ret->id() == rhs.id());
    return ret;
}

ModelObject* ModelObject::new_copy(ModelObject&& rhs)
{
    auto* ret = new ModelObject(std::move(rhs));
    assert(ret->id() == rhs.id());
    return ret;
}

ModelObject ModelObject::make_copy(const ModelObject& rhs)
{
    ModelObject ret(rhs);
    assert(ret.id() == rhs.id());
    return ret;
}

ModelObject ModelObject::make_copy(ModelObject&& rhs)
{
    ModelObject ret(std::move(rhs));
    assert(ret.id() == rhs.id());
    return ret;
}

ModelObject* ModelObject::new_clone(const ModelObject& rhs)
{
    // Default constructor assigning an invalid ID.
    auto obj = new ModelObject(-1);
    obj->assign_clone(rhs);
    assert(obj->id().valid() && obj->id() != rhs.id());
    return obj;
}

std::unique_ptr<ModelObject>
ModelObject::new_clone(const ModelObject& rhs, const std::vector<size_t>& instance_ids_to_clone)
{
    const std::set<size_t> ids_to_clone(instance_ids_to_clone.begin(), instance_ids_to_clone.end());

    std::unique_ptr<ModelObject> new_object(new ModelObject(-1));
    new_object->name                 = rhs.name;
    new_object->input_file           = rhs.input_file;
    new_object->object_settings      = rhs.object_settings;
    new_object->object_settings_sla  = rhs.object_settings_sla;
    new_object->sla_support_points   = rhs.sla_support_points;
    new_object->sla_points_status    = rhs.sla_points_status;
    new_object->sla_drain_holes      = rhs.sla_drain_holes;
    new_object->layer_config_ranges  = rhs.layer_config_ranges;
    new_object->layer_height_profile = rhs.layer_height_profile;
    new_object->origin_translation   = rhs.origin_translation;
    new_object->cut_id               = rhs.cut_id;
    new_object->copy_transformation_caches(rhs);

    new_object->volumes.reserve(rhs.volumes.size());
    for (ModelVolume* volume : rhs.volumes) {
        new_object->volumes.emplace_back(new ModelVolume(*volume));
        new_object->volumes.back()->set_model_object(new_object.get());
    }

    for (const ModelInstance* instance : rhs.instances) {
        if (ids_to_clone.contains(instance->id().id)) {
            new_object->instances.emplace_back(new ModelInstance(*instance));
            new_object->instances.back()->set_model_object(new_object.get());
        }
    }

    new_object->assign_new_unique_ids_recursive();
    assert(new_object->id().valid() && new_object->id() != rhs.id());

    return new_object;
}

ModelObject ModelObject::make_clone(const ModelObject& rhs)
{
    // Default constructor assigning an invalid ID.
    ModelObject obj(-1);
    obj.assign_clone(rhs);
    assert(obj.id().valid() && obj.id() != rhs.id());
    return obj;
}

// Maintains the m_model pointer.
ModelObject& ModelObject::assign_copy(const ModelObject& rhs)
{
    assert(this->id().invalid() || this->id() == rhs.id());
    this->copy_id(rhs);

    this->name                 = rhs.name;
    this->input_file           = rhs.input_file;
    this->object_settings      = rhs.object_settings;
    this->object_settings_sla  = rhs.object_settings_sla;
    this->sla_support_points   = rhs.sla_support_points;
    this->sla_points_status    = rhs.sla_points_status;
    this->sla_drain_holes      = rhs.sla_drain_holes;
    this->layer_config_ranges  = rhs.layer_config_ranges;
    this->layer_height_profile = rhs.layer_height_profile;
    this->origin_translation   = rhs.origin_translation;
    this->cut_id               = rhs.cut_id;
    this->copy_transformation_caches(rhs);

    this->clear_volumes();
    this->volumes.reserve(rhs.volumes.size());
    for (ModelVolume* model_volume : rhs.volumes) {
        this->volumes.emplace_back(new ModelVolume(*model_volume));
        this->volumes.back()->set_model_object(this);
    }

    this->clear_instances();
    this->instances.reserve(rhs.instances.size());
    for (const ModelInstance* model_instance : rhs.instances) {
        this->instances.emplace_back(new ModelInstance(*model_instance));
        this->instances.back()->set_model_object(this);
    }

    return *this;
}

// Maintains the m_model pointer.
ModelObject& ModelObject::assign_copy(ModelObject&& rhs)
{
    assert(this->id().invalid());
    this->copy_id(rhs);

    this->name                 = std::move(rhs.name);
    this->input_file           = std::move(rhs.input_file);
    this->object_settings      = std::move(rhs.object_settings);
    this->object_settings_sla  = std::move(rhs.object_settings_sla);
    this->sla_support_points   = std::move(rhs.sla_support_points);
    this->sla_points_status    = std::move(rhs.sla_points_status);
    this->sla_drain_holes      = std::move(rhs.sla_drain_holes);
    this->layer_config_ranges  = std::move(rhs.layer_config_ranges);
    this->layer_height_profile = std::move(rhs.layer_height_profile);
    this->origin_translation   = std::move(rhs.origin_translation);
    this->copy_transformation_caches(rhs);

    this->clear_volumes();
    this->volumes = std::move(rhs.volumes);
    rhs.volumes.clear();
    for (ModelVolume* model_volume : this->volumes) {
        model_volume->set_model_object(this);
    }

    this->clear_instances();
    this->instances = std::move(rhs.instances);
    rhs.instances.clear();
    for (ModelInstance* model_instance : this->instances) {
        model_instance->set_model_object(this);
    }

    return *this;
}

ModelObject& ModelObject::assign_clone(const ModelObject& rhs)
{
    this->assign_copy(rhs);
    assert(this->id().valid() && this->id() == rhs.id());
    this->assign_new_unique_ids_recursive();
    assert(this->id().valid() && this->id() != rhs.id());
    return *this;
}

Model* ModelObject::get_model() { return m_model; }

const Model* ModelObject::get_model() const { return m_model; }

void ModelObject::set_model(Model* model) { m_model = model; }

ModelVolume* ModelObject::add_volume(const ModelVolume& other, ModelVolumeType type)
{
    ModelVolume* v = new ModelVolume(this, other);
    if (type != ModelVolumeType::INVALID && v->type() != type) {
        v->set_type(type);
    }

    v->cut_info = other.cut_info;
    this->volumes.push_back(v);

    // The volume should already be centered at this point of time when copying shared pointers of the triangle mesh and convex hull.
    return v;
}

void ModelObject::delete_volume(const size_t idx)
{
    ModelVolumePtrs::iterator i = this->volumes.begin() + idx;
    delete *i;
    this->volumes.erase(i);
    this->invalidate_bounding_box();
}

void ModelObject::clear_volumes()
{
    for (ModelVolume* v : this->volumes) {
        delete v;
    }

    this->volumes.clear();
    this->invalidate_bounding_box();
}

void ModelObject::sort_volumes(bool full_sort)
{
    // Sort volumes inside the object to order "Model Part, Negative Volume, Modifier, Support Blocker and Support Enforcer. "
    if (full_sort) {
        std::stable_sort(this->volumes.begin(), this->volumes.end(), [](ModelVolume* vl, ModelVolume* vr) { return vl->type() < vr->type(); });
    } else {
        // Sort has to control "place" of the support blockers/enforcers. But one of the model parts has to be in the first place.
        std::stable_sort(this->volumes.begin(), this->volumes.end(), [](ModelVolume* vl, ModelVolume* vr) {
            ModelVolumeType vl_type = vl->type() > ModelVolumeType::PARAMETER_MODIFIER ? vl->type() : ModelVolumeType::PARAMETER_MODIFIER;
            ModelVolumeType vr_type = vr->type() > ModelVolumeType::PARAMETER_MODIFIER ? vr->type() : ModelVolumeType::PARAMETER_MODIFIER;
            return vl_type < vr_type;
        });
    }
}

ModelInstance* ModelObject::add_instance()
{
    ModelInstance* i = new ModelInstance(this);
    this->instances.push_back(i);
    this->invalidate_bounding_box();
    return i;
}

ModelInstance* ModelObject::add_instance(const ModelInstance& other)
{
    ModelInstance* i = new ModelInstance(this, other);
    this->instances.push_back(i);
    this->invalidate_bounding_box();
    return i;
}

ModelInstance* ModelObject::add_instance(const Transformation& trafo)
{
    ModelInstance* instance = add_instance();
    instance->set_transformation(trafo);
    return instance;
}

void ModelObject::delete_instance(size_t idx)
{
    ModelInstancePtrs::iterator i = this->instances.begin() + idx;
    delete *i;
    this->instances.erase(i);
    this->invalidate_bounding_box();
}

void ModelObject::delete_last_instance() { this->delete_instance(this->instances.size() - 1); }

void ModelObject::clear_instances()
{
    for (ModelInstance* i : this->instances) {
        delete i;
    }

    this->instances.clear();
    this->invalidate_bounding_box();
}

bool ModelObject::is_multiparts() const { return this->volumes.size() > 1; }

bool ModelObject::is_fdm_support_painted() const
{
    return std::any_of(this->volumes.cbegin(), this->volumes.cend(), [](const ModelVolume* mv) { return mv->is_fdm_support_painted(); });
}

bool ModelObject::is_seam_painted() const
{
    return std::any_of(this->volumes.cbegin(), this->volumes.cend(), [](const ModelVolume* mv) { return mv->is_seam_painted(); });
}

bool ModelObject::is_mm_painted() const
{
    return std::any_of(this->volumes.cbegin(), this->volumes.cend(), [](const ModelVolume* mv) { return mv->is_mm_painted(); });
}

bool ModelObject::is_fuzzy_skin_painted() const
{
    return std::any_of(this->volumes.cbegin(), this->volumes.cend(), [](const ModelVolume* mv) { return mv->is_fuzzy_skin_painted(); });
}

bool ModelObject::is_text() const { return this->volumes.size() == 1 && this->volumes[0]->is_text(); }

bool ModelObject::has_custom_layering() const { return !this->layer_config_ranges.empty() || !this->layer_height_profile.empty(); }

double ModelObject::min_z() const
{
    const_cast<ModelObject*>(this)->update_min_max_z();
    return m_bounding_box_exact.min.z();
}

double ModelObject::max_z() const
{
    const_cast<ModelObject*>(this)->update_min_max_z();
    return m_bounding_box_exact.max.z();
}

void ModelObject::update_min_max_z()
{
    assert(!this->instances.empty());
    if (!m_min_max_z_valid && !this->instances.empty()) {
        m_min_max_z_valid = true;
        const Transform3d mat_instance = this->instances.front()->get_transformation().get_matrix();
        double global_min_z = std::numeric_limits<double>::max();
        double global_max_z = -std::numeric_limits<double>::max();
        for (const ModelVolume* v : this->volumes) {
            if (v->is_model_part()) {
                const Transform3d m = mat_instance * v->get_matrix();
                const Vec3d row_z = m.linear().row(2).cast<double>();
                const double shift_z = m.translation().z();
                double this_min_z = std::numeric_limits<double>::max();
                double this_max_z = -std::numeric_limits<double>::max();
                for (const Vec3f& p : v->mesh().its.vertices) {
                    double z = row_z.dot(p.cast<double>());
                    this_min_z = std::min(this_min_z, z);
                    this_max_z = std::max(this_max_z, z);
                }

                this_min_z += shift_z;
                this_max_z += shift_z;
                global_min_z = std::min(global_min_z, this_min_z);
                global_max_z = std::max(global_max_z, this_max_z);
            }
        }

        m_bounding_box_exact.min.z() = global_min_z;
        m_bounding_box_exact.max.z() = global_max_z;
    }
}

void ModelObject::translate_instances(const Vec3d& vector)
{
    for (size_t i = 0; i < this->instances.size(); ++i) {
        this->translate_instance(i, vector);
    }
}

void ModelObject::translate_instance(const size_t instance_idx, const Vec3d& vector)
{
    assert(instance_idx < this->instances.size());
    ModelInstance* i = this->instances[instance_idx];
    i->set_offset(i->get_offset() + vector);
    this->invalidate_bounding_box();
}

// Non-transformed (non-rotated, non-scaled, non-translated) sum of non-modifier object volumes.
// Currently used by ModelObject::mesh(), to calculate the 2D envelope for 2D plater
// and to display the object statistics at ModelObject::print_info().
TriangleMesh ModelObject::raw_mesh() const
{
    TriangleMesh mesh;
    for (const ModelVolume* v : this->volumes)
        if (v->is_model_part()) {
            TriangleMesh vol_mesh(v->mesh());
            vol_mesh.transform(v->get_matrix());
            mesh.merge(vol_mesh);
        }
    return mesh;
}

// Non-transformed (non-rotated, non-scaled, non-translated) sum of non-modifier object volumes.
// Currently used by ModelObject::mesh(), to calculate the 2D envelope for 2D plater
// and to display the object statistics at ModelObject::print_info().
indexed_triangle_set ModelObject::raw_indexed_triangle_set() const
{
    size_t num_vertices = 0;
    size_t num_faces = 0;
    for (const ModelVolume* v : this->volumes) {
        if (v->is_model_part()) {
            num_vertices += v->mesh().its.vertices.size();
            num_faces += v->mesh().its.indices.size();
        }
    }

    indexed_triangle_set out;
    out.vertices.reserve(num_vertices);
    out.indices.reserve(num_faces);
    for (const ModelVolume* v : this->volumes) {
        if (v->is_model_part()) {
            size_t i = out.vertices.size();
            size_t j = out.indices.size();
            Slic3r::append(out.vertices, v->mesh().its.vertices);
            Slic3r::append(out.indices, v->mesh().its.indices);
            const Transform3d& m = v->get_matrix();
            for (; i < out.vertices.size(); ++i) {
                out.vertices[i] = (m * out.vertices[i].cast<double>()).cast<float>().eval();
            }

            if (v->is_left_handed()) {
                for (; j < out.indices.size(); ++j) {
                    std::swap(out.indices[j][0], out.indices[j][1]);
                }
            }
        }
    }

    return out;
}

size_t ModelObject::facets_count() const
{
    size_t num = 0;
    for (const ModelVolume* v : this->volumes) {
        if (v->is_model_part()) {
            num += v->mesh().facets_count();
        }
    }

    return num;
}

size_t ModelObject::parts_count() const
{
    size_t num = 0;
    for (const ModelVolume* v : this->volumes) {
        if (v->is_model_part()) {
            ++num;
        }
    }

    return num;
}

void ModelObject::invalidate_cut()
{
    this->cut_id.invalidate();
    for (ModelVolume* volume : this->volumes) {
        volume->invalidate_cut_info();
    }
}

void ModelObject::delete_connectors()
{
    for (int id = int(this->volumes.size()) - 1; id >= 0; id--) {
        if (this->volumes[id]->is_cut_connector()) {
            this->delete_volume(size_t(id));
        }
    }
}

bool ModelObject::has_connectors() const
{
    assert(is_cut());
    for (const ModelVolume* v : this->volumes) {
        if (v->cut_info.is_connector)
            return true;
    }

    return false;
}

bool ModelObject::has_solid_mesh() const
{
    for (const ModelVolume* volume : this->volumes) {
        if (volume->is_model_part()) {
            return true;
        }
    }

    return false;
}

bool ModelObject::has_negative_volume_mesh() const
{
    for (const ModelVolume* volume : this->volumes) {
        if (volume->is_negative_volume()) {
            return true;
        }
    }

    return false;
}

bool ModelObject::has_sla_drain_holes() const { return !this->sla_drain_holes.empty(); }

bool ModelObject::is_cut() const { return this->cut_id.valid(); }

double ModelObject::get_instance_min_z(const size_t instance_idx) const
{
    double min_z = std::numeric_limits<double>::max();

    const ModelInstance* inst = this->instances[instance_idx];
    const Transform3d mi = inst->get_matrix_no_offset();

    for (const ModelVolume* v : this->volumes) {
        if (!v->is_model_part())
            continue;

        const Transform3d mv = mi * v->get_matrix();
        const TriangleMesh& hull = v->get_convex_hull();
        for (const stl_triangle_vertex_indices& facet : hull.its.indices) {
            for (int i = 0; i < 3; ++i) {
                min_z = std::min(min_z, (mv * hull.its.vertices[facet[i]].cast<double>()).z());
            }
        }
    }

    return min_z + inst->get_offset(Z);
}

double ModelObject::get_instance_max_z(const size_t instance_idx) const
{
    double max_z = std::numeric_limits<double>::lowest();

    const ModelInstance* inst = this->instances[instance_idx];
    const Transform3d mi = inst->get_matrix_no_offset();

    for (const ModelVolume* v : this->volumes) {
        if (!v->is_model_part())
            continue;

        const Transform3d mv = mi * v->get_matrix();
        const TriangleMesh& hull = v->get_convex_hull();
        for (const stl_triangle_vertex_indices& facet : hull.its.indices) {
            for (int i = 0; i < 3; ++i) {
                max_z = std::max(max_z, (mv * hull.its.vertices[facet[i]].cast<double>()).z());
            }
        }
    }

    return max_z + inst->get_offset(Z);
}

// This method could only be called before the meshes of this ModelVolumes are not shared!
void ModelObject::scale_mesh_after_creation(const float scale)
{
    for (ModelVolume* v : this->volumes) {
        v->scale_geometry_after_creation(scale);
        v->set_offset(Vec3d(scale, scale, scale).cwiseProduct(v->get_offset()));
    }

    this->invalidate_bounding_box();
}

void ModelObject::clone_for_cut(ModelObject** obj)
{
    (*obj) = ModelObject::new_clone(*this);
    (*obj)->set_model(this->get_model());
    (*obj)->sla_support_points.clear();
    (*obj)->sla_drain_holes.clear();
    (*obj)->sla_points_status = SLA::PointsStatus::NoPoints;
    (*obj)->clear_volumes();
    (*obj)->input_file.clear();
}

void ModelObject::invalidate_bounding_box() const
{
    m_bounding_box_approx_valid   = false;
    m_bounding_box_exact_valid    = false;
    m_min_max_z_valid             = false;
    m_raw_bounding_box_valid      = false;
    m_raw_mesh_bounding_box_valid = false;
}

ModelObject::~ModelObject()
{
    this->clear_volumes();
    this->clear_instances();
}

ModelObject& ModelObject::operator=(const ModelObject& rhs)
{
    this->assign_copy(rhs);
    m_model = rhs.m_model;
    assert(this->id().valid());
    assert(this->layer_height_profile.id().valid());
    assert(this->id() != this->layer_height_profile.id());
    assert(this->id() == rhs.id());
    assert(this->layer_height_profile.id() == rhs.layer_height_profile.id());
    return *this;
}

ModelObject& ModelObject::operator=(ModelObject&& rhs) noexcept
{
    this->assign_copy(std::move(rhs));
    m_model = rhs.m_model;
    assert(this->id().valid());
    assert(this->layer_height_profile.id().valid());
    assert(this->id() != this->layer_height_profile.id());
    assert(this->id() == rhs.id());
    assert(this->layer_height_profile.id() == rhs.layer_height_profile.id());
    return *this;
}

void ModelObject::assign_new_unique_ids_recursive()
{
    this->set_new_unique_id();
    for (ModelVolume* model_volume : this->volumes) {
        model_volume->assign_new_unique_ids_recursive();
    }

    for (ModelInstance* model_instance : this->instances) {
        model_instance->assign_new_unique_ids_recursive();
    }

    this->layer_height_profile.set_new_unique_id();
}

void ModelObject::set_new_unique_id()
{
    ObjectBase::set_new_unique_id();
    this->layer_height_profile.set_new_unique_id();
}

void ModelObject::copy_transformation_caches(const ModelObject& src)
{
    m_bounding_box_approx         = src.m_bounding_box_approx;
    m_bounding_box_approx_valid   = src.m_bounding_box_approx_valid;
    m_bounding_box_exact          = src.m_bounding_box_exact;
    m_bounding_box_exact_valid    = src.m_bounding_box_exact_valid;
    m_min_max_z_valid             = src.m_min_max_z_valid;
    m_raw_bounding_box            = src.m_raw_bounding_box;
    m_raw_bounding_box_valid      = src.m_raw_bounding_box_valid;
    m_raw_mesh_bounding_box       = src.m_raw_mesh_bounding_box;
    m_raw_mesh_bounding_box_valid = src.m_raw_mesh_bounding_box_valid;
}

} // namespace Slic3r::Domain
