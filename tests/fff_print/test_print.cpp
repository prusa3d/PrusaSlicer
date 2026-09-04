#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Print.hpp"
#include "libslic3r/Layer.hpp"
#include "Slic3r/Biz/GCodeReader/GCodeReader.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"

#include "test_data.hpp"
#include "Slic3r/Biz/Slicing/BackgroundProcess.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;
using Biz::GCodeReader::GCodeReader;
using Domain::Percentage;
using Domain::FloatOrPercentage;
using Biz::Slicing::SerializedConfig;
using Domain::Preset::HwPrinterConfig;
using Slic3r::Domain::EnumWrapper;
using Slic3r::Domain::EnumVectorWrapper;
using Domain::FullConfig;
using Domain::ConfigBox;
using Domain::ConfigItem;
using Domain::SlicingId;
using Domain::ConfigPackSLA;
using Biz::Slicing::IThumbnailImageGenerator;
using Biz::Slicing::ThumbnailImageResults;
using Biz::Slicing::ThumbnailImageRequests;
using Domain::TriangleMesh;
using Biz::Algorithms::ModelObject::add_volume;
using Domain::BrimType;
namespace BB = Biz::Algorithms::BoundingBox;

SCENARIO("PrintObject: Perimeter generation", "[PrintObject]") {
    GIVEN("20mm cube and default config") {
        WHEN("make_perimeters() is called")  {
            Slic3r::Print print;
            TestConfig config;
            config.print.items.opt("fill_density").set(Percentage{0});
            Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, config);
			const PrintObject &object = *print.objects().front();
			THEN("67 layers exist in the model") {
                REQUIRE(object.layers().size() == 66);
            }
            THEN("Every layer in region 0 has 1 island of perimeters") {
                for (const Layer *layer : object.layers())
                    REQUIRE(layer->regions().front()->perimeters().size() == 1);
            }
            THEN("Every layer in region 0 has 3 paths in its perimeters list.") {
                for (const Layer *layer : object.layers())
                    REQUIRE(layer->regions().front()->perimeters().items_count() == 3);
            }
        }
    }
}

SCENARIO("Print: Skirt generation", "[Print]") {
    GIVEN("20mm cube and default config") {
        WHEN("Skirts is set to 2 loops")  {
            Slic3r::Print print;
            TestConfig config;
            config.print.items.opt("skirt_height").set(1 );
            config.print.items.opt("skirt_distance").set(1.0);
            config.print.items.opt("skirts").set(2 );
            Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, config);
            THEN("Skirt Extrusion collection has 2 loops in it") {
                REQUIRE(print.skirt().items_count() == 2);
                REQUIRE(print.skirt().flatten().entities.size() == 2);
            }
        }
    }
}

namespace {
void update(Print& print, Domain::Model& model, const TestConfig& config)
{
    Domain::Bed model_bed;
    Domain::BedInstance bed_instance{model_bed};
    for (const Domain::ModelObject* object : model.objects) {
        for (Domain::ModelInstance* instance : object->instances) {
            bed_instance.model_instances.push_back(instance);
        }
    }
    auto preset_metadata = create_dummy_selected_preset_metadata(
        create_dummy_hw_config(config.tool.size())
    );
    auto metadata = Biz::Slicing::build_gcode_metadata({}, preset_metadata, config);
    print.update(
        model,
        config,
        bed_instance,
        preset_metadata,
        Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config)
    );
}
} // namespace

