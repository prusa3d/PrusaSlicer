#ifndef slic3r_Print_hpp_
#define slic3r_Print_hpp_

#include <functional>
#include <optional>
#include <set>
#include <span>
#include <Eigen/Geometry>

#include "Slic3r/Biz/Algorithms/Scaling.hpp"

#include "Slic3r/Biz/libpgcode/ProcessorResult.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/ConfigCommon.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/VirtualExtruder.hpp"

#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Fill/FillAdaptive.hpp"
#include "libslic3r/Fill/FillLightning.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/GCode/ToolOrdering.hpp"
#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/GeneratedSupportPoints.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintSteps.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/SupportSpotsGenerator.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"

namespace Slic3r {

class GCodeGenerator;
class Layer;
class Print;
class PrintObject;
class SupportLayer;

namespace Biz::Slicing {
class PrePreview;
} // namespace Biz::Print

namespace FillAdaptive {
    struct Octree;
    struct OctreeDeleter;
    using OctreePtr = std::unique_ptr<Octree, OctreeDeleter>;
}; // namespace FillAdaptive

namespace FillLightning {
    class Generator;
    struct GeneratorDeleter;
    using GeneratorPtr = std::unique_ptr<Generator, GeneratorDeleter>;
}; // namespace FillLightning

namespace SlicingSync {
struct AllSteps
{};

template<typename T>
using AllOrSome = std::variant<T, AllSteps>;
using StepsPerPrintObject = std::map<PrintObject*, AllOrSome<FDMPrintObjectSteps>>;
using PrintAndObjectSteps = std::pair<AllOrSome<FDMPrintSteps>, AllOrSome<FDMPrintObjectSteps>>;

struct InvalidatedSteps
{
    AllOrSome<FDMPrintSteps> print;
    StepsPerPrintObject object;

    bool empty() const;
};
} // namespace SlicingSync

// A PrintRegion object represents a group of volumes to print
// sharing the same config (including the same assigned extruder(s))
class PrintRegion
{
public:
    PrintRegion(const PrintRegionConfigView& config, const std::size_t hash, int print_object_region_id = -1)
        : m_config(config)
        , m_config_hash(hash)
        , m_print_object_region_id(print_object_region_id)
    {}
    PrintRegion(PrintRegionConfigView&& config, const std::size_t hash, int print_object_region_id = -1)
        : m_config(std::move(config))
        , m_config_hash(hash)
        , m_print_object_region_id(print_object_region_id)
    {}
    ~PrintRegion() = default;

// Methods NOT modifying the PrintRegion's state:
public:
    const PrintRegionConfigView& config() const throw() { return m_config; }
    void finalize_config() { m_config.finalize(); };
    size_t                      config_hash() const throw() { return m_config_hash; }
    // Identifier of this PrintRegion in the list of Print::m_print_regions.
    int                         print_region_id() const throw() { return m_print_region_id; }
    int                         print_object_region_id() const throw() { return m_print_object_region_id; }
    std::optional<unsigned int> source_virtual_extruder_id() const throw() { return m_source_virtual_extruder_id; }
	// 1-based extruder identifier for this region and role.
	unsigned int 				extruder(FlowRole role) const;
    Flow                        flow(const PrintObject &object, FlowRole role, double layer_height, bool first_layer = false) const;
    // Average diameter of nozzles participating on extruding this region.
    double                    nozzle_dmr_avg(const PrintConfigView &print_config) const;
    // Average diameter of nozzles participating on extruding this region.
    double                    bridging_height_avg(const PrintConfigView &print_config) const;

    // Collect 0-based extruder indices used to print this region's object.
	void                        collect_object_printing_extruders(const Print &print, std::vector<unsigned int> &object_extruders) const;
    static void collect_object_printing_extruders(
        const PrintRegionConfigView& config,
        const bool has_brim,
        std::vector<unsigned int>& object_extruders,
        const Domain::VirtualExtruders& virtual_extruders = {}
    );

    void set_print_region_id(const int id) {m_print_region_id = id;}
    void set_source_virtual_extruder_id(std::optional<unsigned int> id) throw() { m_source_virtual_extruder_id = id; }

