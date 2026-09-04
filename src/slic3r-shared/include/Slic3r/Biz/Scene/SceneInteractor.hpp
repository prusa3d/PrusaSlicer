#pragma once

#include <functional>
#include <optional>
#include <unordered_map>

#include "Slic3r/Biz/ISlicingInputChangedListener.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Domain/Workbench.hpp"
#include "Slic3r/Biz/Scene/SceneInteractorProjectContext.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Biz/Scene/BedPlacement.hpp"
#include "Slic3r/Biz/Platform/ListenerList.hpp"
#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/BedRef.hpp"
#include "Slic3r/Domain/FacetsAnnotation.hpp"
#include "Slic3r/Biz/Preset/IPresetVisualGetter.hpp"

#include "Slic3r/Biz/Scene/BedTracking.hpp"
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"
#include "Slic3r/Biz/IUndoProvider.hpp"

namespace Slic3r { class ObjectModel; }
namespace Slic3r::Domain { class Bed; class ObjectID;}

namespace Slic3r::Biz {
class ISelectedBedInstancesChangedListener;
struct BedTrackingChanges;
namespace Arrange {struct InstanceTransform2D; }
} // namespace Slic3r::Biz

namespace Slic3r::Biz::Scene {

class ISceneSelectionChangedListener
{
public:
    virtual ~ISceneSelectionChangedListener() = default;
    virtual void on_scene_selection_changed(Domain::SelectionId project_id, const ObjectSelection& selection) = 0;
    virtual void on_scene_selection_transformed(Domain::SelectionId project_id, const ObjectSelection& selection) {};
    virtual void on_scene_selection_bounding_box_updated(Domain::SelectionId project_id, const ObjectSelection& selection) {};
};

enum class TransformState
{
    InProgress, /* Transform is in progress (i.e. user is dragging the mouse) */
    Completed,  /* Transform was completed (i.e. user released the mouse button) */
    Canceled    /* Transform was canceled (i.e. user pressed ESC key) */
};

class ISceneChangedListener
{
public:
    virtual ~ISceneChangedListener() = default;

    /**
     * @brief Called whenever new instances are added.
     * @remark The callee is responsible to create all volume representations for given instances.
     * @param project_id Project the new instances belong to
     * @param instances List of instances to add
     */
    virtual void
    on_instance_added(Domain::SelectionId project_id, const Domain::ElementRefs& instances)
    {}

    /**
     * @brief Called whenever instances are removed.
     * @param project_id Project the instances belong to
     * @param instances List of instances to remove
     */
    virtual void
    on_instance_removed(Domain::SelectionId project_id, const Domain::ElementRefs& instances)
    {}

    /**
     * @brief Called whenever instances are transformed, or its printable state changes.
     * @param project_id Project the instances belong to
     * @param elements List of instances to transform
     * @param state Indicates whether the transform state is final (interactive incremental transform)
     * @param bed_tracking_changes Information on changed bed instances
     */
    virtual void on_instance_transformed(
        Domain::SelectionId project_id,
        const Domain::ElementRefs& elements,
        TransformState state,
        const BedTrackingChanges& bed_tracking_changes
    )
    {}

    /**
     * @brief Called whenever volumes are added
     * @remark The callee is responsible to create volume representations of @p volumes for all existing instances.
     * @param project_id Project the volumes belong to
     * @param volumes Set of volumes to add.
     */
    virtual void on_volume_added(Domain::SelectionId project_id, const Domain::ElementRefs& volumes)
    {}

    /**
     * @brief Called whenever volumes are removed
     * @remark The callee is responsible to remove volume representations of @p volumes from all existing instances.
     * @param project_id Project the volumes belong to
     * @param volumes Set of volumes to add.
     */
    virtual void
    on_volume_removed(Domain::SelectionId project_id, const Domain::ElementRefs& volumes)
    {}

