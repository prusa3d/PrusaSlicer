#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <sstream>
#include <fstream>

#include "libslic3r/GCode.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Test;
using namespace Catch;

constexpr bool debug_files{false};

std::string remove_object(const std::string &gcode, const int id) {
    std::string result{gcode};
    std::string start_token{"M486 S" + std::to_string(id) + "\n"};
    std::string end_token{"M486 S-1\n"};

    std::size_t start{result.find(start_token)};

    while (start != std::string::npos) {
        std::size_t end_token_start{result.find(end_token, start)};
        std::size_t end{end_token_start + end_token.size()};
        result.replace(start, end - start, "");
        start = result.find(start_token);
    }
    return result;
}

TEST_CASE("Remove object sanity check", "[CancelObject]") {
    // clang-format off
    const std::string gcode{
        "the\n"
        "M486 S2\n"
        "to delete\n"
        "M486 S-1\n"
        "kept\n"
        "M486 S2\n"
        "to also delete\n"
        "M486 S-1\n"
        "lines\n"
    };
    // clang-format on

    const std::string result{remove_object(gcode, 2)};

    // clang-format off
    CHECK(result == std::string{
        "the\n"
        "kept\n"
        "lines\n"
    });
    // clang-format on
}

void check_retraction(const std::string &gcode, double offset = 0.0) {
    GCodeReader parser;
    std::map<int, double> retracted;
    unsigned count{0};
    std::set<int> there_is_unretract;
    int extruder_id{0};

    parser.parse_buffer(
        gcode,
        [&](Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {
            INFO("Line number: " + std::to_string(++count));
            INFO("Extruder id: " + std::to_string(extruder_id));
            if (!line.raw().empty() && line.raw().front() == 'T') {
                extruder_id = std::stoi(std::string{line.raw().back()});
            }
            if (line.dist_XY(self) < std::numeric_limits<double>::epsilon()) {
                if (line.has_e() && line.e() < 0) {
                    retracted[extruder_id] += line.e();
                }
                if (line.has_e() && line.e() > 0) {
                    INFO("Line: " + line.raw());
                    if (there_is_unretract.count(extruder_id) == 0) {
                        there_is_unretract.insert(extruder_id);
                        REQUIRE(retracted[extruder_id] + offset + line.e() == Approx(0.0));
                    } else {
                        REQUIRE(retracted[extruder_id] + line.e() == Approx(0.0));
                    }
                    retracted[extruder_id] = 0.0;
                }
            }
        }
    );
}

void add_object(
    Model &model, const std::string &name, const int extruder, const Vec3d &offset = Vec3d::Zero()
) {
    std::string extruder_id{std::to_string(extruder)};
    ModelObject *object = model.add_object();
    object->name = name;
    ModelVolume *volume = object->add_volume(Test::mesh(Test::TestMesh::cube_20x20x20));
    volume->set_material_id("material" + extruder_id);
    volume->translate(offset);
    DynamicPrintConfig config;
    config.set_deserialize_strict({
        {"extruder", extruder_id},
    });
    volume->config.assign_config(config);
    object->add_instance();
    object->ensure_on_bed();
}

class CancelObjectFixture
{
public:
    CancelObjectFixture() {
        config.set_deserialize_strict({
            {"gcode_flavor", "marlin2"},
            {"gcode_label_objects", "firmware"},
            {"gcode_comments", "1"},
            {"use_relative_e_distances", "1"},
            {"wipe", "0"},
            {"skirts", "0"},
        });

        add_object(two_cubes, "no_offset_cube", 0);
        add_object(two_cubes, "offset_cube", 0, {30.0, 0.0, 0.0});

        add_object(multimaterial_cubes, "no_offset_cube", 1);
        add_object(multimaterial_cubes, "offset_cube", 2, {30.0, 0.0, 0.0});

        retract_length = config.option<ConfigOptionFloats>("retract_length")->get_at(0);
        retract_length_toolchange = config.option<ConfigOptionFloats>("retract_length_toolchange")
                                        ->get_at(0);
    }

    DynamicPrintConfig config{Slic3r::DynamicPrintConfig::full_print_config()};

    Model two_cubes;
    Model multimaterial_cubes;

    double retract_length{};
    double retract_length_toolchange{};
};

TEST_CASE_METHOD(CancelObjectFixture, "Single extruder", "[CancelObject]") {
    Print print;
    print.apply(two_cubes, config);
    print.validate();
    const std::string gcode{Test::gcode(print)};

    if constexpr (debug_files) {
        std::ofstream output{"single_extruder_two.gcode"};
        output << gcode;
    }

    SECTION("One remaining") {
        const std::string removed_object_gcode{remove_object(gcode, 0)};
        REQUIRE(removed_object_gcode.find("M486 S1\n") != std::string::npos);
        if constexpr (debug_files) {
            std::ofstream output{"single_extruder_one.gcode"};
            output << removed_object_gcode;
        }

        check_retraction(removed_object_gcode);
    }

    SECTION("All cancelled") {
        const std::string removed_all_gcode{remove_object(remove_object(gcode, 0), 1)};

        // First retraction is not compensated - set offset.
        check_retraction(removed_all_gcode, retract_length);
    }
}

TEST_CASE_METHOD(CancelObjectFixture, "Sequential print", "[CancelObject]") {
    config.set_deserialize_strict({{"complete_objects", 1} });

    Print print;
    print.apply(two_cubes, config);
    print.validate();
    const std::string gcode{Test::gcode(print)};

    if constexpr (debug_files) {
        std::ofstream output{"sequential_print_two.gcode"};
        output << gcode;
    }

    SECTION("One remaining") {
        const std::string removed_object_gcode{remove_object(gcode, 0)};
        REQUIRE(removed_object_gcode.find("M486 S1\n") != std::string::npos);
        if constexpr (debug_files) {
            std::ofstream output{"sequential_print_one.gcode"};
            output << removed_object_gcode;
        }

        check_retraction(removed_object_gcode);
    }

    SECTION("All cancelled") {
        const std::string removed_all_gcode{remove_object(remove_object(gcode, 0), 1)};

        // First retraction is not compensated - set offset.
        check_retraction(removed_all_gcode, retract_length);
    }
}
