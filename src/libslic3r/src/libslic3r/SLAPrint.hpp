#ifndef slic3r_SLAPrint_hpp_
#define slic3r_SLAPrint_hpp_

#include <boost/functional/hash.hpp>
#include <stdlib.h>
#include <cstdint>
#include <mutex>
#include <set>
#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/Types.hpp"

#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintSteps.hpp"

#include "libslic3r/CSGMesh/CSGMesh.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/MeshBoolean.hpp"

#include "libslic3r/SLA/Hollowing.hpp"
#include "libslic3r/SLA/Pad.hpp"
#include "libslic3r/SLAResult.hpp"
#include "libslic3r/SLA/SupportTree.hpp"
#include "libslic3r/SLA/SupportPointGenerator.hpp"

namespace Slic3r {
namespace sla {
struct JobController;
}  // namespace sla

class SLAPrint;

using _SLAPrintObjectBase =
    PrintObjectBaseWithState<SLAPrint, SLAPrintObjectStep, slaposCount>;

// Layers according to quantized height levels. This will be consumed by
// the printer (rasterizer) in the SLAPrint class.
// using coord_t = int64_t;

enum SliceOrigin { soSupport, soModel };

} // namespace Slic3r

namespace Slic3r {

// Each sla object step can hold a collection of csg operations on the
// sla model to be sliced. Currently, Assembly step adds negative and positive
// volumes, hollowing adds the negative interior, drilling adds the hole cylinders.
// They need to be processed in this specific order. If CSGPartForStep instances
// are put into a multiset container the key being the sla step,
// iterating over the container will maintain the correct order of csg operations.
struct CSGPartForStep : public csg::CSGPart
{
    SLAPrintObjectStep key;
    mutable Biz::CGAL::Algorithms::MeshBoolean::cgal::CGALMeshPtr cgalcache;

    CSGPartForStep(SLAPrintObjectStep k, CSGPart &&p = {})
        : CSGPart{std::move(p)}, key{k}
    {}

    CSGPartForStep &operator=(CSGPart &&part)
    {
        this->its_ptr = std::move(part.its_ptr);
        this->operation = part.operation;

        return *this;
    }

    bool operator<(const CSGPartForStep &other) const { return key < other.key; }
};

namespace csg {

Biz::CGAL::Algorithms::MeshBoolean::cgal::CGALMeshPtr get_cgalmesh(const CSGPartForStep &part);

} // namespace csg

class SLAPrintObject : public _SLAPrintObjectBase
{
private: // Prevents erroneous use by other classes.
    using Inherited = _SLAPrintObjectBase;
    using CSGContainer = std::multiset<CSGPartForStep>;

public:
    // I refuse to grantee copying (Tamas)
    SLAPrintObject(const SLAPrintObject&) = delete;
    SLAPrintObject& operator=(const SLAPrintObject&) = delete;

    const SLAPrintObjectConfigView& config() const { return m_config; }
    const Domain::Transform3d&      trafo()  const { return m_trafo; }
    bool                        is_left_handed() const { return m_left_handed; }

    struct Instance {
        Instance(Domain::ObjectID inst_id, const Domain::Point &shft, float rot) : instance_id(inst_id), shift(shft), rotation(rot) {}
        bool operator==(const Instance &rhs) const { return this->instance_id == rhs.instance_id && this->shift == rhs.shift && this->rotation == rhs.rotation; }
        // ID of the corresponding ModelInstance.
        Domain::ObjectID instance_id;
        // Slic3r::Point objects in scaled G-code coordinates
        Domain::Point shift;
        // Rotation along the Z axis, in radians.
        float 	rotation;
    };
    using Instances = std::vector<Instance>;

    // Get a support mesh centered around origin in XY, and with zero rotation around Z applied.
    // Support mesh is only valid if this->is_step_done(slaposSupportTree) is true.
    const Domain::TriangleMesh&     support_mesh() const;
    // Get a pad mesh centered around origin in XY, and with zero rotation around Z applied.
    // Support mesh is only valid if this->is_step_done(slaposPad) is true.
    const Domain::TriangleMesh&     pad_mesh() const;

    // Get the mesh that is going to be printed with all the modifications
    // like hollowing and drilled holes.
    std::shared_ptr<const Domain::TriangleMesh> get_mesh_to_print() const;

    std::vector<csg::CSGPart> get_parts_to_slice() const;

