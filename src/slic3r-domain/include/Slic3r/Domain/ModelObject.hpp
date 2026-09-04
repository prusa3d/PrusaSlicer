#pragma once

#include "Slic3r/Domain/Forward.hpp"
#include "Slic3r/Domain/ConfigBoxesFDM.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/LayerHeightProfile.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/SLA/DrainHole.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

namespace Slic3r::Biz::Algorithms::ModelObject {
void translate(Domain::ModelObject&, double, double, double);
const Domain::BoundingBox3d& bounding_box_approx(const Domain::ModelObject&);
const Domain::BoundingBox3d& bounding_box_exact(const Domain::ModelObject&);
const Domain::BoundingBox3d& raw_bounding_box(const Domain::ModelObject&);
const Domain::BoundingBox3d& raw_mesh_bounding_box(const Domain::ModelObject&);
} // namespace Slic3r::Biz::Algorithms::ModelObject

namespace Slic3r::Biz::Algorithms::Model {
Domain::ModelObject* add_object(Domain::Model*, const char*, const char*, Domain::TriangleMesh&&);
} // namespace Slic3r::Biz::Algorithms::Model

namespace cereal {
template<class Archive> void save(Archive&, const Slic3r::Domain::ModelObject&);
template<class Archive> void load(Archive&, Slic3r::Domain::ModelObject&);
template<class Archive> void load(Archive& ar, Slic3r::Domain::Model& model);
} // namespace cereal

namespace Slic3r::Domain {

/**
 * A printable object, possibly having multiple print volumes (each with its own set of parameters and materials),
 * and possibly having multiple modifier volumes, each modifier volume with its set of parameters and materials.
 * Each ModelObject may be instantiated multiple times, each instance having different placement on the print bed,
 * different rotation and different uniform scaling.
 */
class ModelObject final : public ObjectBase
{
public:
    std::string             name;
    std::string             input_file;    // XXX: consider fs::path
    // Instances of this ModelObject. Each instance defines a shift on the print bed, rotation around the Z axis and a uniform scaling.
    // Instances are owned by this ModelObject.
    ModelInstancePtrs       instances;
    // Printable and modifier volumes, each with its material ID and a set of override parameters.
    // ModelVolumes are owned by this ModelObject.
    ModelVolumePtrs         volumes;
    // Configuration parameters specific to a single ModelObject, overriding the global Slic3r settings.
    ObjectSettings          object_settings;
    SLAObjectSettings       object_settings_sla;

    // Variation of a layer thickness for spans of Z coordinates + optional parameter overrides.
    LayerConfigRanges       layer_config_ranges;

    // Profile of increasing z to a layer height, to be linearly interpolated when calculating the layers.
    // The pairs of <z, layer_height> are packed into a 1D array.
    LayerHeightProfile      layer_height_profile;

    // This vector holds the position of selected support points for SLA. The data are
    // saved in mesh coordinates to allow using them for several instances.
    // The format is (x, y, z, point_size, supports_island)
    SLA::SupportPoints      sla_support_points;
    // To keep track of where the points came from (used for synchronization between
    // the SLA gizmo and the backend).
    SLA::PointsStatus       sla_points_status = SLA::PointsStatus::NoPoints;

    // Holes to be drilled into the object, so resin can flow out
    SLA::DrainHoles         sla_drain_holes;

    // Connectors to be added into the object before cut and are used to create a solid/negative volumes during a cut performing.
    CutConnectors           cut_connectors;
    CutId                   cut_id;

    // This vector accumulates the total translation applied to the object by the
    // center_around_origin() method. Callers might want to apply the same translation
    // to new volumes before adding them to this object in order to preserve alignment
    // when user expects that.
    Vec3d                   origin_translation;

    static ModelObject*     new_copy(const ModelObject& rhs);
    static ModelObject*     new_copy(ModelObject&& rhs);
    static ModelObject      make_copy(const ModelObject& rhs);
    static ModelObject      make_copy(ModelObject&& rhs);
    static ModelObject*     new_clone(const ModelObject& rhs);
    static ModelObject      make_clone(const ModelObject& rhs);
    static std::unique_ptr<ModelObject>
    new_clone(const ModelObject& rhs, const std::vector<size_t>& instance_ids_to_clone);