SCENARIO("Print: Changing number of solid surfaces does not cause all surfaces to become internal.", "[Print]") {
    GIVEN("sliced 20mm cube and config with top_solid_surfaces = 2 and bottom_solid_surfaces = 1") {
        TestConfig config;
        config.print.items.opt("top_solid_layers").set(2);
        config.print.items.opt("bottom_solid_layers").set(1);
        config.print.items.opt("layer_height").set(0.25);
        config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.25});
        Slic3r::Print print;
        Domain::Model model;
        Slic3r::Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
        // Precondition: Ensure that the model has 2 solid top layers (39, 38)
        // and one solid bottom layer (0).
		auto test_is_solid_infill = [&print](size_t obj_id, size_t layer_id) {
		    const Layer &layer = *print.objects()[obj_id]->get_layer((int)layer_id);
		    // iterate over all of the regions in the layer
		    for (const LayerRegion *region : layer.regions()) {
		        // for each region, iterate over the fill surfaces
		        for (const Surface &surface : region->fill_surfaces())
		            CHECK(surface.is_solid());
		    }
		};
        print.process();
        test_is_solid_infill(0,  0); // should be solid
        test_is_solid_infill(0, 79); // should be solid
        test_is_solid_infill(0, 78); // should be solid
        WHEN("Model is re-sliced with top_solid_layers == 3") {
			config.print.items.opt("top_solid_layers").set(3);
            update(print, model, config);
            print.process();
            THEN("Print object does not have 0 solid bottom layers.") {
                test_is_solid_infill(0, 0);
            }
            AND_THEN("Print object has 3 top solid layers") {
                test_is_solid_infill(0, 79);
                test_is_solid_infill(0, 78);
                test_is_solid_infill(0, 77);
            }
        }
    }
}

SCENARIO("Print: Brim generation", "[Print]") {
    GIVEN("20mm cube and default config, 1mm first layer width") {
        WHEN("Brim is set to 3mm")  {
	        Slic3r::Print print;
            TestConfig config;
            config.print.items.opt("first_layer_extrusion_width").set(FloatOrPercentage{1.0});
            config.print.items.opt("brim_type").set(BrimType::OuterOnly);
            config.print.items.opt("brim_width").set(3.0);
	        Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, config);
            THEN("Brim Extrusion collection has 3 loops in it") {
                REQUIRE(print.brim().items_count() == 3);
            }
        }
        WHEN("Brim is set to 6mm")  {
	        Slic3r::Print print;
            TestConfig config;
            config.print.items.opt("first_layer_extrusion_width").set(FloatOrPercentage{1.0});
            config.print.items.opt("brim_type").set(BrimType::OuterOnly);
            config.print.items.opt("brim_width").set(6.0);
	        Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, config);
            THEN("Brim Extrusion collection has 6 loops in it") {
                REQUIRE(print.brim().items_count() == 6);
            }
        }
        WHEN("Brim is set to 6mm, extrusion width 0.5mm")  {
	        Slic3r::Print print;
            TestConfig config;
            config.print.items.opt("first_layer_extrusion_width").set(FloatOrPercentage{0.5});
            config.print.items.opt("brim_type").set(BrimType::OuterOnly);
            config.print.items.opt("brim_width").set(6.0);
	        Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, config);
			print.process();
            THEN("Brim Extrusion collection has 12 loops in it") {
                REQUIRE(print.brim().items_count() == 14);
            }
        }
    }
}

TEST_CASE("Ported from Perl", "[Print]") {
    GIVEN("20mm cube") {
        WHEN("Print center is set to 100x100 (test framework default)")  {
            std::string gcode = Slic3r::Test::slice({ TestMesh::cube_20x20x20 }, TestConfig{});
            GCodeReader parser;
            Points      extrusion_points;
            parser.parse_buffer(gcode, [&extrusion_points](GCodeReader &self, const GCodeReader::GCodeLine &line)
            {
                if (line.cmd_is("G1") && line.extruding(self) && line.dist_XY(self) > 0)
                    extrusion_points.emplace_back(line.new_XY_scaled(self));
            });
            Vec2d center = unscaled<double>(BB::center(BB::construct(extrusion_points)));
            THEN("print is centered around print_center") {
                REQUIRE(is_approx(center.x(), 100.));
                REQUIRE(is_approx(center.y(), 100.));
            }
        }
    }
    GIVEN("Model with multiple objects") {
        TestConfig config{4, 0.4};
        Print print;
        Domain::Model model;
        Slic3r::Test::init_print({ TestMesh::cube_20x20x20 }, print, model, config);

        // User sets a per-region option, also testing a deep copy of Model.
        Domain::Model model2(model);
        model2.objects.front()->object_settings.overrides.set("fill_density", Percentage{100});
        WHEN("fill_density overridden") {
            update(print, model2, config);
            THEN("region config inherits model object config") {
                REQUIRE(print.get_print_region(0).config().get<std::vector<Percentage>>("fill_density").at(0).value == Percentage{100}.value);
            }
        }

        model2.objects.front()->object_settings.overrides.disable("fill_density");
        WHEN("fill_density resetted") {
            update(print, model2, config);
            THEN("region config is resetted") {
                REQUIRE(print.get_print_region(0).config().get<std::vector<Percentage>>("fill_density").at(0) == Percentage{20});
            }
        }

        WHEN("extruder is assigned") {
            model2.objects.front()->object_settings.items.opt("extruder").set(3);
            model2.objects.front()->object_settings.overrides.set("perimeter_extruder", 2);
            update(print, model2, config);
            THEN("extruder setting is correctly expanded") {
                REQUIRE(print.get_print_region(0).config().get<int>("infill_extruder") == 3);
            }

            THEN("extruder setting does not override explicitely specified extruders") {
                REQUIRE(print.get_print_region(0).config().get<int>("perimeter_extruder") == 2);
            }
        }
    }
}

