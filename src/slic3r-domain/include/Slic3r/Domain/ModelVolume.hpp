#pragma once

#include "Slic3r/Domain/Forward.hpp"
#include "Slic3r/Domain/Axis.hpp"
#include "Slic3r/Domain/ConfigBoxesFDM.hpp"
#include "Slic3r/Domain/CutConnector.hpp"
#include "Slic3r/Domain/EmbossShape.hpp"
#include "Slic3r/Domain/FacetsAnnotation.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/TextConfiguration.hpp"
#include "Slic3r/Domain/Transformation.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/ITransformable.hpp"

#include <optional>

namespace Slic3r::Biz::Algorithms::ModelObject {
void bake_xy_rotation_into_meshes(Domain::ModelObject&, size_t);
Domain::ModelVolume* add_volume(Domain::ModelObject*, const Domain::TriangleMesh&);
Domain::ModelVolume* add_volume(Domain::ModelObject*, Domain::TriangleMesh&&, Domain::ModelVolumeType);
Domain::ModelVolume* add_volume(Domain::ModelObject*, const Domain::ModelVolume&, Domain::TriangleMesh&&);
} // namespace Slic3r::Biz::Algorithms::ModelObject

namespace Slic3r::Biz::Algorithms::ModelVolume {
Domain::ModelVolume* construct_ptr(Domain::ModelObject*, const Domain::TriangleMesh&, Domain::ModelVolumeType);
Domain::ModelVolume* construct_ptr(Domain::ModelObject*, Domain::TriangleMesh&&, Domain::ModelVolumeType);
Domain::ModelVolume* construct_ptr(Domain::ModelObject*, const Domain::ModelVolume&, Domain::TriangleMesh&&);
bool is_splittable(const Domain::ModelVolume&);
void calculate_convex_hull(Domain::ModelVolume&);
} // namespace Slic3r::Biz::Algorithms::ModelVolume

namespace cereal {
template<class Archive> void load(Archive&, Slic3r::Domain::ModelVolume &);
template<class Archive> void save(Archive&, const Slic3r::Domain::ModelVolume &);
template<class Archive> void load(Archive&, Slic3r::Domain::ModelObject &);
} // namespace cereal

namespace Slic3r::Domain {

// Declared outside of ModelVolume, so it could be forward declared.
enum class ModelVolumeType : int
{
    INVALID = -1,
    MODEL_PART = 0,
    NEGATIVE_VOLUME,
    PARAMETER_MODIFIER,
    SUPPORT_BLOCKER,
    SUPPORT_ENFORCER,
};

/**
 * An object STL, or a modifier volume, over which a different set of parameters shall be applied.
 * ModelVolume instances are owned by a ModelObject.
 */
class ModelVolume final : public ObjectBase, public ITransformable
{
public:
    std::string         name;
    // struct used by reload from disk command to recover data from disk
    struct Source
    {
        std::string input_file;
        int object_idx{ -1 };
        int volume_idx{ -1 };

        // PrusaSlicer 2.x used to center all loaded volume meshes around origin and kept this extra offset
        // so reload from disk can use it. This centering is now removed and volume meshes retain their original
        // coordinate system. We keep this variable for now, it is read from & saved into a 3MF, but currently
        // not used. We will either need to use it to implement reload from disk for legacy projects,
        // or decide that we want to drop it.
        Vec3d mesh_offset{ Vec3d::Zero() };

        Transformation transform;
        bool is_converted_from_inches{ false };
        bool is_converted_from_meters{ false };
        bool is_from_builtin_objects{ false };
    };
    Source              source;

    // struct used by cut command
    // It contains information about connetors
    struct CutInfo
    {
        bool                is_from_upper{ true };
        bool                is_connector{ false };
        bool                is_processed{ true };
        CutConnectorType connector_type{ CutConnectorType::Plug };
        double              radius_tolerance{ 0. };// [0. : 1.]
        double              height_tolerance{ 0. };// [0. : 1.]

        CutInfo() = default;
        CutInfo(CutConnectorType type, double rad_tolerance, double h_tolerance, bool processed = false) :
            is_connector(true),
            is_processed(processed),
            connector_type(type),
            radius_tolerance(rad_tolerance),
            height_tolerance(h_tolerance)
        {}