    ModelObject&            assign_copy(const ModelObject& rhs);
    ModelObject&            assign_copy(ModelObject&& rhs);
    ModelObject&            assign_clone(const ModelObject& rhs);

    Model*                  get_model();
    const Model*            get_model() const;

    ModelVolume*            add_volume(const ModelVolume& volume, ModelVolumeType type = ModelVolumeType::INVALID);
    void                    delete_volume(size_t idx);
    void                    clear_volumes();
    void                    sort_volumes(bool full_sort);

    ModelInstance*          add_instance();
    ModelInstance*          add_instance(const ModelInstance& instance);
    ModelInstance*          add_instance(const Transformation& trafo);
    void                    delete_instance(size_t idx);
    void                    delete_last_instance();
    void                    clear_instances();

    bool                    is_multiparts() const;
    // Checks if any of object volume is painted using the fdm support painting gizmo.
    bool                    is_fdm_support_painted() const;
    // Checks if any of object volume is painted using the seam painting gizmo.
    bool                    is_seam_painted() const;
    // Checks if any of object volume is painted using the multi-material painting gizmo.
    bool                    is_mm_painted() const;
    // Checks if any of object volume is painted using the fuzzy skin painting gizmo.
    bool                    is_fuzzy_skin_painted() const;
    // Checks if object contains just one volume and it's a text
    bool                    is_text() const;

    // This object may have a varying layer height by painting or by a table.
    // Even if true is returned, the layer height profile may be "flat" with no difference to default layering.
    bool                    has_custom_layering() const;

    // Return minimum / maximum of a printable object transformed into the world coordinate system.
    // All instances share the same min / max Z.
    double                  min_z() const;
    double                  max_z() const;

    void                    translate_instances(const Vec3d& vector);
    void                    translate_instance(size_t instance_idx, const Vec3d& vector);

    // Non-transformed (non-rotated, non-scaled, non-translated) sum of non-modifier object volumes.
    // Currently used by ModelObject::mesh() and to calculate the 2D envelope for 2D plater.
    TriangleMesh            raw_mesh() const;
    // The same as above, but producing a lightweight indexed_triangle_set.
    indexed_triangle_set    raw_indexed_triangle_set() const;

    size_t                  facets_count() const;
    size_t                  parts_count() const;

    // Invalidate cut state for this object and its connectors/volumes.
    void                    invalidate_cut();
    // Delete volumes which are marked as connector for this object.
    void                    delete_connectors();
    bool                    has_connectors() const;

    // Detect if the object has at least one solid mash
    bool                    has_solid_mesh() const;
    // Detect if the object has at least one negative volume mash
    bool                    has_negative_volume_mesh() const;
    // Detect if the object has at least one sla drain hole
    bool                    has_sla_drain_holes() const;
    bool                    is_cut() const;

    double                  get_instance_min_z(size_t instance_idx) const;
    double                  get_instance_max_z(size_t instance_idx) const;

    // This method could only be called before the meshes of this ModelVolumes are not shared!
    void                    scale_mesh_after_creation(float scale);

    void                    clone_for_cut(ModelObject** obj);

    void                    invalidate_bounding_box() const;

    ~ModelObject() override;

private:
    // Parent object, owning this ModelObject. Set to nullptr here, so the macros above will have it initialized.
    Model                  *m_model { nullptr };

    // Bounding box, cached.
    mutable BoundingBox3d   m_bounding_box_approx;
    mutable bool            m_bounding_box_approx_valid { false };
    mutable BoundingBox3d   m_bounding_box_exact;
    mutable bool            m_bounding_box_exact_valid { false };
    mutable bool            m_min_max_z_valid { false };
    mutable BoundingBox3d   m_raw_bounding_box;
    mutable bool            m_raw_bounding_box_valid { false };
    mutable BoundingBox3d   m_raw_mesh_bounding_box;
    mutable bool            m_raw_mesh_bounding_box_valid { false };