    /**
     * @brief Called whenever volumes are tranformed.
     * @param project_id Project the instances belong to
     * @param elements List of instances to transform
     * @param state Indicates whether the transform state is final (interactive incremental transform)
     * @param bed_tracking_changes Information on changed bed instances
     */
    virtual void on_volume_transformed(
        Domain::SelectionId project_id,
        const Domain::ElementRefs& elements,
        TransformState state,
        const BedTrackingChanges& bed_tracking_changes
    )
    {}

    /**
     * @brief Called whenever volumes type is changed.
     * @param project_id Project the instances belong to
     * @param volumes List of volumes with changed type
     */
    virtual void on_volume_type_changed(
        Domain::SelectionId project_id,
        const Domain::ElementRefs& volumes
    )
    {}

    /**
     * @brief Called whenever volumes mesh is changed.
     * @param project_id Project the instances belong to
     * @param volumes List of volumes with changed mesh
     */
    virtual void on_volume_mesh_changed(
        Domain::SelectionId project_id,
        const Domain::ElementRefs& volumes
    )
    {}

    virtual void on_wipe_tower_changed(Domain::SlicingId slicing_id, const Slicing::WipeTowerGeometry& wipe_tower) {}
    virtual void on_wipe_tower_moved(Domain::SlicingId slicing_id) {}
    virtual void on_wipe_tower_removed(Domain::SlicingId slicing_id) {}

    virtual void on_instances_last_bed_updated(const Domain::ElementRefs& updated_instances) {}

    virtual void on_model_reloaded(Domain::SelectionId project_id) {}

    /**
     * @brief Called whenever a volume's facets annotation (Multi-Material / Supports / Seam / Fuzzy skin)
     *        changes. Listeners interested only in a specific annotation should filter by kind.
     * @param project_id Project the volumes belong to
     * @param kind Which facets annotation of the volumes changed
     * @param volumes List of volumes whose annotation of the given kind changed
     */
    virtual void on_volume_facets_annotations_changed(
        Domain::SelectionId project_id,
        Domain::FacetsAnnotationKind kind,
        const Domain::ElementRefs& volumes
    )
    {}
};

class ISceneBedInstanceChangedListener
{
public:
    virtual ~ISceneBedInstanceChangedListener() = default;

    virtual void on_bed_instance_updated(Domain::SelectionId project_id, const Domain::BedRefs& instances) {};
    virtual void on_bed_instance_removed(Domain::SelectionId project_id, const Domain::BedRefs& instances) {};
    virtual void on_bed_instance_transformed(Domain::SelectionId project_id, const Domain::BedRefs& instances, TransformState state) {};
    virtual void on_bed_instance_extruder_candidates_changed(
        Domain::SelectionId project_id,
        Domain::BedRef instance,
        const std::vector<unsigned>& extruder_candidates
    ) {};