        void set_processed() { is_processed = true; }
        void invalidate()    { is_connector = false; }
        void reset_from_upper() { is_from_upper = true; }
    };
    CutInfo             cut_info;

    bool                is_from_upper() const    { return cut_info.is_from_upper; }
    void                reset_from_upper()       { cut_info.reset_from_upper(); }

    bool                is_cut_connector() const { return cut_info.is_processed && cut_info.is_connector; }
    void                invalidate_cut_info()    { cut_info.invalidate(); }

    // The triangular model.
    const TriangleMesh& mesh() const { return *m_mesh.get(); }
    std::shared_ptr<const TriangleMesh> mesh_ptr() const { return m_mesh; }
    void                set_mesh(const TriangleMesh &mesh) { m_mesh = std::make_shared<const TriangleMesh>(mesh); }
    void                set_mesh(TriangleMesh &&mesh) { m_mesh = std::make_shared<const TriangleMesh>(std::move(mesh)); }
    void                set_mesh(std::shared_ptr<const TriangleMesh> &mesh) { m_mesh = mesh; }
    void                set_mesh(std::unique_ptr<const TriangleMesh> &&mesh) { m_mesh = std::move(mesh); }

    // Configuration parameters specific to an object model geometry or a modifier volume,
    // overriding the global Slic3r settings and the ModelObject settings.
    VolumeSettings                      volume_settings;

    // List of mesh facets to be supported/unsupported.
    FacetsAnnotation                    supported_facets;

    // List of seam enforcers/blockers.
    FacetsAnnotation                    seam_facets;

    // List of mesh facets painted for MM segmentation.
    FacetsAnnotation                    mm_segmentation_facets;

    // List of mesh facets painted for fuzzy skin.
    FacetsAnnotation                    fuzzy_skin_facets;

    // Is set only when volume is Embossed Text type
    // Contain information how to re-create volume
    std::optional<TextConfiguration>    text_configuration;

    // Is set only when volume is Embossed Shape
    // Contain 2d information about embossed shape to be editable
    std::optional<EmbossShape>          emboss_shape;

    // A parent object owning this modifier volume.
    ModelObject*                        get_object() const;
    ModelVolumeType                     type() const;
    void                                set_type(ModelVolumeType t);
    bool                                is_model_part() const;
    bool                                is_negative_volume() const;
    bool                                is_modifier()  const;
    bool                                is_support_enforcer() const;
    bool                                is_support_blocker() const;
    bool                                is_support_modifier() const;
    bool                                is_text() const;
    bool                                is_svg() const ;
    bool                                is_the_only_one_part() const;
    bool                                is_painted() const;
    void                                reset_extra_facets();

    // Extract the current extruder ID based on this ModelVolume's config and the parent ModelObject's config.
    // Extruder ID is only valid for FFF. Returns -1 for SLA or if the extruder ID is not applicable (support volumes).
    int                                 extruder_id() const;

    void                                discard_splittable();

    // This method could only be called before the meshes of this ModelVolumes are not shared!
    void                                scale_geometry_after_creation(const Vec3f &versor);
    void                                scale_geometry_after_creation(float scale);

    const TriangleMesh&                 get_convex_hull() const;
    const std::shared_ptr<const TriangleMesh>& get_convex_hull_shared_ptr() const;

    const Transformation&               get_transformation() const override;
    void                                set_transformation(const Transformation& transformation) override;
    void                                set_transformation(const Transform3d& trafo);

    Vec3d                               get_offset() const override;

    double                              get_offset(Axis axis) const override;

    void                                set_offset(const Vec3d& offset) override;
    void                                set_offset(Axis axis, double offset) override;

    Vec3d                               get_rotation() const override;
    double                              get_rotation(Axis axis) const override;

    void                                set_rotation(const Vec3d& rotation) override;
    void                                set_rotation(Axis axis, double rotation) override;

    Vec3d                               get_scaling_factor() const override;
    double                              get_scaling_factor(Axis axis) const override;

