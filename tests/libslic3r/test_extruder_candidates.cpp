#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <libslic3r/ExtruderCandidates.hpp>
#include "Slic3r/Biz/Format/3mf.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "fff_print/test_data.hpp"


using Slic3r::Domain::Model;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::ModelInstance;
using Slic3r::Domain::VolumeSettings;
using Slic3r::Biz::Algorithms::TriangleMesh::make_cube;
using Slic3r::Biz::Algorithms::ModelObject::add_volume;
using Slic3r::Test::TestConfig;
using Slic3r::Biz::Slicing::get_extruder_candidates;
using Slic3r::Biz::Algorithms::TriangleSelector;
using Slic3r::Domain::TriangleSelector::TriangleStateType;
using Slic3r::Domain::BedInstance;
using Slic3r::Domain::SupportMode;
using CustomGCodeItem = Slic3r::Domain::CustomGCode::Item;
using CustomGCodeType = Slic3r::Domain::CustomGCode::Type;
using CustomGCodeInfo = Slic3r::Domain::CustomGCode::Info;
using CustomGCodeMode = Slic3r::Domain::CustomGCode::Mode;

const Slic3r::Domain::Bed bed;
const BedInstance bed_instance{bed};

struct ExtruderCandidatesTestFixture {
    Model model;
    ModelObject* object{model.add_object()};
    ModelVolume* volume{add_volume(object, make_cube(10, 10, 10))};
    ModelInstance* instance{object->add_instance()};
    TestConfig config{5};
};

TEST_CASE_METHOD(
    ExtruderCandidatesTestFixture,
    "Extruder candidates return expected extruders for extruders set in print settings",
    "[ExtruderCandidates]"
)
{
    config.print.items.opt("support_material").set(SupportMode::None);
    config.print.items.opt("perimeter_extruder").set(2);

    std::vector<unsigned> extruders{
        get_extruder_candidates(model, config, bed_instance, config.virtual_extruders)
    };
    CHECK(extruders == std::vector<unsigned>{0, 1});

    config.print.items.opt("infill_extruder").set(3);
    config.print.items.opt("solid_infill_extruder").set(4);
    extruders = get_extruder_candidates(model, config, bed_instance, config.virtual_extruders);
    CHECK(extruders == std::vector<unsigned>{1, 2, 3});
}

TEST_CASE_METHOD(
    ExtruderCandidatesTestFixture,
    "Extruder candidates return expected extruders for various object and volume settings",
    "[ExtruderCandidates]"
)
{
    config.print.items.opt("support_material").set(SupportMode::None);

    // It does not matter if the volumes are parts or modifiers, so there is no need
    // for a special "modifiers" test.

    std::vector<unsigned> extruders{
        get_extruder_candidates(model, config, bed_instance, config.virtual_extruders)
    };
    CHECK(extruders == std::vector<unsigned>{0});

    object->object_settings.items.opt("extruder").set(2);
    extruders = get_extruder_candidates(model, config, bed_instance, config.virtual_extruders);
    CHECK(extruders == std::vector<unsigned>{1});

    volume->volume_settings.overrides.set("extruder", 3);
    extruders = get_extruder_candidates(model, config, bed_instance, config.virtual_extruders);
    CHECK(extruders == std::vector<unsigned>{2});

    ModelObject* another_object{model.add_object()};
    ModelVolume* another_volume{add_volume(another_object, make_cube(10, 10, 10))};
    another_object->add_instance();

    extruders = get_extruder_candidates(model, config, bed_instance, config.virtual_extruders);
    CHECK(extruders == std::vector<unsigned>{0, 2});

    another_volume->volume_settings.overrides.set("extruder", 4);
    extruders = get_extruder_candidates(model, config, bed_instance, config.virtual_extruders);
    CHECK(extruders == std::vector<unsigned>{2, 3});

    another_volume->volume_settings.overrides.set("infill_extruder", 5);
    extruders = get_extruder_candidates(model, config, bed_instance, config.virtual_extruders);
    CHECK(extruders == std::vector<unsigned>{2, 3, 4});

    // Overriding all sub-extruders should override the usage of extruder 4 (index 3).
    another_volume->volume_settings.overrides.set("solid_infill_extruder", 5);
    another_volume->volume_settings.overrides.set("perimeter_extruder", 5);
    extruders = get_extruder_candidates(model, config, bed_instance, config.virtual_extruders);
    CHECK(extruders == std::vector<unsigned>{2, 4});
}

TEST_CASE_METHOD(
    ExtruderCandidatesTestFixture,
    "Extruder candidates ignore un-printable instances",
    "[ExtruderCandidates]"
)
{
    config.print.items.opt("support_material").set(SupportMode::None);

    std::vector<unsigned> extruders{
        get_extruder_candidates(model, config, bed_instance, config.virtual_extruders)
    };
    CHECK(extruders == std::vector<unsigned>{0});

    object->object_settings.items.opt("extruder").set(2);
    extruders = get_extruder_candidates(model, config, bed_instance, config.virtual_extruders);
    CHECK(extruders == std::vector<unsigned>{1});

    object->instances.front()->printable = false;
    extruders = get_extruder_candidates(model, config, bed_instance, config.virtual_extruders);
    CHECK(extruders == std::vector<unsigned>{});
}