    std::vector<csg::CSGPart> get_parts_to_slice(SLAPrintObjectStep step) const;

    Domain::SLA::SupportPoints      transformed_support_points() const;
    Domain::SLA::DrainHoles         transformed_drainhole_points() const;

    // Get the needed Z elevation for the model geometry if supports should be
    // displayed. This Z offset should also be applied to the support
    // geometries. Note that this is not the same as the value stored in config
    // as the pad height also needs to be considered.
    double get_elevation() const;

    // This method returns the needed elevation according to the processing
    // status. If the supports are not ready, it is zero, if they are and the
    // pad is not, then without the pad, otherwise the full value is returned.
    double get_current_elevation() const;

    // The public Slice record structure. It corresponds to one printable layer.
    class SliceRecord {
    public:
        // this will be the max limit of size_t
        static const size_t NONE = size_t(-1);

        static const SliceRecord EMPTY;

    private:
        Domain::coord_t m_print_z = 0;      // Top of the layer
        float           m_slice_z = 0.f;    // Exact level of the slice
        float           m_height  = 0.f;     // Height of the sliced layer

        size_t m_model_slices_idx = NONE;
        size_t m_support_slices_idx = NONE;
        const SLAPrintObject *m_po = nullptr;

    public:

        SliceRecord(Domain::coord_t key, float slicez, float height):
            m_print_z(key), m_slice_z(slicez), m_height(height) {}

        // The key will be the integer height level of the top of the layer.
        Domain::coord_t print_level() const { return m_print_z; }

        // Returns the exact floating point Z coordinate of the slice
        float slice_level() const { return m_slice_z; }

        // Returns the current layer height
        float layer_height() const { return m_height; }

        bool is_valid() const { return m_po && ! std::isnan(m_slice_z); }

        const SLAPrintObject* print_obj() const { return m_po; }

        // Methods for setting the indices into the slice vectors.
        void set_model_slice_idx(const SLAPrintObject &po, size_t id) {
            m_po = &po; m_model_slices_idx = id;
        }

        void set_support_slice_idx(const SLAPrintObject& po, size_t id) {
            m_po = &po; m_support_slices_idx = id;
        }

        const Domain::ExPolygons& get_slice(SliceOrigin o) const;
        size_t            get_slice_idx(SliceOrigin o) const
        {
            return o == soModel ? m_model_slices_idx : m_support_slices_idx;
        }
    };

private:

    const std::vector<Domain::ExPolygons>& get_model_slices() const { return m_model_slices; }
    const std::vector<Domain::ExPolygons>& get_support_slices() const;
protected:
    // to be called from SLAPrint only.
    friend class SLAPrint;
    friend class PrintBaseWithState<SLAPrintStep, slapsCount>;

public:
	SLAPrintObject(SLAPrint* print, Domain::ModelObject* model_object, const SLAPrintObjectConfigView& config);

    void                    set_trafo(const Domain::Transform3d& trafo, bool left_handed) {
        m_trafo = trafo;
        m_left_handed = left_handed;
    }

    const std::vector<Instance>& instances() const { return m_instances; }
    inline void set_instances(Instances&& instances) { m_instances = std::move(instances); }

    // Invalidates the step, and its depending steps in SLAPrintObject and SLAPrint.
    bool                    invalidate_all_steps();
    // Invalidate steps based on a set of parameters changed.
    bool                    invalidate_state_by_config_options(const std::vector<std::string> &opt_keys);

    void set_config(const SLAPrintObjectConfigView& config) { m_config = config; }

private:
    // Object specific configuration, pulled from the configuration layer.
    SLAPrintObjectConfigView                m_config;

    // Translation in Z + Rotation by Y and Z + Scaling / Mirroring.
    Domain::Transform3d                     m_trafo = Domain::Transform3d::Identity();
    // m_trafo is left handed -> 3x3 affine transformation has negative determinant.
    bool                                    m_left_handed = false;

    Instances            					m_instances;

    // Individual 2d slice polygons from lower z to higher z levels
    std::vector<Domain::ExPolygons>         m_model_slices;

    // Exact (float) height levels mapped to the slices. Each record contains
    // the index to the model and the support slice vectors.
    std::vector<SliceRecord>                m_slice_index;

    std::vector<float>                      m_model_height_levels;