    template <typename T>
    T extruder_config_value(const std::string& key, FlowRole role) const {
        return m_config.get<std::vector<T>>(key).at(extruder(role) - 1);
    }

    double nozzle_diameter(FlowRole role) const;

private:
    friend Print;
    friend void print_region_ref_inc(PrintRegion&);
    friend void print_region_ref_reset(PrintRegion&);
    friend int  print_region_ref_cnt(const PrintRegion&);

    PrintRegionConfigView  m_config;
    size_t             m_config_hash;
    int                m_print_region_id { -1 };
    int                m_print_object_region_id { -1 };
    int                m_ref_cnt { 0 };
    std::optional<unsigned int> m_source_virtual_extruder_id;
};

inline bool operator==(const PrintRegion &lhs, const PrintRegion &rhs)
{
    return lhs.config() == rhs.config()
        && lhs.source_virtual_extruder_id() == rhs.source_virtual_extruder_id();
}
inline bool operator!=(const PrintRegion &lhs, const PrintRegion &rhs) { return ! (lhs == rhs); }

// For const correctness: Wrapping a vector of non-const pointers as a span of const pointers.
template<class T>
using SpanOfConstPtrs           = std::span<const T* const>;

using LayerPtrs                 = std::vector<Layer*>;
using SupportLayerPtrs          = std::vector<SupportLayer*>;

// Single instance of a PrintObject.
// As multiple PrintObjects may be generated for a single ModelObject (their instances differ in rotation around Z),
// ModelObject's instances will be distributed among these multiple PrintObjects.
class PrintInstance
{
public:
    PrintInstance() = delete;

    PrintInstance(
        const Domain::ModelInstance& model_instance,
        std::size_t model_instance_index,
        const Domain::Vec2big& shift
    );

    /**
     * Returns the shift as Point (coord_t).
     * @note The shift is stored internally as Vec2big (int64) to handle meshes with coordinates beyond coord_t range.
     */
    Domain::Point shift() const;

    // Parent PrintObject.
    PrintObject* print_object = nullptr;
    // Source ModelInstance of a ModelObject, for which this print_object was created.
    Domain::ModelInstance model_instance;
    std::size_t model_instance_index;

private:
    // Shift of this instance's center into the world coordinates (scaled, int64).
    // Stored as Vec2big to handle meshes with coordinates beyond coord_t range.
    Domain::Vec2big m_shift;

    friend class PrintObject;
};

using PrintInstances = std::vector<PrintInstance>;

class PrintObjectRegions
{
public:
    // Bounding box of a ModelVolume transformed into the working space of a PrintObject, possibly
    // clipped by a layer range modifier.
    // Only Eigen types of Nx16 size are vectorized. This bounding box will not be vectorized.
    static_assert(sizeof(Eigen::AlignedBox<float, 3>) == 24, "Eigen::AlignedBox<float, 3> is not being vectorized, thus it does not need to be aligned");
    using BoundingBox = Eigen::AlignedBox<float, 3>;
    struct VolumeExtents {
        Domain::ObjectID     volume_id;
        BoundingBox          bbox;
    };

    struct VolumeRegion
    {
        // ID of the associated ModelVolume.
        const Domain::ModelVolume *model_volume { nullptr };
        // Index of a parent VolumeRegion.
        int                  parent { -1 };
        // Pointer to PrintObjectRegions::all_regions, null for a negative volume.
        PrintRegion         *region { nullptr };
        // Pointer to VolumeExtents::bbox.
        const BoundingBox   *bbox { nullptr };
        // To speed up merging of same regions.
        const VolumeRegion  *prev_same_region { nullptr };
    };

    struct PaintedRegion
    {
        // 1-based extruder identifier.
        unsigned int     extruder_id;
        // Index of a parent VolumeRegion.
        int              parent { -1 };
        // Pointer to PrintObjectRegions::all_regions.
        PrintRegion     *region { nullptr };
    };

    struct LayerRangeRegions;

    struct FuzzySkinPaintedRegion
    {
        enum class ParentType { VolumeRegion, PaintedRegion };