const Domain::overloaded change_value{
    [](EnumWrapper& v)
    {
        ASSERT(v.def().size() > 1);
        const std::size_t index{v.index_of_value(v.value())};
        if (index == 0) {
            v.set_index(1);
        } else {
            v.set_index(0);
        }
    },
    [](bool& v) { v = !v; },
    [](int& v) { v += 1; },
    [](std::optional<int>& v)
    {
        if (v) {
            *v += 1;
        } else {
            v = 1;
        }
    },
    [](double& v) { v += 1.0; },
    [](std::string& v) { v = "changed"; },
    [](Domain::Vec2d& v)
    {
        v[0] += 1.0;
        v[1] += 1.0;
    },
    [](FloatOrPercentage& v) { v = FloatOrPercentage{1.23}; },
    [](Percentage& v) { v.value += 1.0; },
    [](EnumVectorWrapper& v)
    {
        ASSERT(v.def().size() > 1);
        const std::vector<std::size_t> indicies{v.get_indexes()};
        std::vector<std::size_t> new_indicies;
        std::ranges::transform(
            indicies,
            std::back_inserter(new_indicies),
            [](std::size_t index)
            {
                if (index == 0) {
                    return 1;
                } else {
                    return 0;
                }
            }
        );
        v.set_indexes(new_indicies);
    },
    [](std::vector<bool>& v)
    {
        if (!v.empty()) {
            v[0] = !v[0];
        } else {
            v.push_back(true);
        }
    },
    [](std::vector<int>& v)
    {
        if (!v.empty()) {
            v[0] += 1;
        } else {
            v.push_back(1);
        }
    },
    [](std::vector<std::optional<int>>& v)
    {
        if (!v.empty()) {
            v[0] = 42;
        } else {
            v.push_back(42);
        }
    },
    [](std::vector<double>& v)
    {
        if (!v.empty()) {
            v[0] += 1.0;
        } else {
            v.push_back(1.0);
        }
    },
    [](std::vector<std::string>& v)
    {
        if (!v.empty()) {
            v[0] = "changed";
        } else {
            v.push_back("changed");
        }
    },
    [](std::vector<Domain::Vec2d>& v)
    {
        if (!v.empty()) {
            v[0][0] += 1.0;
            v[0][1] += 1.0;
        } else {
            v.push_back(Domain::Vec2d(1.0, 1.0));
        }
    },
    [](std::vector<FloatOrPercentage>& v)
    {
        if (!v.empty()) {
            v[0] = FloatOrPercentage{2.34};
        } else {
            v.push_back(FloatOrPercentage{2.34});
        }
    },
    [](std::vector<Percentage>& v)
    {
        if (!v.empty()) {
            v[0].value += 1.0;
        } else {
            v.push_back(Percentage{1.0});
        }
    }
};