    // This constructor assigns new ID to this ModelObject and its config.
    explicit ModelObject(Model* model) : origin_translation(Vec3d::Zero()), m_model(model)
    {
        assert(this->id().valid());
        assert(this->layer_height_profile.id().valid());
    }

    explicit ModelObject(int) : ObjectBase(-1), layer_height_profile(-1), origin_translation(Vec3d::Zero())
    {
        assert(this->id().invalid());
        assert(this->layer_height_profile.id().invalid());
    }

    // To be able to return an object from own copy / clone methods. Hopefully the compiler will do the "Copy elision"
    // (Omits copy and move(since C++11) constructors, resulting in zero - copy pass - by - value semantics).
    ModelObject(const ModelObject& rhs) : ObjectBase(-1), layer_height_profile(-1), m_model(rhs.m_model)
    {
        assert(this->id().invalid());
        assert(this->layer_height_profile.id().invalid());
        assert(rhs.id() != rhs.layer_height_profile.id());
        this->assign_copy(rhs);
        assert(this->id().valid());
        assert(this->layer_height_profile.id().valid());
        assert(this->id() != this->layer_height_profile.id());
        assert(this->id() == rhs.id());
        assert(this->layer_height_profile.id() == rhs.layer_height_profile.id());
    }

    ModelObject(ModelObject&& rhs) noexcept : ObjectBase(-1), layer_height_profile(-1)
    {
        assert(this->id().invalid());
        assert(this->layer_height_profile.id().invalid());
        assert(rhs.id() != rhs.layer_height_profile.id());
        this->assign_copy(std::move(rhs));
        assert(this->id().valid());
        assert(this->layer_height_profile.id().valid());
        assert(this->id() != this->layer_height_profile.id());
        assert(this->id() == rhs.id());
        assert(this->layer_height_profile.id() == rhs.layer_height_profile.id());
    }

    // Used for deserialization -> Don't allocate any IDs for the ModelObject or its config.
    ModelObject() : ObjectBase(-1), layer_height_profile(-1)
    {
        assert(this->id().invalid());
        assert(this->layer_height_profile.id().invalid());
    }

    ModelObject& operator=(const ModelObject& rhs);

    ModelObject& operator=(ModelObject&& rhs) noexcept;

    void set_model(Model* model);

    // Called by min_z(), max_z()
    void update_min_max_z();

    void assign_new_unique_ids_recursive() override;

    void set_new_unique_id() override;

    // Only use this method if now the source and dest ModelObjects are equal, for example they were synchronized by Print::apply().
    void copy_transformation_caches(const ModelObject& src);

    friend class Model;

    friend Domain::ModelObject* Slic3r::Biz::Algorithms::Model::add_object(Domain::Model*, const char*, const char*, Domain::TriangleMesh&&);
    friend void Slic3r::Biz::Algorithms::ModelObject::translate(Domain::ModelObject&, double, double, double);
    friend const BoundingBox3d& Slic3r::Biz::Algorithms::ModelObject::bounding_box_approx(const Domain::ModelObject&);
    friend const BoundingBox3d& Slic3r::Biz::Algorithms::ModelObject::bounding_box_exact(const Domain::ModelObject&);
    friend const Domain::BoundingBox3d& Slic3r::Biz::Algorithms::ModelObject::raw_bounding_box(const Domain::ModelObject&);
    friend const Domain::BoundingBox3d& Slic3r::Biz::Algorithms::ModelObject::raw_mesh_bounding_box(const Domain::ModelObject&);

    template<class Archive> friend void cereal::save(Archive&, const Slic3r::Domain::ModelObject&);
    template<class Archive> friend void cereal::load(Archive&, Slic3r::Domain::ModelObject&);
    template<class Archive> friend void cereal::load(Archive& ar, Slic3r::Domain::Model& model);
};

using ModelObjectPtrs = std::vector<ModelObject*>;

} // namespace Slic3r::Domain