        ParentType   parent_type { ParentType::VolumeRegion };
        // Index of a parent VolumeRegion or PaintedRegion.
        int          parent { -1 };
        // Pointer to PrintObjectRegions::all_regions.
        PrintRegion *region { nullptr };

        PrintRegion *parent_print_object_region(const LayerRangeRegions &layer_range) const;
        int          parent_print_object_region_id(const LayerRangeRegions &layer_range) const;
    };

    // One slice over the PrintObject (possibly the whole PrintObject) and a list of ModelVolumes and their bounding boxes
    // possibly clipped by the layer_height_range.
    struct LayerRangeRegions
    {
        Domain::LayerHeightRange            layer_height_range;
        // Config of the layer range, null if there is just a single range with no config override.
        // Config is owned by the associated ModelObject.
        Domain::PartialVolumeConfigFDMPtr   config;
        // Volumes sorted by ModelVolume::id().
        std::vector<VolumeExtents>          volumes;

        // Sorted in the order of their source ModelVolumes, thus reflecting the order of region clipping, modifier overrides etc.
        std::vector<VolumeRegion>           volume_regions;
        std::vector<PaintedRegion>          painted_regions;
        std::vector<FuzzySkinPaintedRegion> fuzzy_skin_painted_regions;

        bool has_volume(const Domain::ObjectID id) const {
            const auto it = std::ranges::lower_bound(this->volumes, id, {}, [](const VolumeExtents& l) {
                return l.volume_id;
            });
            return it != this->volumes.end() && it->volume_id == id;
        }
    };

    struct GeneratedSupportPoints{
        Domain::Transform3d object_transform; // for frontend object mapping
        SupportSpotsGenerator::SupportPoints support_points;
        SupportSpotsGenerator::PartialObjects partial_objects;
    };

    std::vector<std::unique_ptr<PrintRegion>>   all_regions;
    std::vector<LayerRangeRegions>              layer_ranges;
    // Transformation of this ModelObject into one of the associated PrintObjects (all PrintObjects derived from a single modelObject differ by a Z rotation only).
    // This transformation is used to calculate VolumeExtents.
    Domain::Transform3d                         trafo_bboxes;
    std::vector<Domain::ObjectID>               cached_volume_ids;

    std::optional<GeneratedSupportPoints> generated_support_points;

    void clear() {
        all_regions.clear();
        layer_ranges.clear();
        cached_volume_ids.clear();
    }

private:
    friend class PrintObject;
};

class PrintObject : public PrintObjectBaseWithState<Print, FDMPrintObjectStep, posCount>
{
private: // Prevents erroneous use by other classes.
    typedef PrintObjectBaseWithState<Print, FDMPrintObjectStep, posCount> Inherited;

public:
    // Size of an object: XYZ in scaled coordinates. The size might not be quite snug in XY plane.
    const Domain::Vec3crd&       size() const			{ return m_size; }
    const PrintObjectConfigView&     config() const         { return m_config; }
    void                         set_config(const PrintObjectConfigView& config) { m_config = config; }
    auto                         layers() const         { return SpanOfConstPtrs<Layer>(const_cast<const Layer* const*>(m_layers.data()), m_layers.size()); }
    auto                         support_layers() const { return SpanOfConstPtrs<SupportLayer>(const_cast<const SupportLayer* const*>(m_support_layers.data()), m_support_layers.size()); }
    const Domain::Transform3d&   trafo() const          { return m_trafo; }
    // Trafo with the center_offset() applied after the transformation, to center the object in XY before slicing.
    Domain::Transform3d          trafo_centered() const;
    const PrintInstances&        instances() const      { return m_instances; }

    // Whoever will get a non-const pointer to PrintObject will be able to modify its layers.
    LayerPtrs&                   layers()               { return m_layers; }
    SupportLayerPtrs&            support_layers()       { return m_support_layers; }