TEST_CASE("Changing all config values works", "[PrintApply]")
{
    TestConfig config{4};

    Print print;
    Domain::Model model;
    Slic3r::Test::init_print({TestMesh::cube_20x20x20}, print, model, config);

    auto full_config_result{prepare_slicing_input(config, {}, config.hw_config)};
    REQUIRE(full_config_result.has_value());
    auto& full_config{*full_config_result};
    auto preset_metadata =
        create_dummy_selected_preset_metadata(create_dummy_hw_config(config.tool.size()));
    auto metadata = Biz::Slicing::build_gcode_metadata({}, preset_metadata, config);

    auto apply_status{print.apply(
        model,
        full_config,
        preset_metadata,
        Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config),
        Domain::ModelWipeTower{},
        std::nullopt,
        {0}
    )};
    REQUIRE(std::holds_alternative<Biz::Slicing::ApplyStatus::Unchanged>(apply_status));

    std::vector<ConfigBox*> boxes{&config.print, &config.printer, &config.project};
    for (auto& tool : config.tool) {
        boxes.push_back(&tool);
    }
    for (auto& filament : config.filament) {
        boxes.push_back(&filament);
    }

    for (ConfigBox* box : boxes) {
        for (ConfigItem& item : box->items.all_items()) {
            // Skip shrinkage compensation as it modifies the model.
            if (item.name() == "filament_shrinkage_compensation_z"
                || item.name() == "filament_shrinkage_compensation_xy")
            {
                continue;
            }
            item.visit(change_value);
        }
        for (ConfigItem& item : box->overrides.all_items()) {
            item.visit(change_value);
        }
    }

    full_config_result = prepare_slicing_input(config, {}, config.hw_config);
    REQUIRE(full_config_result.has_value());
    full_config = *full_config_result;

    apply_status = print.apply(
        model,
        full_config,
        preset_metadata,
        Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config),
        Domain::ModelWipeTower{},
        std::nullopt,
        {0}
    );
    REQUIRE(std::holds_alternative<Biz::Slicing::ApplyStatus::Changed>(apply_status));
}

class ThumbnailGenerator : public Biz::Slicing::IThumbnailImageGenerator
{
    virtual std::future<Biz::Slicing::ThumbnailImageResults> enqueue_thumbnail_requests(
        const Slic3r::Biz::Slicing::ThumbnailImageRequests& requests
    ) override
    {
        std::promise<Biz::Slicing::ThumbnailImageResults> promise;
        promise.set_value(Biz::Slicing::ThumbnailImageResults{});
        return promise.get_future();
    }

    void handle_enqueued_requests() override {}
};

using Step = std::variant<FDMPrintStep, FDMPrintObjectStep>;

// Check that all other steps are done (hence the exclusive in the name).
// Check only the first object.
bool is_exclusively_undone(const Print& print, const std::set<Step>& steps)
{
    for (std::size_t i{}; i < FDMPrintStep::psCount; ++i) {
        const FDMPrintStep step{static_cast<FDMPrintStep>(i)};
        if (steps.contains(step)) {
            if (print.is_step_done(step)) {
                return false;
            }
            continue;
        }
        if (!print.is_step_done(step)) {
            return false;
        }
    }

    ASSERT(print.objects().size() == 1);
    const PrintObject& object{*print.objects().front()};

    for (std::size_t i{}; i < FDMPrintObjectStep::posCount; ++i) {
        const FDMPrintObjectStep step{static_cast<FDMPrintObjectStep>(i)};
        if (steps.contains(step)) {
            if (object.is_step_done(step)) {
                return false;
            }
            continue;
        }
        if (!object.is_step_done(step)) {
            return false;
        }
    }

    return true;
}

struct ApplyTestFixture
{
    TestConfig config{1};
    Print print{};
    Domain::Model model;

    ApplyTestFixture()
    {
        Slic3r::Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
    }
};

void apply_and_check(
    ApplyTestFixture& context,
    const std::function<void(TestConfig&)>& modify_config,
    const std::set<Step>& expected_undone
)
{
    ThumbnailGenerator thumbnail_generator{};
    Biz::Slicing::SerializedConfig serialized_config{};
    HwPrinterConfig hw_config{create_dummy_hw_config()};

    context.print.slice(SlicingId{0, 0}, thumbnail_generator, std::nullopt);
    REQUIRE(is_exclusively_undone(context.print, {}));

    modify_config(context.config);
    const auto slicing_input{prepare_slicing_input(context.config, {}, hw_config)};
    REQUIRE(slicing_input);
    const auto full_config{*slicing_input};
    auto preset_metadata =
        create_dummy_selected_preset_metadata(hw_config);
    auto metadata = Biz::Slicing::build_gcode_metadata({}, preset_metadata, context.config);


    const auto apply_status{context.print.apply(
        context.model,
        full_config,
        preset_metadata,
        Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, context.config),
        Domain::ModelWipeTower{},
        std::nullopt,
        {0}
    )};
    REQUIRE(std::holds_alternative<Biz::Slicing::ApplyStatus::Changed>(apply_status));
    REQUIRE(is_exclusively_undone(context.print, expected_undone));
}

