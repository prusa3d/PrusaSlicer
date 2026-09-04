#pragma once
#include <memory>
#include <vector>
#include <optional>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Image.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/SLA/PrintStatistics.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

#include "libslic3r/SerializedConfig.hpp"

namespace Slic3r::Biz::Slicing {

namespace Sla {

using FileData = std::vector<uint8_t>;
using FilesData = std::vector<FileData>;
enum class FileDataType{sl1_png, sl1_svg, other};
struct OutputFiles
{
    FilesData data; // files binary data,  count files data == slices.size()
    FileDataType type = FileDataType::other; // define type of the file data content
};

// MUST be in order of creation backend
enum class ResultType
{
    None,
    Slices,   // with configs
    Files     // Last steps prepared files for store
};

/**
@brief Issue found on backend during slicing
NOTE: originally it was divided on critical and non-critical warning levels,
now use function is_critical()
*/
enum class ObjectIssueType
{ // issue are chronologicaly orderd and grouped by slice step - for clarity
    BadCGALBooleans,     // CSG mesh is not egligible for proper CGAL booleans!
    BadMashForHollowing, // Mesh to be hollowed is not suitable for hollowing (does not bound a volume).
    BadHoles,            // Unable to drill the current configuration of holes into the model.
    BadHolesDrilling,    // Drilling holes into the mesh failed. This is usually caused by broken model. Try to fix it first.
    BadHolesFailed,      // Failed to drill some holes into the model
    VoxelizedPreview,    // Some parts of the print will be previewed with approximated meshes. 
                         // This does not affect the quality of slices or the physical print in any way.
};
using ObjectIssues = std::vector<ObjectIssueType>;
inline bool is_critical(ObjectIssueType issue) { return false; }

struct Object{
    using TriangleMesh  = ::Slic3r::Domain::TriangleMesh;
    using ObjectID      = ::Slic3r::Domain::ObjectID;
    using SupportPoints = ::Slic3r::Domain::SLA::SupportPoints;
    using InstanceTrafos = std::vector<std::pair<ObjectID, Domain::Transform3d>>;

    // Identify source object in the model
    ObjectID object_id; // Model::Objects[N]::id
                        //
    InstanceTrafos instance_trafos;

    // Holds the preview of the object to be printed
    // (holes, cavities, negatives and positive volumes unified)
    // Essentially this should be a m_mesh_to_slice after the CSG operations
    // or at least an approximation of that.
    std::shared_ptr<const TriangleMesh> mesh;

    // Mesh of the supporting structure
    // Note: Support tree could be shared across build plate
    std::shared_ptr<const TriangleMesh> support_structure;

    // Volume under(or around) the object for better adhesion to the build plate
    // Note: multiple objects and instances could share the same pad
    std::shared_ptr<const TriangleMesh> pad;

    // SupportPoints
    std::shared_ptr<const SupportPoints> support_points;

    // Issues found during slicing
    ObjectIssues issues;
};
using Instances = std::vector<Object>;

} // namespace Sla

struct SLAResultData
{
    SerializedConfig serialized_config;
    Domain::ConfigView config;
    std::optional<Domain::SLA::PrintStatistics> print_statistics;
    Sla::OutputFiles files; // count files == slices.size()
    Domain::Images thumbnails;
    std::string project_name; // upload_job.upload_data.upload_path.filename()
};

// Result of slicing steps
// Input for exporting file for printer
// + provide data during slicing process
struct SLAResult
{
    // New: shared pointer to the data used by store_sl1 and other result-dependent operations
    std::shared_ptr<SLAResultData> export_data;

    // It is filled after slicing
    // NOTE: SLAPrint::print_statistics();

    // m_print->m_printer_input[idx].transformed_slices()
    std::vector<Domain::ExPolygons> slices; // shape of merged models
    std::vector<float> heights; // heights of the slices

    // at the moment also instances and support structure - want to change it

    // used for merge Result in Cache
    // Define the state of the result data
    Sla::ResultType type{Sla::ResultType::None}; // type of the result

    // Whether or not the result of slicing is contained in the bed
    bool contained_in_bed{ true };
};

} // namespace Slic3r::Biz::Slicing