    // Bounding box is used to align the object infill patterns, and to calculate attractor for the rear seam.
    // The bounding box may not be quite snug.
    Domain::BoundingBox2crd      bounding_box() const   { return Domain::BoundingBox2crd(Domain::Point(- m_size.x() / 2, - m_size.y() / 2), Domain::Point(m_size.x() / 2, m_size.y() / 2)); }
    // Height is used for slicing, for sorting the objects by height for sequential printing and for checking vertical clearence in sequential print mode.
    // The height is snug.
    Domain::coord_t 			 height() const         { return m_size.z(); }

    bool                         has_brim() const       {
        return this->config().get<Domain::BrimType>("brim_type") != Domain::BrimType::NoBrim
            && this->config().get<double>("brim_width") > 0.
            && ! this->has_raft();
    }

    // This is the *total* layer count (including support layers)
    // this value is not supposed to be compared with Layer::id
    // since they have different semantics.
    size_t 			total_layer_count() const { return this->layer_count() + this->support_layer_count(); }
    size_t 			layer_count() const { return m_layers.size(); }
    void 			clear_layers();
    const Layer* 	get_layer(int idx) const { return m_layers[idx]; }
    Layer* 			get_layer(int idx) 		 { return m_layers[idx]; }
    // Get a layer exactly at print_z.
    const Layer*	get_layer_at_printz(double print_z) const;
    Layer*			get_layer_at_printz(double print_z);
    // Get a layer approximately at print_z.
    const Layer*	get_layer_at_printz(double print_z, double epsilon) const;
    Layer*			get_layer_at_printz(double print_z, double epsilon);
    // Get the first layer approximately bellow print_z.
    const Layer*	get_first_layer_bellow_printz(double print_z, double epsilon) const;

    // print_z: top of the layer; slice_z: center of the layer.
    Layer*          add_layer(int id, double height, double print_z, double slice_z);

    size_t          support_layer_count() const { return m_support_layers.size(); }
    void            clear_support_layers();
    SupportLayer*   get_support_layer(int idx) { return m_support_layers[idx]; }
    SupportLayer*   add_support_layer(int id, int interface_id, double height, double print_z);
    SupportLayerPtrs::iterator insert_support_layer(SupportLayerPtrs::iterator pos, size_t id, size_t interface_id, double height, double print_z, double slice_z);
    void            delete_support_layer(int idx);
    
    // Initialize the layer_height_profile from the model_object's layer_height_profile, from model_object's layer height table, or from slicing parameters.
    // Returns true, if the layer_height_profile was changed.
    static bool     update_layer_height_profile(const Domain::ModelObject &model_object, const SlicingParameters &slicing_parameters, Domain::ZHeightPairs &layer_height_profile);

    // Collect the slicing parameters, to be used by variable layer thickness algorithm,
    // by the interactive layer height editor and by the printing process itself.
    // The slicing parameters are dependent on various configuration values
    // (layer height, first layer height, raft settings, print nozzle diameter etc).
    const SlicingParameters&    slicing_parameters() const { return m_slicing_params; }

    size_t                      num_printing_regions() const throw() { return m_shared_regions->all_regions.size(); }
    const PrintRegion&          printing_region(size_t idx) const throw() { return *m_shared_regions->all_regions[idx].get(); }
    //FIXME returing all possible regions before slicing, thus some of the regions may not be slicing at the end.
    std::vector<std::reference_wrapper<const PrintRegion>> all_regions() const;
    const PrintObjectRegions*   shared_regions() const { return m_shared_regions.get(); }

    bool                        has_support()           const { return m_config.get<Domain::SupportMode>("support_material") != Domain::SupportMode::None || m_config.get<int>("support_material_enforce_layers") > 0; }
    // Whether the object has any support enforcer (an enforcer volume or painted enforcer facets).
    bool                        has_support_enforcers() const;
    bool                        has_raft()              const { return m_config.get<int>("raft_layers") > 0; }
    bool                        has_support_material()  const { return this->has_support() || this->has_raft(); }
    // Checks if the model object is painted using the multi-material painting gizmo.
    bool                        is_mm_painted()         const { return this->model_object()->is_mm_painted(); }
    // Checks if the model object is painted using the fuzzy skin painting gizmo.
    bool                        is_fuzzy_skin_painted() const { return this->model_object()->is_fuzzy_skin_painted(); }