TEST_CASE_METHOD(ApplyTestFixture, "Apply invalidates correct steps - silent_mode", "[PrintApply]")
{
    apply_and_check(
        *this,
        [](TestConfig& c) { c.printer.items.opt("silent_mode").set<bool>(false); },
        {psGCodeExport}
    );
}

TEST_CASE_METHOD(
    ApplyTestFixture,
    "Apply invalidates correct steps - brim separation",
    "[PrintApply]"
)
{
    apply_and_check(
        *this,
        [](TestConfig& c) { c.print.items.opt("brim_separation").set<double>(2.0); },
        {
            posSupportSpotsSearch,
            posSupportMaterial,
            posEstimateCurledExtrusions,
            psWipeTower,
            psSkirtBrim,
            psAlertWhenSupportsNeeded,
            psWipeTower,
            psGCodeExport,
        }
    );
}

TEST_CASE_METHOD(ApplyTestFixture, "Apply invalidates correct steps - layer height", "[PrintApply]")
{
    apply_and_check(
        *this,
        [](TestConfig& c) { c.print.items.opt("layer_height").set<double>(0.2); },
        {
            posSlice,
            posPerimeters,
            posPrepareInfill,
            posInfill,
            posIroning,
            posSupportSpotsSearch,
            posSupportMaterial,
            posEstimateCurledExtrusions,
            posCalculateOverhangingPerimeters,
            psSkirtBrim,
            psAlertWhenSupportsNeeded,
            psWipeTower,
            psGCodeExport,
        }
    );
}

TEST_CASE_METHOD(ApplyTestFixture, "Apply invalidates correct steps - fill density", "[PrintApply]")
{
    apply_and_check(
        *this,
        [](TestConfig& c) { c.print.items.opt("fill_density").set(Percentage{40.0}); },
        {
            posPrepareInfill,
            posInfill,
            posIroning,
            posSupportSpotsSearch,
            psAlertWhenSupportsNeeded,
            psWipeTower,
            psGCodeExport,
        }
    );
}

TEST_CASE_METHOD(ApplyTestFixture, "Apply invalidates correct steps - GCode flavor", "[PrintApply]")
{
    apply_and_check(
        *this,
        [](TestConfig& c)
        { c.printer.items.opt("gcode_flavor").set(Domain::GCodeFlavor::gcfKlipper); },
        {psWipeTower, psSkirtBrim, psGCodeExport}
    );
}

TEST_CASE("Apply rejects invalid extruders", "[PrintApply]") {
    using Biz::Slicing::ApplyStatus::Status;
    using Biz::Slicing::ApplyStatus::InvalidData;
    using Biz::Slicing::ApplyStatus::Changed;
    using Biz::Slicing::ErrorCode;

    Print print{};
    Domain::Model model;
    TestConfig config{3};
    Slic3r::Test::init_print({TestMesh::cube_20x20x20}, print, model, config);

    Domain::Bed bed{};
    Domain::BedInstance bed_instance{bed};
    bed_instance.model_instances = {model.objects.front()->instances.front()};
    HwPrinterConfig hw_config{create_dummy_hw_config(config.tool.size())};
    auto preset_metadata =
        create_dummy_selected_preset_metadata(hw_config);
    auto metadata = Biz::Slicing::build_gcode_metadata({}, preset_metadata, config);

    // Can be <1, 3> on print, object and volume.
    config.print.items.opt("infill_extruder").set(4);
    model.objects.front()->object_settings.overrides.set("perimeter_extruder", 0);
    model.objects.front()->volumes.front()->volume_settings.overrides.set("solid_infill_extruder", 0);

    // Can be <0, 3> on print.
    config.print.items.opt("wipe_tower_extruder").set(-1);

    // Can be <0, 3> on print and object.
    config.print.items.opt("support_material_extruder").set(4);
    model.objects.front()->object_settings.overrides.set("support_material_interface_extruder", 4);

    Status status{print.update(
        model,
        config,
        bed_instance,
        preset_metadata,
        Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config)
    )};
    REQUIRE(std::holds_alternative<InvalidData>(status));
    const auto errors{std::get<InvalidData>(status).errors};
    REQUIRE(errors.size() == 1);
    const auto error{errors.front()};
    CHECK(error.code == ErrorCode::InvalidExtruders);
    CHECK_THAT(
        error.item_keys,
        Catch::Matchers::UnorderedEquals(
            std::vector<std::string>{
                "infill_extruder",
                "perimeter_extruder",
                "solid_infill_extruder",
                "wipe_tower_extruder",
                "support_material_extruder",
                "support_material_interface_extruder"

            }
        )
    );

    config.print.items.opt("infill_extruder").set(3);
    model.objects.front()->object_settings.overrides.set("perimeter_extruder", 2);
    model.objects.front()->volumes.front()->volume_settings.overrides.set("solid_infill_extruder", 1);

    config.print.items.opt("wipe_tower_extruder").set(0);

    config.print.items.opt("support_material_extruder").set(0);
    model.objects.front()->object_settings.overrides.set("support_material_interface_extruder", 2);

    status = print.update(
        model,
        config,
        bed_instance,
        preset_metadata,
        Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config)
    );
    REQUIRE(std::holds_alternative<Changed>(status));
}