    void                                set_scaling_factor(const Vec3d& scaling_factor) override;
    void                                set_scaling_factor(Axis axis, double scaling_factor) override;

    Vec3d                               get_mirror() const override;
    double                              get_mirror(Axis axis) const override;
    bool                                is_left_handed() const override;

    void                                set_mirror(const Vec3d& mirror) override;
    void                                set_mirror(Axis axis, double mirror) override;

    const Transform3d&                  get_matrix() const override;
    Transform3d                         get_matrix_no_offset() const override;

    void                                set_new_unique_id() override;

    bool                                is_fdm_support_painted() const;
    bool                                is_seam_painted() const;
    bool                                is_mm_painted() const;
    bool                                is_fuzzy_skin_painted() const;

    /**
     * @brief Returns true if every facet of the volume mesh is painted by the multi-material painting gizmo.
     */
    bool is_fully_mm_painted() const;

    // Returns 0-based indices of extruders painted by multi-material painting gizmo.
    std::vector<size_t>                 get_extruders_from_multi_material_painting() const;

    static size_t                       get_extruder_color_idx(const ModelVolume& model_volume, const int extruders_count);
    // Helpers for loading / storing into AMF / 3MF files.
    static ModelVolumeType              type_from_string(const std::string& s);
    static std::string                  type_to_string(const ModelVolumeType t);

    ModelVolume& operator=(ModelVolume &rhs) = delete;

protected:
    // Copies IDs of both the ModelVolume and its config.
    ModelVolume(const ModelVolume& rhs) = default;

    void                                set_model_object(ModelObject* model_object);
    void                                assign_new_unique_ids_recursive() override;
    void                                transform_this_mesh(const Transform3d& t, bool fix_left_handed);

private:
    // Parent object owning this ModelVolume.
    ModelObject*                        object;
    // The triangular model.
    std::shared_ptr<const TriangleMesh> m_mesh;
    // Is it an object to be printed, or a modifier volume?
    ModelVolumeType                     m_type;
    // The convex hull of this model's mesh.
    std::shared_ptr<const TriangleMesh> m_convex_hull;
    Transformation                      m_transformation;

    // flag to optimize the checking if the volume is splittable
    //     -1   ->   is unknown value (before first cheking)
    //      0   ->   is not splittable
    //      1   ->   is splittable
    mutable int                         m_is_splittable { -1 };

    bool                                check();

    // Called from Slic3r::Biz::Algorithms::ModelVolume::construct().
    // It doesn't initialize m_convex_hull.
    ModelVolume(ModelObject* object, const TriangleMesh& mesh, ModelVolumeType type = ModelVolumeType::MODEL_PART) : object(object), m_mesh(new TriangleMesh(mesh)), m_type(type)
    {
        assert(check());
    }

    // Called from Slic3r::Biz::Algorithms::ModelVolume::construct().
    // It doesn't initialize m_convex_hull.
    ModelVolume(ModelObject* object, TriangleMesh&& mesh, ModelVolumeType type = ModelVolumeType::MODEL_PART) : object(object), m_mesh(new TriangleMesh(std::move(mesh))), m_type(type)
    {
        assert(check());
    }

    ModelVolume(ModelObject* object, TriangleMesh&& mesh, TriangleMesh&& convex_hull, ModelVolumeType type = ModelVolumeType::MODEL_PART)
        : object(object), m_mesh(new TriangleMesh(std::move(mesh))), m_type(type), m_convex_hull(new TriangleMesh(std::move(convex_hull)))
    {
        assert(check());
    }

