#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <sstream>
#include <fstream>

#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/ModelVolume.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "libslic3r/GCode.hpp"
#include "test_data.hpp"
#include "Slic3r/Biz/Slicing/BackgroundProcess.hpp"

using namespace Slic3r;
using namespace Test;
using namespace Catch;

using Biz::GCodeReader::GCodeReader;
using Biz::Algorithms::ModelObject::add_volume;
using Biz::Algorithms::ModelObject::ensure_on_bed;
using Biz::Algorithms::ModelVolume::translate;
using Biz::Slicing::SerializedConfig;
using Domain::Preset::HwPrinterConfig;

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
        [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
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
    Domain::Model &model, const std::string &name, const int extruder, const Vec3d &offset = Vec3d::Zero()
) {
    std::string extruder_id{std::to_string(extruder)};
    Domain::ModelObject *object = model.add_object();
    object->name = name;
    Domain::ModelVolume *volume = add_volume(object, Test::mesh(Test::TestMesh::cube_20x20x20));
    translate(*volume, offset);

    Domain::VolumeSettings volume_settings;
    volume_settings.overrides.set("extruder", extruder);

    volume->volume_settings = volume_settings;
    object->add_instance();
    ensure_on_bed(*object);
}

class CancelObjectFixture
{
public:
    CancelObjectFixture() {
        config.printer.items.opt("gcode_flavor").set(Domain::GCodeFlavor::gcfMarlinFirmware);
        config.print.items.opt("gcode_label_objects").set(Domain::LabelObjectsStyle::Firmware);
        config.print.items.opt("gcode_comments").set(true);
        config.printer.items.opt("use_relative_e_distances").set(true);
        config.print.items.opt("wipe").set(false);
        config.print.items.opt("skirts").set(0);

        add_object(two_cubes, "no_offset_cube", 0);
        add_object(two_cubes, "offset_cube", 0, {30.0, 0.0, 0.0});

        add_object(multimaterial_cubes, "no_offset_cube", 1);
        add_object(multimaterial_cubes, "offset_cube", 2, {30.0, 0.0, 0.0});

        retract_length = config.print.items.opt("retract_length").get<double>();
        retract_length_toolchange =
            config.print.items.opt("retract_length_toolchange").get<double>();

    }

    TestConfig config;
    Domain::Bed model_bed;
    Domain::BedInstance bed_instance{model_bed};

    Domain::Model two_cubes;
    Domain::Model multimaterial_cubes;

    double retract_length{};
    double retract_length_toolchange{};
};

TEST_CASE_METHOD(CancelObjectFixture, "Single extruder", "[CancelObject]") {
    for (const Domain::ModelObject* object : two_cubes.objects) {
        for (Domain::ModelInstance* instance : object->instances) {
            bed_instance.model_instances.push_back(instance);
        }
    }

    Print print;
    auto preset_metadata = create_dummy_selected_preset_metadata(create_dummy_hw_config());
    auto metadata = Biz::Slicing::build_gcode_metadata({}, preset_metadata, config);

    print.update(
        two_cubes,
        config,
        bed_instance,
        preset_metadata,
        Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config)
    );
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
    config.print.items.opt("complete_objects").set(true);

    for (const Domain::ModelObject* object : two_cubes.objects) {
        for (Domain::ModelInstance* instance : object->instances) {
            bed_instance.model_instances.push_back(instance);
        }
    }

    Print print;
    auto preset_metadata = create_dummy_selected_preset_metadata(create_dummy_hw_config());
    auto metadata        = Biz::Slicing::build_gcode_metadata({}, preset_metadata, config);

    print.update(
        two_cubes,
        config,
        bed_instance,
        preset_metadata,
        Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config)
    );
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