TEST_CASE_METHOD(
    ExtruderCandidatesTestFixture,
    "Extruder candidates return expected extruders with layer config ranges",
    "[ExtruderCandidates]"
)
{
    const std::pair<double, double> range{0.0, 1.0};
    VolumeSettings range_settings;
    object->layer_config_ranges.insert({range, std::move(range_settings)});

    std::vector<unsigned> extruders{
        get_extruder_candidates(model, config, bed_instance, config.virtual_extruders)
    };
    CHECK(extruders == std::vector<unsigned>{0});

    object->layer_config_ranges.at(range).overrides.set("extruder", 3);

    extruders = get_extruder_candidates(model, config, bed_instance, config.virtual_extruders);
    CHECK(extruders == std::vector<unsigned>{0, 2});
}

TEST_CASE_METHOD(
    ExtruderCandidatesTestFixture,
    "Extruder candidates return expected extruders with supports enabled",
    "[ExtruderCandidates]"
)
{
    object->object_settings.items.opt("extruder").set(3);
    object->object_settings.overrides.set("support_material", SupportMode::Everywhere);

    std::vector<unsigned> extruders{
        get_extruder_candidates(model, config, bed_instance, config.virtual_extruders)
    };
    CHECK(extruders == std::vector<unsigned>{0, 2});

    object->object_settings.overrides.set("support_material_extruder", 4);
    extruders = get_extruder_candidates(model, config, bed_instance, config.virtual_extruders);
    CHECK(extruders == std::vector<unsigned>{0, 2, 3});

    object->object_settings.overrides.set("support_material_interface_extruder", 5);
    extruders = get_extruder_candidates(model, config, bed_instance, config.virtual_extruders);
    CHECK(extruders == std::vector<unsigned>{2, 3, 4});
}

TEST_CASE_METHOD(
    ExtruderCandidatesTestFixture,
    "Extruder candidates return expected extruders with multi material painting",
    "[ExtruderCandidates]"
)
{
    TriangleSelector selector{volume->mesh()};
    selector.set_facet(0, Slic3r::Domain::TriangleSelector::TriangleStateType::Extruder2);
    selector.set_facet(1, Slic3r::Domain::TriangleSelector::TriangleStateType::Extruder3);
    volume->mm_segmentation_facets.triangle_splitting_data = selector.serialize();

    std::vector<unsigned> extruders{
        get_extruder_candidates(model, config, bed_instance, config.virtual_extruders)
    };
    CHECK(extruders == std::vector<unsigned>{0, 1, 2});
}

TEST_CASE_METHOD(
    ExtruderCandidatesTestFixture,
    "Extruder candidates return expected extruders with custom gcodes",
    "[ExtruderCandidates]"
)
{
    config.print.items.opt("support_material").set(SupportMode::None);

    object->object_settings.items.opt("extruder").set(3);
    BedInstance instance{bed};
    instance.custom_gcode = CustomGCodeInfo{
        CustomGCodeMode::MultiExtruder,
        {CustomGCodeItem{.print_z = 5.0, .type = CustomGCodeType::ToolChange, .extruder = 2}}
    };
    std::vector<unsigned> extruders{
        get_extruder_candidates(model, config, instance, config.virtual_extruders)
    };
    CHECK(extruders == std::vector<unsigned>{1, 2});
}

TEST_CASE_METHOD(
    ExtruderCandidatesTestFixture,
    "Extruder candidates skip the default extruder of a fully painted volume",
    "[ExtruderCandidates]"
)
{
    config.print.items.opt("support_material").set(SupportMode::None);
    object->object_settings.items.opt("extruder").set(3);

    TriangleSelector selector{volume->mesh()};
    for (size_t facet_idx = 0; facet_idx < volume->mesh().facets_count(); ++facet_idx) {
        selector.set_facet(static_cast<int>(facet_idx), TriangleStateType::Extruder2);
    }

    volume->mm_segmentation_facets.triangle_splitting_data = selector.serialize();

    std::vector<unsigned> extruders =
        get_extruder_candidates(model, config, bed_instance, config.virtual_extruders);
    CHECK(extruders == std::vector<unsigned>{1});
}

TEST_CASE_METHOD(
    ExtruderCandidatesTestFixture,
    "Extruder candidates keep the default extruder of a partially painted volume",
    "[ExtruderCandidates]"
)
{
    config.print.items.opt("support_material").set(SupportMode::None);
    object->object_settings.items.opt("extruder").set(3);

    // Paint all facets of the cube except the first one.
    TriangleSelector selector{volume->mesh()};
    for (size_t facet_idx = 1; facet_idx < volume->mesh().facets_count(); ++facet_idx) {
        selector.set_facet(static_cast<int>(facet_idx), TriangleStateType::Extruder2);
    }

    volume->mm_segmentation_facets.triangle_splitting_data = selector.serialize();

    std::vector<unsigned> extruders =
        get_extruder_candidates(model, config, bed_instance, config.virtual_extruders);
    CHECK(extruders == std::vector<unsigned>{1, 2});
}

TEST_CASE_METHOD(
    ExtruderCandidatesTestFixture,
    "Extruder candidates keep the default extruder of a fully painted volume when the painting extruder does not exist",
    "[ExtruderCandidates]"
)
{
    // Facets painted with an extruder exceeding the number of extruders are printed with the default extruder of the volume.
    TestConfig small_config{2};
    small_config.print.items.opt("support_material").set(SupportMode::None);
    object->object_settings.items.opt("extruder").set(2);

    TriangleSelector selector{volume->mesh()};
    for (size_t facet_idx = 0; facet_idx < volume->mesh().facets_count(); ++facet_idx) {
        selector.set_facet(static_cast<int>(facet_idx), TriangleStateType::Extruder3);
    }

    volume->mm_segmentation_facets.triangle_splitting_data = selector.serialize();

    std::vector<unsigned> extruders =
        get_extruder_candidates(model, small_config, bed_instance, small_config.virtual_extruders);
    CHECK(extruders == std::vector<unsigned>{1});
}