    /**
     * @brief Called when the virtual bed preview for a project is shown, moved, or hidden.
     * @param preview std::nullopt means the preview should be hidden.
     */
    virtual void on_virtual_bed_preview_changed(
        Domain::SelectionId project_id,
        const std::optional<VirtualBedPreview>& preview
    ) {};
};

struct TransformMemento;

class SceneInteractor final :
    public ISelectedProjectChangedListener,
    public ISelectedConfigContainerChangedListener,
    public Preset::IPresetChangedListener,
    public Slicing::IWipeTowerGeometryListener,
    public Slicing::IExtruderCandidatesListener,
    public WithListeners<
        ISceneSelectionChangedListener,
        ISceneChangedListener,
        ISceneBedInstanceChangedListener,
        ISelectedBedInstancesChangedListener,
        ISlicingInputChangedListener
    >
{
public:
    using Transform = Domain::SquareMatrix4d;

    explicit SceneInteractor(Domain::Workbench& workbench);

    void set_preset_visual_getter(Preset::IPresetVisualGetter* preset_visual_getter)
    {
        m_preset_visual_getter = preset_visual_getter;
    }

    void on_selected_project_changed(size_t index) override;
    void on_selected_config_container_changed(Domain::SelectionId project_id, Domain::SelectionId container_id) override;

    Domain::ElementRefs add_new_objects(const std::vector<Domain::ModelObject*>& objects);
    void add_volume_from_mesh(
        Domain::TriangleMesh&& mesh,
        Domain::ModelVolumeType volume_type,
        const std::string& name = std::string(),
        const Transform& xform  = Domain::SquareMatrix4d::Identity(),
        const std::optional<Domain::ModelVolume::Source>& volume_source = std::nullopt
    );
    void add_volume_into_selected_object(const Domain::ModelVolume& volume);
    using VolumeFactory = std::function<Domain::ModelVolume*(Domain::ModelObject&)>;
    void add_volume(
        Domain::SelectionId project_id,
        Domain::SelectionId instance_id,
        const VolumeFactory& factory
    );
    void set_selected_volume_type(
        Domain::ModelVolumeType volume_type
    );

    using UpdateObjectFn = std::function<void(Domain::ModelObject&)>;
    Domain::ElementRefs new_object_from_mesh(
        Domain::TriangleMesh&& mesh,
        const std::string& name                                         = std::string(),
        const std::optional<Domain::ModelVolume::Source>& volume_source = std::nullopt
    );
    Domain::ElementRefs new_object_from_mesh(
        Domain::TriangleMesh&& mesh,
        Domain::SelectionId project_id,
        UpdateObjectFn update_object
    );

    Domain::ElementRefs add_instance(const Domain::Vec2d& offset);
    void delete_selected_object_last_instance();
    Domain::ElementRefs set_selected_objects_instance_count(int count);

    Domain::ModelObjectPtrs clone_objects_from_project(
        Domain::SelectionId source_project_id,
        const std::vector<Domain::ElementRef>& source_elements
    );

    using RefMesh   = std::pair<Domain::ElementRef, Domain::TriangleMesh>;
    using RefMeshes = std::vector<RefMesh>;

    struct VolumeMeshReplacement
    {
        Domain::ElementRef element;
        Domain::TriangleMesh mesh;
        std::optional<Domain::ModelVolume::Source> new_source;
        std::optional<Domain::Transform3d> new_transformation;
        std::optional<std::string> new_name;
    };

    using VolumeMeshReplacements = std::vector<VolumeMeshReplacement>;

    void change_volume_meshes(RefMeshes&& meshes);
    Domain::ElementRefs change_volume_meshes(VolumeMeshReplacements&& replacements);

#if SLIC3R_ENABLE_WIN10_MESH_REPAIR
    bool can_repair_selected_object() const;
    void repair_selected_object();
#endif

    /**
     * @brief Modify facets annotations for given volumes.
     * @param volume_refs List of volumes to modify.
     * @param kind Which facets annotation of the volumes the modifier modifies.
     * @param modifier Called for each volume to perform modification of facets annotations.
     */
    void modify_facets_annotations(
        const Domain::ElementRefs& volume_refs,
        const Domain::FacetsAnnotationKind kind,
        const std::function<bool(const Domain::ElementRef&, Domain::ModelVolume&)>& modifier
    );

    /**
     * @brief Modify the layer height profile for the given object.
     * @param object_ref Reference to the object to modify (only object_id is used).
     * @param modifier Called with the ModelObject to perform modification of the layer height profile.
     */
    void modify_layer_height_profile(
        const Domain::ElementRef& object_ref,
        const std::function<void(Domain::ModelObject&)>& modifier
    );

    /**
     * @brief Modify the layer config ranges for the given object.
     * @param object_ref Reference to the object to modify (only object_id is used).
     * @param modifier Called with the ModelObject to perform modification of the layer config ranges.
     */
    void modify_layer_config_ranges(
        const Domain::ElementRef& object_ref,
        const std::function<void(Domain::ModelObject&)>& modifier
    );

    void edit_name(const Domain::ElementRef& id, const std::string& new_name);
    void set_printable(const Domain::ElementRef& id, bool is_printable);
    void set_selected_instances_printable(bool is_printable);
    bool selected_instances_printable() const;
    void extract_selected_instances();
    bool can_extract_selected_instances() const;

    bool can_split_selection_to_objects() const;
    void split_selection_to_objects();
    bool can_split_selection_to_volumes() const;
    void split_selection_to_volumes();
    bool can_merge_selection_into_object() const;
    void merge_selection_into_object();
    bool can_invalidate_cut_info() const;
    void invalidate_cut_info();

    /**
     * @brief Whether the current selection can be exported as a single mesh file.
     *
     * The selection must contain exactly one object and no wipe tower. In Volume mode
     * exactly one volume of any type must be selected. In Instance mode either one fully
     * selected instance or all instances of the object must be selected.
     */
    bool can_export_selection_as_mesh() const;

    /**
     * @brief Whether the mesh of the selected volume can be replaced from a file.
     */
    bool can_replace_selected_volume() const;

    /**
     * @brief Whether the current selection has anything to reload from disk.
     */
    bool can_reload_selection_from_disk() const;

    /**
     * Delete elements (volumes or instances) from the current scene selection.
     *
     * @return An optional string containing the name of the last solid part that was attempted to be deleted.
     *         Deleting the last solid part is not permitted, and the user will be informed about it later.
     */
    std::optional<std::string> delete_selected_elements();

    /**
     * Delete object from the scene and model.
     */
    bool delete_object(Domain::ModelObject* object);

    void prepare_added_project(Domain::SelectionId project_id);

    Domain::BedInstance& add_bed_instance(size_t config_container_id);

    /**
     * @name Virtual bed preview
     *
     * A purely visual preview of a bed that would be appended if the user commits
     * (e.g. by dropping a dragged object on it). Never part of domain/undo state.
     * @{
     */
    /**
     * @brief Show a virtual bed preview in the currently selected project at the position
     *        where the next real bed would be placed in @p config_container_id.
     *        No-op (and the preview stays hidden) if the position cannot be determined.
     */
    void show_virtual_bed_preview(Domain::SelectionId config_container_id);

    /**
     * @brief Hide the virtual bed preview in the currently selected project, if any.
     */
    void hide_virtual_bed_preview();

    /**
     * @brief Access the virtual bed preview of the currently selected project.
     */
    const std::optional<VirtualBedPreview>& virtual_bed_preview() const;

    /**
     * @brief Check whether any currently-unplaced instance of the scene selection
     *        would be fully contained in the bed that the virtual preview stands for.
     *
     * Uses the shared BedTracking containment logic (no duplication).
     * @return false if no virtual preview is shown or if no unplaced selected
     *         instance would land inside the hypothetical new bed.
     */
    bool virtual_bed_preview_accepts_selection();
    /** @} */

    void insert_bed_instance(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        std::size_t position,
        std::unique_ptr<Domain::BedInstance> bed_instance
    );
    void erase_bed_instance(Domain::SelectionId project_id, const Domain::BedRef& instance);
    void remove_bed_instance(const Domain::BedRef& instance, bool allow_to_remove_last_one = false);
    void transform_bed_instance(const Domain::BedRef& instance, const Transform& xform);

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Preset::PresetItemType type
    ) override;

