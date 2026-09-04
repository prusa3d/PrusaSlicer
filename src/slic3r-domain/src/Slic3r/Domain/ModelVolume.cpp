#include "Slic3r/Domain/ModelVolume.hpp"

#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"

using Slic3r::Domain::TriangleSelector::TriangleSplittingData;
using Slic3r::Domain::TriangleSelector::TriangleStateType;

namespace Slic3r::Domain {

ModelObject* ModelVolume::get_object() const { return this->object; }

ModelVolumeType ModelVolume::type() const { return m_type; }

void ModelVolume::set_type(const ModelVolumeType t) { m_type = t; }

bool ModelVolume::is_model_part() const { return m_type == ModelVolumeType::MODEL_PART; }

bool ModelVolume::is_negative_volume() const { return m_type == ModelVolumeType::NEGATIVE_VOLUME; }

bool ModelVolume::is_modifier() const { return m_type == ModelVolumeType::PARAMETER_MODIFIER; }

bool ModelVolume::is_support_enforcer() const { return m_type == ModelVolumeType::SUPPORT_ENFORCER; }

bool ModelVolume::is_support_blocker() const { return m_type == ModelVolumeType::SUPPORT_BLOCKER; }

bool ModelVolume::is_support_modifier() const { return m_type == ModelVolumeType::SUPPORT_BLOCKER || m_type == ModelVolumeType::SUPPORT_ENFORCER; }

bool ModelVolume::is_text() const { return this->text_configuration.has_value(); }

bool ModelVolume::is_svg() const { return this->emboss_shape.has_value() && !this->text_configuration.has_value(); }

bool ModelVolume::is_the_only_one_part() const
{
    if (m_type != ModelVolumeType::MODEL_PART)
        return false;

    if (this->object == nullptr)
        return false;

    for (const ModelVolume* v : this->object->volumes) {
        if (v == nullptr)
            continue;
        // is this volume?
        if (v->id() == this->id())
            continue;
        // exist another model part in object?
        if (v->type() == ModelVolumeType::MODEL_PART)
            return false;
    }

    return true;
}

void ModelVolume::reset_extra_facets()
{
    this->supported_facets.reset();
    this->seam_facets.reset();
    this->mm_segmentation_facets.reset();
    this->fuzzy_skin_facets.reset();
}

// Extract the current extruder ID based on this ModelVolume's config and the parent ModelObject's config.
int ModelVolume::extruder_id() const
{
    if (this->is_model_part()) {
        const std::optional<Domain::ConfigItem> volume_extruder_item = this->volume_settings.overrides.get("extruder");
        if (volume_extruder_item.has_value() && volume_extruder_item->get<int>() > 0) {
            return volume_extruder_item->get<int>();
        }

        return this->object->object_settings.items.opt("extruder").get<int>();
    }

    return -1;
}

void ModelVolume::discard_splittable() { m_is_splittable = 0; }

// This method could only be called before the meshes of this ModelVolumes are not shared!
void ModelVolume::scale_geometry_after_creation(const Vec3f& versor)
{
    const_cast<TriangleMesh*>(m_mesh.get())->scale(versor);
    const_cast<TriangleMesh*>(m_convex_hull.get())->scale(versor);
}

void ModelVolume::scale_geometry_after_creation(const float scale) { this->scale_geometry_after_creation(Vec3f(scale, scale, scale)); }

const TriangleMesh& ModelVolume::get_convex_hull() const { return *m_convex_hull.get(); }

const std::shared_ptr<const TriangleMesh>& ModelVolume::get_convex_hull_shared_ptr() const { return m_convex_hull; }

const Transformation& ModelVolume::get_transformation() const { return m_transformation; }

void ModelVolume::set_transformation(const Transformation& transformation)
{
    m_transformation = transformation;
    this->object->invalidate_bounding_box();
}

void ModelVolume::set_transformation(const Transform3d& trafo)
{
    m_transformation.set_matrix(trafo);
    this->object->invalidate_bounding_box();
}

Vec3d ModelVolume::get_offset() const { return m_transformation.get_offset(); }

double ModelVolume::get_offset(const Axis axis) const { return m_transformation.get_offset(axis); }

void ModelVolume::set_offset(const Vec3d& offset) { m_transformation.set_offset(offset); }

void ModelVolume::set_offset(const Axis axis, double offset) { m_transformation.set_offset(axis, offset); }

Vec3d ModelVolume::get_rotation() const { return m_transformation.get_rotation(); }

double ModelVolume::get_rotation(const Axis axis) const { return m_transformation.get_rotation(axis); }

void ModelVolume::set_rotation(const Vec3d& rotation) { m_transformation.set_rotation(rotation); }

void ModelVolume::set_rotation(const Axis axis, double rotation) { m_transformation.set_rotation(axis, rotation); }

Vec3d ModelVolume::get_scaling_factor() const { return m_transformation.get_scaling_factor(); }

double ModelVolume::get_scaling_factor(const Axis axis) const { return m_transformation.get_scaling_factor(axis); }

void ModelVolume::set_scaling_factor(const Vec3d& scaling_factor) { m_transformation.set_scaling_factor(scaling_factor); }

void ModelVolume::set_scaling_factor(const Axis axis, double scaling_factor) { m_transformation.set_scaling_factor(axis, scaling_factor); }

Vec3d ModelVolume::get_mirror() const { return m_transformation.get_mirror(); }

double ModelVolume::get_mirror(const Axis axis) const { return m_transformation.get_mirror(axis); }

bool ModelVolume::is_left_handed() const { return m_transformation.is_left_handed(); }

void ModelVolume::set_mirror(const Vec3d& mirror) { m_transformation.set_mirror(mirror); }

void ModelVolume::set_mirror(const Axis axis, double mirror) { m_transformation.set_mirror(axis, mirror); }

const Transform3d& ModelVolume::get_matrix() const { return m_transformation.get_matrix(); }

Transform3d ModelVolume::get_matrix_no_offset() const { return m_transformation.get_matrix_no_offset(); }

void ModelVolume::set_new_unique_id()
{
    ObjectBase::set_new_unique_id();
    this->supported_facets.set_new_unique_id();
    this->seam_facets.set_new_unique_id();
    this->mm_segmentation_facets.set_new_unique_id();
    this->fuzzy_skin_facets.set_new_unique_id();
}

bool ModelVolume::is_fdm_support_painted() const { return !this->supported_facets.empty(); }

bool ModelVolume::is_seam_painted() const { return !this->seam_facets.empty(); }

bool ModelVolume::is_mm_painted() const { return !this->mm_segmentation_facets.empty(); }

bool ModelVolume::is_fully_mm_painted() const
{
    if (!this->is_mm_painted()) {
        return false;
    }

    // A volume is fully painted when no facet (or child of a split facet) keeps the default (NONE) state.
    const TriangleSplittingData& data = this->mm_segmentation_facets.get_data();
    return !data.used_states[static_cast<size_t>(TriangleStateType::NONE)];
}

bool ModelVolume::is_fuzzy_skin_painted() const { return !this->fuzzy_skin_facets.empty(); }

bool ModelVolume::is_painted() const
{
    return is_fdm_support_painted()
        || is_seam_painted()
        || is_mm_painted()
        || is_fuzzy_skin_painted();
}

std::vector<size_t> ModelVolume::get_extruders_from_multi_material_painting() const
{
    if (!this->is_mm_painted())
        return {};

    assert(static_cast<size_t>(TriangleSelector::TriangleStateType::Extruder1) - 1 == 0);
    const TriangleSelector::TriangleSplittingData& data = this->mm_segmentation_facets.get_data();

    std::vector<size_t> extruders;
    for (size_t state_idx = static_cast<size_t>(TriangleSelector::TriangleStateType::Extruder1); state_idx < data.used_states.size(); ++state_idx) {
        if (data.used_states[state_idx]) {
            extruders.emplace_back(state_idx - 1);
        }
    }

    return extruders;
}

void ModelVolume::set_model_object(ModelObject* model_object) { this->object = model_object; }

void ModelVolume::assign_new_unique_ids_recursive()
{
    ObjectBase::set_new_unique_id();
    this->supported_facets.set_new_unique_id();
    this->seam_facets.set_new_unique_id();
    this->mm_segmentation_facets.set_new_unique_id();
    this->fuzzy_skin_facets.set_new_unique_id();
}

void ModelVolume::transform_this_mesh(const Transform3d& mesh_trafo, const bool fix_left_handed)
{
    TriangleMesh mesh = this->mesh();
    mesh.transform(mesh_trafo, fix_left_handed);
    this->set_mesh(std::move(mesh));
    TriangleMesh convex_hull = this->get_convex_hull();
    convex_hull.transform(mesh_trafo, fix_left_handed);
    m_convex_hull = std::make_shared<TriangleMesh>(std::move(convex_hull));
    // Let the rest of the application know that the geometry changed, so the meshes have to be reloaded.
    this->set_new_unique_id();
}

bool ModelVolume::check()
{
    assert(this->id().valid());
    assert(this->supported_facets.id().valid());
    assert(this->seam_facets.id().valid());
    assert(this->mm_segmentation_facets.id().valid());
    assert(this->fuzzy_skin_facets.id().valid());
    assert(this->id() != this->supported_facets.id());
    assert(this->id() != this->seam_facets.id());
    assert(this->id() != this->mm_segmentation_facets.id());
    assert(this->id() != this->fuzzy_skin_facets.id());
    return true;
}

size_t ModelVolume::get_extruder_color_idx(const ModelVolume& model_volume, const int extruders_count)
{
    const int extruder_id = model_volume.extruder_id();
    return (extruder_id <= 0 || extruder_id > extruders_count) ? 0 : extruder_id - 1;
}

ModelVolumeType ModelVolume::type_from_string(const std::string& s)
{
    // Legacy support
    if (s == "1")
        return ModelVolumeType::PARAMETER_MODIFIER;
    // New type (supporting the support enforcers & blockers)
    if (s == "ModelPart")
        return ModelVolumeType::MODEL_PART;
    if (s == "NegativeVolume")
        return ModelVolumeType::NEGATIVE_VOLUME;
    if (s == "ParameterModifier")
        return ModelVolumeType::PARAMETER_MODIFIER;
    if (s == "SupportEnforcer")
        return ModelVolumeType::SUPPORT_ENFORCER;
    if (s == "SupportBlocker")
        return ModelVolumeType::SUPPORT_BLOCKER;
    assert(s == "0");
    // Default value if invalid type string received.
    return ModelVolumeType::MODEL_PART;
}

std::string ModelVolume::type_to_string(const ModelVolumeType t)
{
    switch (t) {
    case ModelVolumeType::MODEL_PART:
        return "ModelPart";
    case ModelVolumeType::NEGATIVE_VOLUME:
        return "NegativeVolume";
    case ModelVolumeType::PARAMETER_MODIFIER:
        return "ParameterModifier";
    case ModelVolumeType::SUPPORT_ENFORCER:
        return "SupportEnforcer";
    case ModelVolumeType::SUPPORT_BLOCKER:
        return "SupportBlocker";
    default:
        assert(false);
        return "ModelPart";
    }
}

} // namespace Slic3r::Domain
