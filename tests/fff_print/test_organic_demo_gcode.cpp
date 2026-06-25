// Tests for organic/tree supports with variable layer height.
// Also generates G-code files for visual inspection.
// Build: ninja -j$(nproc) fff_print_tests
// Run:   ./tests/fff_print/fff_print_tests "[DemoGcode]"

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

#include "libslic3r/GCodeReader.hpp"
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/fstream.hpp>
#include "libslic3r/Layer.hpp"
#include "test_data.hpp"

using namespace Slic3r::Test;
using namespace Slic3r;

static void write_gcode_file(const std::string &path, Print &print) {
    boost::filesystem::path temp = boost::filesystem::unique_path();
    print.export_gcode(temp.string(), nullptr, nullptr);
    std::ifstream src(temp.string(), std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
    src.close();
    boost::filesystem::remove(temp);
    std::ofstream f(path);
    f << content;
}

static TriangleMesh load_bridge_mesh() {
    TriangleMesh mesh;
    boost::filesystem::path path{TEST_DATA_DIR};
    path /= "bridge_overhang.stl";
    mesh.ReadSTLFile(path.string().c_str());
    return mesh;
}

static bool support_layers_align_with_object(const PrintObject *obj) {
    for (const SupportLayer *sl : obj->support_layers()) {
        bool found = false;
        for (const Layer *ol : obj->layers())
            if (std::abs(sl->print_z - ol->print_z) < EPSILON) {
                found = true;
                break;
            }
        if (!found)
            return false;
    }
    return true;
}

static bool has_variable_layer_heights(const PrintObject *obj) {
    double prev_h = 0;
    for (const Layer *l : obj->layers()) {
        if (prev_h > 0 && std::abs(l->height - prev_h) > 0.01)
            return true;
        prev_h = l->height;
    }
    return false;
}
// Set a variable layer height profile on the model object and create the print.
// The profile must be set BEFORE print.apply() for it to take effect.
static void init_print_with_variable_profile(
    TriangleMesh &mesh,
    Print &print,
    Model &model,
    std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items,
    double obj_height
) {
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict(config_items);

    ModelObject *mo = model.add_object();
    mo->name += "object.stl";
    mo->add_volume(mesh);
    mo->add_instance();
    model.center_instances_around_point({100, 100});
    for (ModelObject *o : model.objects)
        o->ensure_on_bed();
    print.auto_assign_extruders(mo);

    // Set variable layer height via layer_config_ranges (table-based modifier).
    // This is simpler than layer_height_profile and doesn't require the last
    // z to match the exact object height.
    double mid = obj_height * 0.7;
    DynamicPrintConfig range_cfg;
    range_cfg.set_key_value("layer_height", new ConfigOptionFloat(0.1));
    mo->layer_config_ranges[{mid, obj_height}].apply(range_cfg);

    print.apply(model, config);
    print.validate();
    print.set_status_silent();
}

TEST_CASE("Demo: organic uniform (baseline)", "[DemoGcode]") {
    TriangleMesh mesh = load_bridge_mesh();
    Print print;
    Model model;
    init_print(
        {mesh}, print, model,
        {
            {"support_material", 1},
            {"support_material_style", "organic"},
            {"support_material_threshold", 0},
            {"layer_height", 0.2},
            {"first_layer_height", 0.2},
            {"dont_support_bridges", 0},
            {"gcode_comments", 1},
        }
    );
    print.process();
    write_gcode_file("demo_gcode/built_organic_uniform.gcode", print);

    const PrintObject *obj = print.objects().front();
    REQUIRE(!obj->support_layers().empty());
    REQUIRE(obj->support_layers().back()->print_z > 15.0);
    REQUIRE(!has_variable_layer_heights(obj));
}

TEST_CASE("Demo: organic variable layer height (the fix)", "[DemoGcode]") {
    TriangleMesh mesh = load_bridge_mesh();
    double obj_height = mesh.bounding_box().size().z();

    Print print;
    Model model;
    init_print_with_variable_profile(
        mesh, print, model,
        {
            {"support_material", 1},
            {"support_material_style", "organic"},
            {"support_material_threshold", 0},
            {"layer_height", 0.2},
            {"first_layer_height", 0.2},
            {"dont_support_bridges", 0},
            {"gcode_comments", 1},
        },
        obj_height
    );
    print.process();
    write_gcode_file("demo_gcode/built_organic_variable.gcode", print);

    const PrintObject *obj = print.objects().front();
    REQUIRE(!obj->support_layers().empty());
    REQUIRE(obj->support_layers().back()->print_z > 15.0);
    REQUIRE(has_variable_layer_heights(obj));
    REQUIRE(support_layers_align_with_object(obj));
    REQUIRE(obj->layers().size() > size_t(obj_height / 0.2));
}

TEST_CASE("Demo: tree variable layer height", "[DemoGcode]") {
    TriangleMesh mesh = load_bridge_mesh();
    double obj_height = mesh.bounding_box().size().z();

    Print print;
    Model model;
    init_print_with_variable_profile(
        mesh, print, model,
        {
            {"support_material", 1},
            {"support_material_style", "tree"},
            {"support_material_threshold", 0},
            {"layer_height", 0.2},
            {"first_layer_height", 0.2},
            {"dont_support_bridges", 0},
            {"gcode_comments", 1},
        },
        obj_height
    );
    print.process();
    write_gcode_file("demo_gcode/built_tree_variable.gcode", print);

    const PrintObject *obj = print.objects().front();
    REQUIRE(!obj->support_layers().empty());
    REQUIRE(obj->support_layers().back()->print_z > 15.0);
    REQUIRE(has_variable_layer_heights(obj));
    REQUIRE(support_layers_align_with_object(obj));
}

TEST_CASE("Demo: snug variable layer height (classic, for comparison)", "[DemoGcode]") {
    TriangleMesh mesh = load_bridge_mesh();
    double obj_height = mesh.bounding_box().size().z();

    Print print;
    Model model;
    init_print_with_variable_profile(
        mesh, print, model,
        {
            {"support_material", 1},
            {"support_material_style", "snug"},
            {"support_material_threshold", 0},
            {"layer_height", 0.2},
            {"first_layer_height", 0.2},
            {"dont_support_bridges", 0},
            {"gcode_comments", 1},
        },
        obj_height
    );
    print.process();
    write_gcode_file("demo_gcode/built_snug_variable.gcode", print);

    const PrintObject *obj = print.objects().front();
    REQUIRE(!obj->support_layers().empty());
    REQUIRE(obj->support_layers().back()->print_z > 15.0);
    REQUIRE(has_variable_layer_heights(obj));
}

static std::unique_ptr<Print> process_3mf_and_export(
    const std::string &input_3mf, const std::string &output_gcode
) {
    DynamicPrintConfig config;
    auto print = std::make_unique<Print>();
    Model model;
    ConfigSubstitutionContext context{ForwardCompatibilitySubstitutionRule::Disable};
    boost::optional<Semver> version;
    load_3mf(input_3mf.c_str(), config, context, &model, false, version);
    config.set_key_value("binary_gcode", new ConfigOptionBool(false));
    init_print(std::vector<TriangleMesh>{}, *print, model, config);
    print->apply(model, print->full_print_config());
    print->process();
    write_gcode_file(output_gcode, *print);
    return print;
}

TEST_CASE("Demo: issue #9462 - organic supports + variable layer height", "[DemoGcode]") {
    boost::filesystem::path path{TEST_DATA_DIR};
    path /= "issue9462_organic_variable.3mf";
    auto print = process_3mf_and_export(path.string(), "demo_gcode/issue9462_organic_variable.gcode");

    const PrintObject *obj = print->objects().front();
    REQUIRE(!obj->support_layers().empty());
    REQUIRE(has_variable_layer_heights(obj));
    REQUIRE(obj->support_layers().back()->print_z > 50.0);
}

TEST_CASE("Demo: issue #12014 - organic supports + variable layer height", "[DemoGcode]") {
    boost::filesystem::path path{TEST_DATA_DIR};
    path /= "issue12014_organic_variable.3mf";
    auto print =
        process_3mf_and_export(path.string(), "demo_gcode/issue12014_organic_variable.gcode");

    const PrintObject *obj = print->objects().front();
    REQUIRE(!obj->support_layers().empty());
    REQUIRE(has_variable_layer_heights(obj));
    REQUIRE(obj->support_layers().back()->print_z > 20.0);
}