    void on_preset_value_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const Domain::ConfigItem& item
    ) override;

    const Domain::Project& selected_project() const;
    const Domain::Project::ConfigContainerList& selected_project_config_containers() const;
    const Domain::ModelInstanceList& unplaced_model_instances(const Domain::SelectionId project_id) const;
    const Domain::ModelInstanceList& selected_project_unplaced_model_instances() const;
    const Domain::BedContainer::BedList& selected_project_beds() const;


    Domain::SelectionId selected_config_container_id() const
    { return m_selected_config_container_id; }

    /**
     * @name Scene selection
     * @{
     */
    const ObjectSelection& object_selection() const;
    const ObjectSelection& object_selection(Domain::SelectionId project_id) const;
    void set_object_selection(const ObjectSelection& object_selection);
    void set_object_selection(const ObjectSelection& object_selection, Domain::SelectionId project_id);
    /*
    * Return an equivalent selection but for Volume mode from single instance selection
    */
    Domain::ElementRefs selected_instance_all_volumes() const;

    Domain::ElementRefs selected_volumes_with_shear() const;

    /**
     * @brief Volumes of the current selection, each of them once.
     */
    Domain::ElementRefs selected_volumes() const;

    std::set<SelectionReferenceFrame> object_selection_reference_frame_options() const;
    SelectionReferenceFrame object_selection_reference_frame() const;
    bool reload_object_selection_reference_frame(SelectionReferenceFrame preferred_frame);
    void clear_object_selection();

    void modify_selection(const std::function<void(ObjectSelection&)>& modifier);

    /** @} */

    const BedSelection& bed_selection() const;
    BedSelection& bed_selection();
    const BedSelection* bed_selection(const Domain::SelectionId project_id) const;
    BedSelection* bed_selection(const Domain::SelectionId project_id);

    using ElementTransforms = std::map<Domain::ElementRef, Domain::SquareMatrix4d>;
    void set_element_transforms(const ElementTransforms& transforms);

    /**
     * @name Transforming selection
     * @{
     */
    /**
     * @breif Update transform of all selected volumes or instances in a interactive way.
     *
     * This method will start or continue (depending on the @p memento object state) transforming
     * selected volumes or instances (depending on selection mode).
     *
     * @param relative_transform Relative transformation (w.r.t. object transformation at the start
     * of transform change) to be applied to all objects in selection. Note that the transform is
     * interpreted always as in WORLD COORDINATES. This means that if you want to rotate around
     * certain `O`, the transform passed here is computed as `T(O) * R * T(-O)`, where `T(x)` is
     * translation by `x`, and `R` is the rotation to be applied. If you pass just `R` here, it will
     * be interpreted as rotation around world origin. Same applies for scaling.
     * @note volume_tr = instance.inverse() * relative * instance * volume_tr
     * @param memento Maintains state of the transformation (i.e. original transformation at time of
     * transform change start).
     *
     * @note It is required to call finalize_transform_selection() to finish the operation.
     */
    void transform_selection(
        const Transform& relative_transform,
        TransformMemento& memento,
        bool place_on_bed = false
    );

    /**
     * @brief Update selection transform in one shot (not interactive way).
     * @param relative_transform Relative transformation (w.r.t. object transformation at the start
     * of transform change) to be applied to all objects in selection. Note that the transform is
     * interpreted always as in WORLD COORDINATES. This means that if you want to rotate around
     * certain `O`, the transform passed here is computed as `T(O) * R * T(-O)`, where `T(x)` is
     * translation by `x`, and `R` is the rotation to be applied. If you pass just `R` here, it will
     * be interpreted as rotation around world origin. Same applies for scaling.
     *
     * @note This effectively same as calling transform_selection(const Transform&, TransformMemento&)
     * and then finalize_transform_selection()
     */
    void transform_selection(const Transform& relative_transform, bool place_on_bed = false);

    void transform_instances(const std::vector<Arrange::InstanceTransform2D>& transformations);

    /**
     * @brief Finalize or cancel selection transform.
     * @param memento State of the transform in progress.
     * @param canceled Indicates if the transform in progress was canceled or finished.
     */
    void finalize_transform_selection(TransformMemento& memento, bool canceled);
    /** @} */

    void on_removed_config_container(Domain::Project& project);

    void on_wipe_tower_geometry_changed(
        Slicing::OptWipeTowerGeometry wipe_tower,
        const Domain::SlicingId slicing_id
    ) override;

    void remove_wipe_tower(const Domain::SlicingId slicing_id);
    void change_wipe_tower(
        const Slicing::WipeTowerGeometry& wipe_tower,
        const Domain::SlicingId slicing_id
    );

    void resolve_wipe_tower_outside_bed(
        const Slicing::WipeTowerGeometry& wipe_tower,
        Domain::SlicingId slicing_id
    );

    const Slicing::WipeTowerGeometry* wipe_tower_geometry(std::size_t bed_instance_id) const;

    void update_custom_gcode(
        const Domain::SlicingId slicing_id,
        const Domain::CustomGCode::Info& custom_gcode
    );

    void update_selection_bounding_box(const std::optional<SelectionExtents>& bounding_box = std::nullopt);
    const std::optional<SelectionExtents> selection_bounding_box() const;

    void on_extruder_candidates_changed(std::vector<unsigned>, const Domain::SlicingId) override;

    Domain::ElementRefs
    add_object_to_active_bed(const indexed_triangle_set& its, const std::string& name);
    void add_volume_to_active_object(
        const indexed_triangle_set& its,
        Domain::ModelVolumeType volume_type,
        const std::string& name
    );

    void set_state(
        Domain::SelectionId project_id,
        Domain::Model model,
        ObjectSelection object_selection
    );

    void clear_beds(Domain::Project& project);
    void update_beds(Domain::SelectionId project_id, Domain::SelectionId config_container_id);

    void set_undo_provider(IUndoProvider* undo_provider)
    {
        m_undo_provider = undo_provider;
    }

    IUndoProvider& undo_provider() const
    {
        ASSERT(m_undo_provider);
        return *m_undo_provider;
    };

    BedInstances selected_bed_instances() const;