    // returns 0-based indices of extruders used to print the object (without brim, support and other helper extrusions)
    std::vector<unsigned int>   object_extruders() const;

    // Called by make_perimeters()
    void slice();

    // Helpers to slice support enforcer / blocker meshes by the support generator.
    std::vector<Domain::Polygons> slice_support_volumes(const Domain::ModelVolumeType model_volume_type) const;
    std::vector<Domain::Polygons> slice_support_blockers() const { return this->slice_support_volumes(Domain::ModelVolumeType::SUPPORT_BLOCKER); }
    std::vector<Domain::Polygons> slice_support_enforcers() const { return this->slice_support_volumes(Domain::ModelVolumeType::SUPPORT_ENFORCER); }

    // Helpers to project custom facets on slices
    void project_and_append_custom_facets(bool seam, Domain::TriangleSelector::TriangleStateType type, std::vector<Domain::Polygons>& expolys) const;

private:
    // to be called from Print only.
    friend class Print;
    friend class PrintBaseWithState<FDMPrintStep, psCount>;

public:
	PrintObject(Print* print, Domain::ModelObject* model_object, const PrintObjectConfigView& config, const Domain::Transform3d& trafo, PrintInstances&& instances);

    ~PrintObject() override {
        clear_layers();
        clear_support_layers();
    }

    FDMPrintSteps           set_instances(PrintInstances &&instances);
    // Invalidates the step, and its depending steps in PrintObject and Print.
    bool                    invalidate_step(FDMPrintObjectStep step);
    // Invalidates all PrintObject and Print steps.
    bool                    invalidate_all_steps();
    // If ! m_slicing_params.valid, recalculate.
    void                    update_slicing_parameters();

    // Called on main thread with stopped or paused background processing to let PrintObject release data for its milestones that were invalidated or canceled.
    void                    cleanup();

    void set_shared_regions(const std::shared_ptr<PrintObjectRegions>& regions);

private:
    void make_perimeters();
    void prepare_infill();
    void clear_fills();
    void infill();
    void ironing();
    void generate_support_spots();
    void generate_support_material();
    void estimate_curled_extrusions();
    void calculate_overhanging_perimeters();

    void slice_volumes();
    // Has any support (not counting the raft).
    void detect_surfaces_type();
    void process_external_surfaces();
    void discover_vertical_shells();
    void bridge_over_infill();
    void clip_fill_surfaces();
    void discover_horizontal_shells();
    void combine_infill();
    void _generate_support_material();
    std::pair<FillAdaptive::OctreePtr, FillAdaptive::OctreePtr> prepare_adaptive_infill_data(
        const std::vector<std::pair<const Surface*, float>>& surfaces_w_bottom_z) const;
    FillLightning::GeneratorPtr prepare_lightning_infill_data();

    // XYZ in scaled coordinates
    Domain::Vec3crd							m_size;
    PrintObjectConfigView                   m_config;
    // Translation in Z + Rotation + Scaling / Mirroring.
    Domain::Transform3d                     m_trafo = Domain::Transform3d::Identity();
    // Slic3r::Point objects in scaled G-code coordinates
    std::vector<PrintInstance>              m_instances;
    // The mesh is being centered before thrown to Clipper, so that the Clipper's fixed coordinates require less bits.
    // This is the adjustment of the  the Object's coordinate system towards PrintObject's coordinate system.
    Domain::Vec2d                           m_center_offset_unscaled;

    // Object split into layer ranges and regions with their associated configurations.
    // Shared among PrintObjects created for the same ModelObject.
    std::shared_ptr<PrintObjectRegions>     m_shared_regions;

    SlicingParameters                       m_slicing_params;
    LayerPtrs                               m_layers;
    SupportLayerPtrs                        m_support_layers;

    // this is set to true when LayerRegion->slices is split in top/internal/bottom
    // so that next call to make_perimeters() performs a union() before computing loops
    bool                    				m_typed_slices = false;