    // Precalculated data needed for interactive automatic support placement.
    sla::SupportPointGeneratorData          m_support_point_generator_data;

    std::optional<sla::SupportableMesh> m_supportable_mesh;
    std::vector<Domain::ExPolygons> m_support_slices;

    // Holds CSG operations for the printed object, prioritized by print steps.
    CSGContainer                  m_mesh_to_slice;
    // Data type for propagate instance progress into frontend
    std::optional<Biz::Slicing::Sla::Object> m_preview;

    auto mesh_to_slice(SLAPrintObjectStep s) const
    {
        auto r = m_mesh_to_slice.equal_range(s);

        return Range{r.first, r.second};
    }

    auto mesh_to_slice() const { return range(m_mesh_to_slice); }

    sla::InteriorPtr m_hollowing_data;
};

Biz::Slicing::Sla::Object::InstanceTrafos get_instance_trafos(const SLAPrintObject& object);

using PrintObjects = std::vector<SLAPrintObject*>;

using SliceRecord  = SLAPrintObject::SliceRecord;

namespace SLASlicingSync {
struct AllSteps
{};

template<typename T>
using AllOrSome = std::variant<T, AllSteps>;
using PrintSteps = std::set<SLAPrintStep>;
using PrintObjectSteps = std::set<SLAPrintObjectStep>;
using StepsPerPrintObject = std::map<SLAPrintObject*, AllOrSome<PrintObjectSteps>>;
using PrintAndObjectSteps = std::pair<AllOrSome<PrintSteps>, AllOrSome<PrintObjectSteps>>;

struct InvalidatedSteps
{
    AllOrSome<PrintSteps> print;
    StepsPerPrintObject object;

    bool empty() const;
};
}
/**
 * @brief This class is the high level FSM for the SLA printing process.
 *
 * It should support the background processing framework and contain the
 * metadata for the support geometries and their slicing. It should also
 * dispatch the SLA printing configuration values to the appropriate calculation
 * steps.
 */
class SLAPrint : public PrintBaseWithState<SLAPrintStep, slapsCount>
{
private: // Prevents erroneous use by other classes.
    using Inherited = PrintBaseWithState<SLAPrintStep, slapsCount>;    
    class Steps; // See SLAPrintSteps.cpp
    
public:
    using OnSlaResult = std::function<void(Biz::Slicing::SLAResult&&)>;
    using OnSlaObject = std::function<void(const Biz::Slicing::Sla::Object&)>;
    explicit SLAPrint(const OnSlaResult& on_sla_result, const OnSlaObject& on_sla_object);

    ~SLAPrint() override { this->clear(); }

    Domain::PrinterTechnology	technology() const noexcept override { return Domain::PrinterTechnology::SLA; }

    void                clear() override;
    bool                empty() const override { return m_objects.empty(); }
    // List of existing PrintObject IDs, to remove notifications for non-existent IDs.
    std::vector<Domain::ObjectID> print_object_ids() const override;

    Biz::Slicing::ApplyStatus::Status update(
        Domain::Model& model,
        const Domain::ConfigPack& config,
        const Domain::BedInstance& bed,
        const Domain::Preset::SelectedPresetMetadata& metadata,
        const MetadataSerializeFn& serializer
    ) override;

    SLASlicingSync::InvalidatedSteps apply(
        const Domain::Model& model,
        const Domain::ConfigPackSLA& config_pack,
        const Domain::Preset::SelectedPresetMetadata& metadata,
        const MetadataSerializeFn& serializer,
        std::vector<std::string>* warnings = nullptr
    );

    void invalidate_object_steps(
        const SLASlicingSync::InvalidatedSteps& steps
    );

    void                set_task(const TaskParams &params) override { PrintBaseWithState<SLAPrintStep, slapsCount>::set_task_impl(params, m_objects); }
    void                process() override;
    void                finalize() override { Inherited::finalize_impl(m_objects); }
    void                cleanup() override {}

    void slice(
        Domain::SlicingId slicing_id,
        Biz::Slicing::IThumbnailImageGenerator&,
        std::optional<Biz::Slicing::SliceUntilStep> slice_until_step
    ) override;

    // Returns true if an object step is done on all objects and there's at least one object.
    bool                is_object_step_done(SLAPrintObjectStep step) const;
    // Returns true if the last step was finished with success.
    bool                finished() const override { return this->is_object_step_done(slaposSliceSupports) && this->Inherited::is_step_done(slapsRasterize); }