SCENARIO("Print: Object containing a degenerate planar volume", "[PrintApply]")
{
    using Slic3r::Biz::Algorithms::TriangleMesh::construct;
    using Slic3r::Biz::Algorithms::TriangleMesh::make_cube;

    GIVEN("20mm cube object with an additional zero-thickness planar part")
    {
        Domain::Model model;
        Domain::ModelObject* object = model.add_object();
        object->name                = "object.stl";
        add_volume(object, make_cube(20., 20., 20.));

        indexed_triangle_set planar_its;
        planar_its.vertices = {
            Vec3f(5.f, 5.f, 20.f),
            Vec3f(15.f, 5.f, 20.f),
            Vec3f(15.f, 15.f, 20.f),
            Vec3f(5.f, 15.f, 20.f)
        };
        planar_its.indices = {Domain::Index3{0, 1, 2}, Domain::Index3{0, 2, 3}};
        add_volume(object, construct(planar_its));
        object->add_instance();

        const std::shared_ptr<const TriangleMesh>& convex_hull{
            object->volumes.back()->get_convex_hull_shared_ptr()
        };
        REQUIRE(convex_hull != nullptr);
        REQUIRE(convex_hull->its.indices.empty());

        WHEN("the print is synchronized with the model")
        {
            Print print;
            TestConfig config;
            Test::init_print(std::vector<TriangleMesh>{}, print, model, config);
            THEN("a print object with regions for both volumes exists")
            {
                REQUIRE(print.objects().size() == 1);
                REQUIRE(print.objects().front()->shared_regions() != nullptr);
            }
        }
    }
}

using SLAStep = std::variant<SLAPrintStep, SLAPrintObjectStep>;

// Check that all other steps are done (hence the exclusive in the name).
// Check only the first object.
bool is_exclusively_undone(const SLAPrint& print, const std::set<SLAStep>& steps)
{
    for (std::size_t i{}; i < SLAPrintStep::slapsCount; ++i) {
        const SLAPrintStep step{static_cast<SLAPrintStep>(i)};
        if (steps.contains(step)) {
            if (print.is_step_done(step)) {
                return false;
            }
            continue;
        }
        if (!print.is_step_done(step)) {
            return false;
        }
    }

    ASSERT(print.objects().size() == 1);
    const SLAPrintObject& object{*print.objects().front()};

    for (std::size_t i{}; i < SLAPrintObjectStep::slaposCount; ++i) {
        const SLAPrintObjectStep step{static_cast<SLAPrintObjectStep>(i)};
        if (steps.contains(step)) {
            if (object.is_step_done(step)) {
                return false;
            }
            continue;
        }
        if (!object.is_step_done(step)) {
            return false;
        }
    }

    return true;
}

struct SLAApplyTestFixture
{
    ConfigPackSLA config{};
    SLAPrint print{[](Biz::Slicing::SLAResult&&) {}, [](const Biz::Slicing::Sla::Object&) {}};
    Domain::Model model;
    Domain::Bed model_bed;
    Domain::BedInstance bed_instance{model_bed};

