#ifndef SLIC3R_TEST_DATA_HPP
#define SLIC3R_TEST_DATA_HPP

#include "libslic3r/Config.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/GCode/ModelVisibility.hpp"
#include "libslic3r/GCode/SeamGeometry.hpp"
#include "libslic3r/GCode/SeamPerimeters.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/GCode/SeamPlacer.hpp"
#include "libslic3r/GCode/SeamAligned.hpp"

#include <filesystem>
#include <unordered_map>

namespace Slic3r { namespace Test {

constexpr double MM_PER_MIN = 60.0;

/// Enumeration of test meshes
enum class TestMesh {
    A,
    L,
    V,
    _40x10,
    cube_20x20x20,
    cube_2x20x10,
    sphere_50mm,
    bridge,
    bridge_with_hole,
    cube_with_concave_hole,
    cube_with_hole,
    gt2_teeth,
    ipadstand,
    overhang,
    pyramid,
    sloping_hole,
    slopy_cube,
    small_dorito,
    step,
    two_hollow_squares
};

// Neccessary for <c++17
struct TestMeshHash
{
    std::size_t operator()(TestMesh tm) const { return static_cast<std::size_t>(tm); }
};

/// Mesh enumeration to name mapping
extern const std::unordered_map<TestMesh, const char *, TestMeshHash> mesh_names;

/// Port of Slic3r::Test::mesh
/// Basic cubes/boxes should call TriangleMesh::make_cube() directly and rescale/translate it
TriangleMesh mesh(TestMesh m);

TriangleMesh mesh(TestMesh m, Vec3d translate, Vec3d scale = Vec3d(1.0, 1.0, 1.0));
TriangleMesh mesh(TestMesh m, Vec3d translate, double scale = 1.0);

/// Templated function to see if two values are equivalent (+/- epsilon)
template<typename T> bool _equiv(const T &a, const T &b) { return std::abs(a - b) < EPSILON; }

template<typename T> bool _equiv(const T &a, const T &b, double epsilon) {
    return abs(a - b) < epsilon;
}

Slic3r::Model model(const std::string &model_name, TriangleMesh &&_mesh);
void init_print(
    std::vector<TriangleMesh> &&meshes,
    Slic3r::Print &print,
    Slic3r::Model &model,
    const DynamicPrintConfig &config_in,
    bool comments = false,
    unsigned duplicate_count = 1
);
void init_print(
    std::initializer_list<TestMesh> meshes,
    Slic3r::Print &print,
    Slic3r::Model &model,
    const Slic3r::DynamicPrintConfig &config_in = Slic3r::DynamicPrintConfig::full_print_config(),
    bool comments = false,
    unsigned duplicate_count = 1
);
void init_print(
    std::initializer_list<TriangleMesh> meshes,
    Slic3r::Print &print,
    Slic3r::Model &model,
    const Slic3r::DynamicPrintConfig &config_in = Slic3r::DynamicPrintConfig::full_print_config(),
    bool comments = false,
    unsigned duplicate = 1
);
void init_print(
    std::initializer_list<TestMesh> meshes,
    Slic3r::Print &print,
    Slic3r::Model &model,
    std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items,
    bool comments = false,
    unsigned duplicate = 1
);
void init_print(
    std::initializer_list<TriangleMesh> meshes,
    Slic3r::Print &print,
    Slic3r::Model &model,
    std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items,
    bool comments = false,
    unsigned duplicate = 1
);

void init_and_process_print(
    std::initializer_list<TestMesh> meshes,
    Slic3r::Print &print,
    const DynamicPrintConfig &config,
    bool comments = false
);
void init_and_process_print(
    std::initializer_list<TriangleMesh> meshes,
    Slic3r::Print &print,
    const DynamicPrintConfig &config,
    bool comments = false
);
void init_and_process_print(
    std::initializer_list<TestMesh> meshes,
    Slic3r::Print &print,
    std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items,
    bool comments = false
);
void init_and_process_print(
    std::initializer_list<TriangleMesh> meshes,
    Slic3r::Print &print,
    std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items,
    bool comments = false
);

std::string gcode(Print &print);

std::string slice(
    std::initializer_list<TestMesh> meshes, const DynamicPrintConfig &config, bool comments = false
);
std::string slice(
    std::initializer_list<TriangleMesh> meshes,
    const DynamicPrintConfig &config,
    bool comments = false
);
std::string slice(
    std::initializer_list<TestMesh> meshes,
    std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items,
    bool comments = false
);
std::string slice(
    std::initializer_list<TriangleMesh> meshes,
    std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items,
    bool comments = false
);

bool contains(const std::string &data, const std::string &pattern);
bool contains_regex(const std::string &data, const std::string &pattern);

inline std::unique_ptr<Print> process_3mf(const std::filesystem::path &path) {
    DynamicPrintConfig config;
    auto print{std::make_unique<Print>()};
    Model model;

    ConfigSubstitutionContext context{ForwardCompatibilitySubstitutionRule::Disable};
    boost::optional<Semver> version;
    load_3mf(path.string().c_str(), config, context, &model, false, version);

    Slic3r::Test::init_print(std::vector<TriangleMesh>{}, *print, model, config);
    print->process();

    return print;
}

static std::map<std::string, std::unique_ptr<Print>> prints_3mfs;
// Lazy getter, to avoid processing the 3mf multiple times, it already takes ages.
inline Print *get_print(const std::filesystem::path &file_path) {
    if (!prints_3mfs.count(file_path.string())) {
        prints_3mfs[file_path.string()] = process_3mf(file_path.string());
    }
    return prints_3mfs[file_path.string()].get();
}

inline void serialize_seam(std::ostream &output, const std::vector<std::vector<Seams::SeamPerimeterChoice>> &seam) {
    output << "x,y,z,layer_index" << std::endl;

    for (const std::vector<Seams::SeamPerimeterChoice> &layer : seam) {
        if (layer.empty()) {
            continue;
        }
        const Seams::SeamPerimeterChoice &choice{layer.front()};

        // clang-format off
        output
            << choice.choice.position.x() << ","
            << choice.choice.position.y() << ","
            << choice.perimeter.slice_z << ","
            << choice.perimeter.layer_index << std::endl;
        // clang-format on
    }
}

struct SeamsFixture
{
    const std::filesystem::path file_3mf{
        std::filesystem::path{TEST_DATA_DIR} / std::filesystem::path{"seam_test_object.3mf"}};
    const Print *print{Test::get_print(file_3mf)};
    const PrintObject *print_object{print->objects()[0]};

    Seams::Params params{Seams::Placer::get_params(print->full_print_config())};

    const Transform3d transformation{print_object->trafo_centered()};
    const ModelVolumePtrs &volumes{print_object->model_object()->volumes};
    Seams::ModelInfo::Painting painting{transformation, volumes};

    const std::vector<Seams::Geometry::Extrusions> extrusions{
        Seams::Geometry::get_extrusions(print_object->layers())};
    const Seams::Perimeters::LayerInfos layer_infos{Seams::Perimeters::get_layer_infos(
        print_object->layers(), params.perimeter.elephant_foot_compensation
    )};
    const std::vector<Seams::Geometry::BoundedPolygons> projected{
        Seams::Geometry::project_to_geometry(extrusions, params.max_distance)};

    const ModelInfo::Visibility visibility{transformation, volumes, params.visibility, [](){}};
    Seams::Aligned::VisibilityCalculator
        visibility_calculator{visibility, params.convex_visibility_modifier, params.concave_visibility_modifier};
};

}} // namespace Slic3r::Test

#endif // SLIC3R_TEST_DATA_HPP