    std::pair<FillAdaptive::OctreePtr, FillAdaptive::OctreePtr> m_adaptive_fill_octrees;
    FillLightning::GeneratorPtr m_lightning_generator;
};



struct WipeTowerData
{
    // Cache of tool changes per print layer.
    std::unique_ptr<std::vector<WipeTower::ToolChangeResult>> priming;
    std::vector<std::vector<WipeTower::ToolChangeResult>> tool_changes;
    std::unique_ptr<WipeTower::ToolChangeResult>          final_purge;
    std::vector<std::pair<float, std::vector<float>>>     used_filament_until_layer;
    int                                                   number_of_toolchanges{};

    // Depth of the wipe tower to pass to GLCanvas3D for exact bounding box:
    float                                                 depth{};
    std::vector<std::pair<float, float>>                  z_and_depth_pairs;
    float                                                 brim_width{};
    float                                                 height{};

    // Data needed to generate fake extrusions for conflict checking.
    float                                                 width{};
    float                                                 first_layer_height{};
    float                                                 cone_x_scale{};
    float                                                 cone_radius{};
    Domain::Vec2d                                         position{};
    float                                                 rotation_angle{};
};

bool is_toolchange_required(
    const bool first_layer,
    const unsigned last_extruder_id,
    const unsigned extruder_id,
    const unsigned current_extruder_id
);

using PrintObjectPtrs          = std::vector<PrintObject*>;
using ConstPrintObjectPtrs     = std::vector<const PrintObject*>;

using PrintRegionPtrs          = std::vector<PrintRegion*>;

enum ModelObjectCreationStatus
{
    Unknown,
    Old,
    New,
    Moved,
    Deleted
};

struct Thumbnails {
    std::future<Biz::Slicing::ThumbnailImageResults> raw_data;
    std::vector<Domain::GCodeThumbnailsFormat> formats;
};

// The complete print tray with possibly multiple objects.
class Print : public PrintBaseWithState<FDMPrintStep, psCount>
{
private: // Prevents erroneous use by other classes.
    typedef PrintBaseWithState<FDMPrintStep, psCount> Inherited;
    // Bool indicates if supports of PrintObject are top-level contour.
    typedef std::pair<PrintObject *, bool>         PrintObjectInfo;

public:
    using OnFdmResult              = std::function<void(Biz::libpgcode::ProcessorResult&&)>;
    using OnWipeTowerGeometry      = std::function<void(Biz::Slicing::OptWipeTowerGeometry)>;
    using OnExtruderCandidates     = std::function<void(std::vector<unsigned>)>;
    using OnGeneratedSupportPoints =
        std::function<void(Biz::Slicing::GeneratedSupportPointsSnapshot&&)>;
    Print();
    Print(
        const OnFdmResult& on_fdm_result,
        const OnWipeTowerGeometry& on_wipe_tower_geometry,
        const OnExtruderCandidates& on_extruder_candidates,
        const OnGeneratedSupportPoints& on_generated_support_points
    );
    virtual ~Print();

    Domain::PrinterTechnology	technology() const noexcept override { return Domain::PrinterTechnology::FFF; }

    // Methods, which change the state of Print / PrintObject / PrintRegion.
    // The following methods are synchronized with process() and export_gcode(),
    // so that process() and export_gcode() may be called from a background thread.
    // In case the following methods need to modify data processed by process() or export_gcode(),
    // a cancellation callback is executed to stop the background processing before the operation.
    void                clear() override;
    bool                empty() const override { return m_objects.empty(); }
    // List of existing PrintObject IDs, to remove notifications for non-existent IDs.
    std::vector<Domain::ObjectID> print_object_ids() const override;

    virtual Biz::Slicing::ApplyStatus::Status update(
        Domain::Model& model,
        const Domain::ConfigPack& config,
        const Domain::BedInstance& bed,
        const Domain::Preset::SelectedPresetMetadata& metadata,
        const MetadataSerializeFn& serializer
    ) override;

    Biz::Slicing::ApplyStatus::Status apply(
        const Domain::Model& model,
        const Domain::FullConfigFDMPtr& new_full_config_ptr,
        const Domain::Preset::SelectedPresetMetadata& metadata,
        const MetadataSerializeFn& serializer,
        const Domain::ModelWipeTower& wipe_tower,
        const std::optional<Domain::CustomGCode::Info>& custom_gcode,
        const std::vector<unsigned>& extruder_candidates
    );