    SLAApplyTestFixture()
    {
        TriangleMesh mesh{Biz::Algorithms::TriangleMesh::make_cube(5.0, 5.0, 0.5)};
        Domain::ModelObject* object = model.add_object();
        object->name += "object.stl";
        add_volume(object, mesh);
        object->add_instance();

        for (const Domain::ModelObject* object : model.objects) {
            for (Domain::ModelInstance* instance : object->instances) {
                bed_instance.model_instances.push_back(instance);
            }
        }
        auto preset_metadata = create_dummy_selected_preset_metadata(
            create_dummy_hw_config(1, 0, Domain::PrinterTechnology::SLA)
        );
        auto metadata = Biz::Slicing::build_gcode_metadata({}, preset_metadata, config);

        print.update(
            model,
            config,
            bed_instance,
            preset_metadata,
            Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config)
        );
    }
};

void apply_and_check(
    SLAApplyTestFixture& context,
    const std::function<void(ConfigPackSLA&)>& modify_config,
    const std::set<SLAStep>& expected_undone
)
{
    ThumbnailGenerator thumbnail_generator{};
    Biz::Slicing::SerializedConfig serialized_config{};
    HwPrinterConfig hw_config{create_dummy_hw_config(1, 0, Domain::PrinterTechnology::SLA)};

    context.print.slice(SlicingId{0, 0}, thumbnail_generator, std::nullopt);
    REQUIRE(is_exclusively_undone(context.print, {}));

    modify_config(context.config);

    auto preset_metadata = create_dummy_selected_preset_metadata(hw_config);
    auto metadata = Biz::Slicing::build_gcode_metadata({}, preset_metadata, context.config);

    const auto apply_status{context.print.update(
        context.model,
        context.config,
        context.bed_instance,
        preset_metadata,
        Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, context.config)
    )};
    REQUIRE(is_exclusively_undone(context.print, expected_undone));
}

TEST_CASE_METHOD(
    SLAApplyTestFixture,
    "SLA Apply invalidates correct steps - layer_height",
    "[SLAPrintApply]"
)
{
    apply_and_check(
        *this,
        [](ConfigPackSLA& c) { c.sla_print_settings.items.opt("layer_height").set<double>(0.11); },
        {slaposObjectSlice,
         slaposSupportPoints,
         slaposSupportTree,
         slaposPad,
         slaposSliceSupports,
         slapsMergeSlicesAndEval}
    );
}

TEST_CASE_METHOD(
    SLAApplyTestFixture,
    "Apply invalidates correct steps - absolute_correction",
    "[SLAPrintApply]"
)
{
    apply_and_check(
        *this,
        [](ConfigPackSLA& c) { c.sla_material_settings.overrides.set("absolute_correction", 0.42); },
        {slapsMergeSlicesAndEval,
         slapsRasterize,
         slaposAssembly,
         slaposHollowing,
         slaposDrillHoles,
         slaposObjectSlice,
         slaposSupportPoints,
         slaposSupportTree,
         slaposPad,
         slaposSliceSupports
        }
    );
}

TEST_CASE_METHOD(
    SLAApplyTestFixture,
    "SLA Apply invalidates correct steps - display_width",
    "[SLAPrintApply]"
)
{
    apply_and_check(
        *this,
        [](ConfigPackSLA& c)
        { c.sla_printer_settings.items.opt("display_width").set<double>(121.0); },
        {slapsMergeSlicesAndEval, slapsRasterize}
    );
}

TEST_CASE_METHOD(
    SLAApplyTestFixture,
    "SLA Apply invalidates correct steps - branchingsupport_base_diameter",
    "[SLAPrintApply]"
)
{
    apply_and_check(
        *this,
        [](ConfigPackSLA& c)
        { c.sla_print_settings.items.opt("branchingsupport_base_diameter").set<double>(5.0); },
        {slaposSupportTree, slaposPad, slaposSliceSupports, slapsMergeSlicesAndEval}
    );
}

TEST_CASE_METHOD(
    SLAApplyTestFixture,
    "SLA Apply invalidates correct steps - fast_tilt_time",
    "[SLAPrintApply]"
)
{
    apply_and_check(
        *this,
        [](ConfigPackSLA& c)
        { c.sla_printer_settings.items.opt("fast_tilt_time").set<double>(6.0); },
        {}
    );
}