    const PrintObjects& objects() const { return m_objects; }
    // PrintObject by its ObjectID, to be used to uniquely bind slicing warnings to their source PrintObjects
    // in the notification center.
    const SLAPrintObject* get_print_object_by_model_object_id(Domain::ObjectID object_id) const {
        auto it = std::find_if(m_objects.begin(), m_objects.end(),
            [object_id](const SLAPrintObject* obj) { return obj->model_object()->id() == object_id; });
        return (it == m_objects.end()) ? nullptr : *it;
    }
    const SLAPrintObject* get_object(Domain::ObjectID object_id) const {
        auto it = std::find_if(m_objects.begin(), m_objects.end(),
            [object_id](const SLAPrintObject *obj) { return obj->id() == object_id; });
        return (it == m_objects.end()) ? nullptr : *it;
    }

    const SLAPrintConfigView& print_config() const { return m_print_config; }

    Biz::Slicing::SerializedConfig build_serialized_config(
        const Domain::SLA::PrintStatistics& print_statistics
    ) const
    {
        return m_metadata_serializer(print_statistics);
    }

    // Extracted value from the configuration objects
    Domain::Vec3d               relative_correction() const;

    // Return sla tansformation for a given model_object
    Domain::Transform3d sla_trafo(const Domain::ModelObject &model_object) const;

    std::string validate(std::vector<std::string>* warnings = nullptr) const;

    // An aggregation of SliceRecord-s from all the print objects for each
    // occupied layer. Slice record levels dont have to match exactly.
    // They are unified if the level difference is within +/- SCALED_EPSILON
    class PrintLayer {
        Domain::coord_t m_level;

        // The collection of slice records for the current level.
        std::vector<std::reference_wrapper<const SliceRecord>> m_slices;

        Domain::ExPolygons m_transformed_slices;

        template<class Container> void transformed_slices(Container&& c)
        {
            m_transformed_slices = std::forward<Container>(c);
        }
        
        friend class SLAPrint::Steps;

    public:
        
        explicit PrintLayer(Domain::coord_t lvl) : m_level(lvl) {}

        // for being sorted in their container (see m_printer_input)
        bool operator<(const PrintLayer& other) const {
            return m_level < other.m_level;
        }

        void add(const SliceRecord& sr) { m_slices.emplace_back(sr); }

        Domain::coord_t level() const { return m_level; }

        auto slices() const -> const decltype (m_slices)& { return m_slices; }

        const Domain::ExPolygons & transformed_slices() const {
            return m_transformed_slices;
        }
    };
    using PrintLayers = std::vector<PrintLayer>;

    // The aggregated and leveled print records from various objects.
    // TODO: use this structure for the preview in the future.
    const PrintLayers& print_layers() const { return m_printer_input; }

    static bool is_prusa_print(const std::string& printer_model);
    
public:
    // Invalidate steps based on a set of parameters changed.
    bool invalidate_state_by_config_options(const std::vector<std::string> &opt_keys, bool &invalidate_all_model_objects);

    OnSlaResult                     m_on_sla_result;
    OnSlaObject                     m_on_sla_object;

    SLAPrintConfigView m_print_config;
    Domain::Preset::SelectedPresetMetadata m_metadata;
    MetadataSerializeFn m_metadata_serializer;

    ::Slic3r::Domain::SLA::PrintStatistics m_print_statistics;

    PrintObjects                    m_objects;

    // Ready-made data for rasterization.
    PrintLayers m_printer_input;

    class StatusReporter
    {
        double m_st = 0;

    public:
        void operator()(SLAPrint& p, double st, Biz::Slicing::ProgressInfo msg);

        double status() const
        {
            return m_st;
        }
    } m_report_status;

    friend SLAPrintObject;
};

// Helper functions:

bool is_zero_elevation(const SLAPrintObjectConfigView &c);

sla::SupportTreeConfig make_support_cfg(const SLAPrintObjectConfigView& c);

sla::PadConfig::EmbedObject builtin_pad_cfg(const  SLAPrintObjectConfigView& c);

sla::PadConfig make_pad_cfg(const SLAPrintObjectConfigView& c);

bool validate_pad(const indexed_triangle_set &pad, const sla::PadConfig &pcfg);


} // namespace Slic3r

#endif /* slic3r_SLAPrint_hpp_ */
