#ifndef SLIC3R_TEST_DATA_HPP
#define SLIC3R_TEST_DATA_HPP

#include "Slic3r/Biz/Parser/IO.hpp"
#include "Slic3r/TestUtils/HwConfigUtils.hpp"
#include "libslic3r/GCode/ModelVisibility.hpp"
#include "libslic3r/GCode/SeamGeometry.hpp"
#include "libslic3r/GCode/SeamPerimeters.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Print.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "libslic3r/GCode/SeamPlacer.hpp"
#include "libslic3r/GCode/SeamAligned.hpp"
#include "libslic3r/SlicingInput.hpp"
#include "Slic3r/Biz/Config/3mf_legacy.hpp"
#include "Slic3r/Biz/Config/ConfigLegacy.hpp"
#include "Slic3r/Domain/OnBeds.hpp"

#include <boost/filesystem.hpp>
#include <unordered_map>

namespace Slic3r::Test {

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

struct TestConfig : public Domain::ConfigPackFDM
{
    explicit TestConfig(const int extruder_count, const double nozzle_diameter = 0.4) :
        Domain::ConfigPackFDM{extruder_count}
    {
        hw_config = create_dummy_hw_config(extruder_count, nozzle_diameter);
    }

    TestConfig() : Domain::ConfigPackFDM{}
    {
        hw_config = create_dummy_hw_config();
    }

    Biz::Parser::IO::Config get_parser_config() const
    {
        return Biz::Parser::IO::get_parser_config(get_view());
    };

    Domain::FullConfigFDM get_full_config() const
    {
        return **ASSERT_VAL(
            prepare_slicing_input(*this, {}, hw_config)
        );
    };

    PrintConfigView get_view() const
    {
        return PrintConfigView{
            *ASSERT_VAL(prepare_slicing_input(*this, {}, hw_config))
        };
    };

    Domain::Preset::HwPrinterConfig hw_config{};
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
Domain::TriangleMesh mesh(TestMesh m);

Domain::TriangleMesh mesh(TestMesh m, Vec3d translate, Vec3d scale = Vec3d(1.0, 1.0, 1.0));
Domain::TriangleMesh mesh(TestMesh m, Vec3d translate, double scale = 1.0);

/// Templated function to see if two values are equivalent (+/- epsilon)
template<typename T> bool _equiv(const T &a, const T &b) { return std::abs(a - b) < EPSILON; }

template<typename T> bool _equiv(const T &a, const T &b, double epsilon) {
    return abs(a - b) < epsilon;
}

Domain::Model model(const std::string& model_name, Domain::TriangleMesh&& _mesh);
void init_print(
    std::vector<Domain::TriangleMesh>&& meshes,
    Slic3r::Print& print,
    Domain::Model& model,
    const TestConfig& config_in,
    unsigned duplicate_count = 1,
    bool ensure_on_bed       = true
);
void init_print(
    std::initializer_list<TestMesh> meshes,
    Slic3r::Print& print,
    Domain::Model& model,
    const TestConfig& config_in = {},
    unsigned duplicate_count    = 1,
    bool ensure_on_bed          = true
);
void init_print(
    std::initializer_list<Domain::TriangleMesh> meshes,
    Slic3r::Print& print,
    Domain::Model& model,
    const TestConfig& config_in = {},
    unsigned duplicate          = 1,
    bool ensure_on_bed          = true
);
void init_and_process_print(
    std::initializer_list<TestMesh> meshes,
    Slic3r::Print& print,
    const TestConfig& config,
    bool ensure_on_bed = true
);
void init_and_process_print(
    std::initializer_list<Domain::TriangleMesh> meshes,
    Slic3r::Print& print,
    const TestConfig& config,
    bool ensure_on_bed = true
);

std::string gcode(Print& print);

std::string
slice(std::initializer_list<TestMesh> meshes, const TestConfig& config, bool ensure_on_bed = true);
std::string slice(
    std::initializer_list<Domain::TriangleMesh> meshes,
    const TestConfig& config,
    bool ensure_on_bed = true
);

Domain::Preset::SelectedPresetMetadata create_dummy_selected_preset_metadata(
    const Domain::Preset::HwPrinterConfig& hw_config
);
bool contains(const std::string &data, const std::string &pattern);
bool contains_regex(const std::string &data, const std::string &pattern);

inline std::unique_ptr<Print> process_3mf(const boost::filesystem::path &path) {
    Domain::ConfigPack config;
    auto print{std::make_unique<Print>()};
    Domain::Model model;

    boost::optional<Semver> version;
    Domain::WipeTowersOnBeds wipe_towers;
    Domain::CustomGCodesOnBeds custom_gcodes;
    Biz::LegacyPresetMetadata preset_metadata;
    Biz::VirtualExtrudersConfig virtual_extruders_config;
    Slic3rLegacy::load_3mf_legacy(path.string().c_str(), config, preset_metadata, &model, false, version, wipe_towers, custom_gcodes, virtual_extruders_config);

    const auto fdm_config{std::get<Domain::ConfigPackFDM>(config)};
    TestConfig test_config{static_cast<int>(fdm_config.tool.size())};
    test_config.print = fdm_config.print;
    test_config.tool = fdm_config.tool;
    test_config.printer = fdm_config.printer;
    test_config.project = fdm_config.project;
    test_config.filament = fdm_config.filament;

    Slic3r::Test::init_print(std::vector<Domain::TriangleMesh>{}, *print, model, test_config);
    print->process();

    return print;
}

static std::map<std::string, std::unique_ptr<Print>> prints_3mfs;
// Lazy getter, to avoid processing the 3mf multiple times, it already takes ages.
inline Print *get_print(const boost::filesystem::path &file_path) {
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
    const boost::filesystem::path file_3mf{
        boost::filesystem::path{TEST_DATA_DIR} / boost::filesystem::path{"seam_test_object.3mf"}};
    const Print *print{Test::get_print(file_3mf)};
    const PrintObject *print_object{print->objects()[0]};

    Seams::Params params{Seams::Placer::get_params(print->config())};

    const Transform3d transformation{print_object->trafo_centered()};
    const Domain::ModelVolumePtrs &volumes{print_object->model_object()->volumes};
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



inline double arrange_min_distance(const TestConfig& config)
{
    double out = 6.;
    if (config.get_view().get<Domain::PrinterTechnology>("printer_technology") == Domain::PrinterTechnology::FFF) {
        out = 0.;
        bool has_all = std::ranges::all_of(std::vector<std::string>{"extruder_clearance_radius", "duplicate_distance", "complete_objects"},
            [&config](const auto& s) { return bool(config.contains("s", 0).item); });
        if (has_all) {
            auto ecr = config.get_view().get<double>("extruder_clearance_radius");
            auto dd  = config.get_view().get<double>("duplicate_distance");
            auto co  = config.get_view().get<bool>("complete_objects");
            out = (co && ecr > dd) ? ecr : dd;
        }
    }
    return out;
}

} // namespace Slic3r::Test

#endif // SLIC3R_TEST_DATA_HPP