    static Domain::ModelInstancePtrs deep_copy_instances(
        const Domain::ModelInstancePtrs& instances,
        Domain::ModelObject* model_object
    );

    bool invalidate_object_steps(
        const SlicingSync::InvalidatedSteps& steps
    );

    void                set_task(const TaskParams &params) override { PrintBaseWithState<FDMPrintStep, psCount>::set_task_impl(params, m_objects); }
    void                process() override;
    void                finalize() override { PrintBaseWithState<FDMPrintStep, psCount>::finalize_impl(m_objects); }
    void                cleanup() override;

    void slice(
        Domain::SlicingId slicing_id,
        Biz::Slicing::IThumbnailImageGenerator&,
        std::optional<Biz::Slicing::SliceUntilStep> slice_until_step
    ) override;

    // Exports G-code into a file name based on the path_template, returns the file path of the generated G-code file.
    // If preview_data is not null, the preview_data is filled in for the G-code visualization (not used by the command line Slic3r).
    Biz::libpgcode::ProcessorResult process_gcode();

    // methods for handling state
    bool                is_step_done(FDMPrintStep step) const { return Inherited::is_step_done(step); }
    // Returns true if an object step is done on all objects and there's at least one object.    
    bool                is_step_done(FDMPrintObjectStep step) const;
    // Returns true if the last step was finished with success.
    bool                finished() const override { return this->is_step_done(psGCodeExport); }

    bool                has_infinite_skirt() const;
    bool                has_skirt() const;
    bool                has_brim() const;

    // Returns an empty string if valid, otherwise returns an error message.
    Biz::Slicing::ValidationResult validate() const;
    double              skirt_first_layer_height() const;
    Flow                brim_flow() const;
    Flow                skirt_flow() const;

    unsigned int num_physical_extruders() const
    {
        return static_cast<unsigned int>(m_config.hw_config().material_slot_count());
    }

    const Domain::VirtualExtruders& virtual_extruders() const
    {
        return m_virtual_extruders;
    }

    std::vector<unsigned int> object_extruders() const;
    std::vector<unsigned int> support_material_extruders() const;
    std::vector<unsigned int> extruders() const;
    double              max_allowed_layer_height() const;
    bool                has_support_material() const;

    const PrintConfigView& config() const
    {
        return m_config;
    }

    void set_config(const PrintConfigView& config) { m_config = config; }
    SpanOfConstPtrs<PrintObject> objects() const { return SpanOfConstPtrs<PrintObject>(const_cast<const PrintObject* const*>(m_objects.data()), m_objects.size()); }
    PrintObject*                get_object(size_t idx) { return const_cast<PrintObject*>(m_objects[idx]); }
    const PrintObject*          get_object(size_t idx) const { return m_objects[idx]; }
    const PrintObject* get_print_object_by_model_object_id(Domain::ObjectID object_id) const {
        auto it = std::find_if(m_objects.begin(), m_objects.end(),
                               [object_id](const PrintObject* obj) { return obj->model_object()->id() == object_id; });
        return (it == m_objects.end()) ? nullptr : *it;
    }
    // PrintObject by its ObjectID, to be used to uniquely bind slicing warnings to their source PrintObjects
    // in the notification center.
    const PrintObject*          get_object(Domain::ObjectID object_id) const {
        auto it = std::find_if(m_objects.begin(), m_objects.end(), 
            [object_id](const PrintObject *obj) { return obj->id() == object_id; });
        return (it == m_objects.end()) ? nullptr : *it;
    }
    // How many of PrintObject::copies() over all print objects are there?
    // If zero, then the print is empty and the print shall not be executed.
    unsigned int                num_object_instances() const;