    // Copying an existing volume, therefore this volume will get a copy of the ID assigned.
    ModelVolume(ModelObject* object, const ModelVolume& other)
        : ObjectBase(other), name(other.name), source(other.source), cut_info(other.cut_info), volume_settings(other.volume_settings), supported_facets(other.supported_facets)
        , seam_facets(other.seam_facets), mm_segmentation_facets(other.mm_segmentation_facets), fuzzy_skin_facets(other.fuzzy_skin_facets), text_configuration(other.text_configuration), emboss_shape(other.emboss_shape)
        , object(object), m_mesh(other.m_mesh), m_type(other.m_type), m_convex_hull(other.m_convex_hull), m_transformation(other.m_transformation)
    {
        assert(this->id().valid());
        assert(this->supported_facets.id().valid());
        assert(this->seam_facets.id().valid());
        assert(this->mm_segmentation_facets.id().valid());
        assert(this->fuzzy_skin_facets.id().valid());
        assert(this->id() != this->supported_facets.id());
        assert(this->id() != this->seam_facets.id());
        assert(this->id() != this->mm_segmentation_facets.id());
        assert(this->id() == other.id());
        assert(this->supported_facets.id() == other.supported_facets.id());
        assert(this->seam_facets.id() == other.seam_facets.id());
        assert(this->mm_segmentation_facets.id() == other.mm_segmentation_facets.id());
        assert(this->fuzzy_skin_facets.id() == other.fuzzy_skin_facets.id());
    }

    // Providing a new mesh, therefore, this volume will get a new unique ID assigned.
    // Called from Slic3r::Biz::Algorithms::ModelVolume::construct().
    // It doesn't initialize m_convex_hull.
    ModelVolume(ModelObject* object, const ModelVolume& other, TriangleMesh&& mesh)
        : name(other.name), source(other.source), cut_info(other.cut_info), volume_settings(other.volume_settings), text_configuration(other.text_configuration), emboss_shape(other.emboss_shape)
        , object(object), m_mesh(new TriangleMesh(std::move(mesh))), m_type(other.m_type), m_transformation(other.m_transformation)
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
        assert(this->id() != other.id());
        assert(this->supported_facets.id() != other.supported_facets.id());
        assert(this->seam_facets.id() != other.seam_facets.id());
        assert(this->mm_segmentation_facets.id() != other.mm_segmentation_facets.id());
        assert(this->fuzzy_skin_facets.id() != other.fuzzy_skin_facets.id());
        assert(this->supported_facets.empty());
        assert(this->seam_facets.empty());
        assert(this->mm_segmentation_facets.empty());
        assert(this->fuzzy_skin_facets.empty());
    }

    // Used for deserialization, therefore no IDs are allocated.
    ModelVolume() : ObjectBase(-1), supported_facets(-1), seam_facets(-1), mm_segmentation_facets(-1), fuzzy_skin_facets(-1), object(nullptr)
    {
        assert(this->id().invalid());
        assert(this->supported_facets.id().invalid());
        assert(this->seam_facets.id().invalid());
        assert(this->mm_segmentation_facets.id().invalid());
        assert(this->fuzzy_skin_facets.id().invalid());
    }

    friend class Model;
    friend class ModelObject;

    friend void Slic3r::Biz::Algorithms::ModelObject::bake_xy_rotation_into_meshes(Domain::ModelObject&, size_t);
    friend bool Slic3r::Biz::Algorithms::ModelVolume::is_splittable(const Domain::ModelVolume&);
    friend void Slic3r::Biz::Algorithms::ModelVolume::calculate_convex_hull(Domain::ModelVolume&);
    friend Domain::ModelVolume* Slic3r::Biz::Algorithms::ModelVolume::construct_ptr(Domain::ModelObject*, const Domain::TriangleMesh&, Domain::ModelVolumeType);
    friend Domain::ModelVolume* Slic3r::Biz::Algorithms::ModelVolume::construct_ptr(Domain::ModelObject*, Domain::TriangleMesh&&, Domain::ModelVolumeType);
    friend Domain::ModelVolume* Slic3r::Biz::Algorithms::ModelVolume::construct_ptr(Domain::ModelObject*, const Domain::ModelVolume&, Domain::TriangleMesh&&);

    template<class Archive> friend void cereal::load(Archive&, Slic3r::Domain::ModelVolume &);
    template<class Archive> friend void cereal::save(Archive&, const Slic3r::Domain::ModelVolume &);
    template<class Archive> friend void cereal::load(Archive&, Slic3r::Domain::ModelObject &);
};

using ModelVolumePtrs = std::vector<ModelVolume*>;

} // namespace Slic3r::Domain