private:
    void layout_after_project_load(Domain::Project& added_project);
    void notify_listener_on_objects(const std::vector<Domain::ModelObject*>& objects);
    void notify_listener_on_objects(const Domain::Project& project);

    BedTrackingChanges update_elements_bed_placement(
        const Domain::ElementRefs& elements,
        bool volume_mode,
        bool postpone_slicing_invalidation = false
    );
    void invoke_slicing_input_changed(const Domain::BedRef& bed_instance);
    void update_config_container_bed(Domain::SelectionId project_id, Domain::SelectionId config_container_id);
    void normalize_object_selection(ObjectSelection& object_selection) const;

    /**
     * Update BedInstance::wipe_tower_is_outside for wipe towers in `elements`.
     * BedTracking skips wipe tower elements (they are not ModelInstance*),
     * so we handle them here where we have access to the wipe tower geometry.
     *
     * @param project_context  Project context providing wipe tower geometries.
     * @param elements         Elements being moved (non-wipe-tower entries are skipped).
     * @param changes          Tracking changes to signal material refresh when containment state changes.
     */
    void update_wipe_tower_containment(
        SceneInteractorProjectContext& project_context,
        const Domain::ElementRefs& elements,
        BedTrackingChanges& changes
    );

    // Collapse the volume transform into the instance transforms, if the object constains only one volume.
    // No change is made otherwise.
    void normalize_single_volume_object(Domain::ModelObject& object);

    using ProjectContexts = std::unordered_map<Domain::SelectionId, SceneInteractorProjectContext>;

    Domain::Workbench& m_workbench;
    Preset::IPresetVisualGetter* m_preset_visual_getter{ nullptr };

    ProjectContexts m_projects;
    Domain::SelectionId m_selected_project_id {Domain::INVALID_ID};
    Domain::SelectionId m_selected_config_container_id {Domain::INVALID_ID};
    BedPlacement m_bed_placement;
    BedTracking m_bed_tracking;
    IUndoProvider *m_undo_provider{nullptr};
};

struct TransformMemento
{
    struct Element
    {
        Domain::ElementRef element;
        SceneInteractor::Transform original_xform;
    };
    using Elements = std::unordered_map<Domain::ElementRef, Element>;

    Elements elements;
    bool forced_volume_mode {false};
    BedTrackingChanges changes;

    void reset()
    {
        elements.clear();
        forced_volume_mode = false;
        changes = {};
    }
};


} // namespace Slic3r::Biz::Interactor