    const ExtrusionEntityCollection& skirt() const { return m_skirt; }
    const ExtrusionEntityCollection& brim() const { return m_brim; }
    // Convex hull of the 1st layer extrusions, for bed leveling and placing the initial purge line.
    // It encompasses the object extrusions, support extrusions, skirt, brim, wipe tower.
    // It does NOT encompass user extrusions generated by custom G-code,
    // therefore it does NOT encompass the initial purge line.
    // It does NOT encompass MMU/MMU2 starting (wipe) areas.
    const Domain::Polygon&      first_layer_convex_hull() const { return m_first_layer_convex_hull; }

    // Wipe tower support.
    bool                        can_have_wipe_tower() const;
    const std::optional<WipeTowerData>& wipe_tower_data() const;
    const ToolOrdering& 		tool_ordering() const { return m_tool_ordering; }

    size_t                      num_print_regions() const throw() { return m_print_regions.size(); }
    const PrintRegion&          get_print_region(size_t idx) const  { return *m_print_regions[idx]; }
    const ToolOrdering&         get_tool_ordering() const { return m_tool_ordering; }
    const std::vector<unsigned>& get_extruder_candidates() const { return m_extruder_candidates; }

    // Invalidates the step, and its depending steps in Print.
    bool                invalidate_step(FDMPrintStep step);

    void                _make_skirt();

    // Modifies m_tool_ordering_heavily!
    std::optional<WipeTowerData> generate_wipe_tower_data();
    void                finalize_first_layer_convex_hull();
    void                alert_when_supports_needed();

    // Islands of objects and their supports extruded at the 1st layer.
    Domain::Polygons    first_layer_islands() const;
    // Return 4 wipe tower corners in the world coordinates (shifted and rotated), including the wipe tower brim.
    Domain::Points      first_layer_wipe_tower_corners() const;

    // Returns true if any of the print_objects has print_object_step valid.
    // That means data shared by all print objects of the print_objects span may still use the shared data.
    // Otherwise the shared data shall be released.
    // Unguarded variant, thus it shall only be called from main thread with background processing stopped.
    static bool         is_shared_print_object_step_valid_unguarded(SpanOfConstPtrs<PrintObject> print_objects, FDMPrintObjectStep print_object_step);

    OnFdmResult m_on_fdm_result;
    OnWipeTowerGeometry m_on_wipe_tower_geometry;
    OnExtruderCandidates m_on_extruder_candidates;
    OnGeneratedSupportPoints m_on_generated_support_points;

    PrintConfigView m_config;
    MetadataSerializeFn m_metadata_serializer;

    PrintObjectPtrs m_objects;
    PrintRegionPtrs m_print_regions;
    Domain::VirtualExtruders m_virtual_extruders;

    // Ordered collections of extrusion paths to build skirt loops and brim.
    ExtrusionEntityCollection               m_skirt;
    ExtrusionEntityCollection               m_brim;
    // Convex hull of the 1st layer extrusions.
    // It encompasses the object extrusions, support extrusions, skirt, brim, wipe tower.
    // It does NOT encompass user extrusions generated by custom G-code,
    // therefore it does NOT encompass the initial purge line.
    // It does NOT encompass MMU/MMU2 starting (wipe) areas.
    Domain::Polygon                         m_first_layer_convex_hull;
    Domain::Points                          m_skirt_convex_hull;

    // Following section will be consumed by the GCodeGenerator.
    ToolOrdering 							m_tool_ordering;
    std::optional<WipeTowerData>            m_wipe_tower_data;

    Thumbnails thumbnails;

    // To allow GCode to set the Print's GCodeExport step status.
    friend class GCodeGenerator;
    // To allow GCodeProcessor to emit warnings.
    friend class GCodeProcessor;
    // Allow PrintObject to access m_mutex and m_cancel_callback.
    friend class PrintObject;

    std::optional<std::pair<std::string, std::string>> m_sequential_collision_detected; // names of objects (hit first when printing second)

private:
    std::vector<unsigned> m_extruder_candidates;
    Domain::Vec3d m_shrinkage_compensation{Domain::Vec3d::Ones()};
    std::unique_ptr<Biz::Slicing::PrePreview> m_pre_preview;
    bool m_invalid{false};
};

} /* slic3r_Print_hpp_ */

#endif
