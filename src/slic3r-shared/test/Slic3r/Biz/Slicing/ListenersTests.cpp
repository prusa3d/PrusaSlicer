#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <span>

#include <Slic3r/Biz/Slicing/SlicingInteractor.hpp>

#include "Slic3r/Biz/Slicing/TestUtils.hpp"
#include "Slic3r/Biz/Slicing/GCodeUtils.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

#include "libslic3r/SLAResult.hpp"

using namespace Catch;
using Catch::Matchers::Equals;
using Catch::Matchers::UnorderedEquals;

using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::high_resolution_clock;
using Slic3r::Biz::Slicing::StatusCode;
using Slic3r::Test::get_cubes_model;
using Slic3r::Test::ModelOnBed;
using Slic3r::Test::is_gcode_sane;
using Slic3r::Biz::Slicing::FDMResult;
using Slic3r::Domain::SlicingId;
using Slic3r::Domain::SelectionId;
using Slic3r::Biz::Slicing::IWipeTowerGeometryListener;
using Slic3r::Biz::Slicing::OptWipeTowerGeometry;
using Slic3r::Biz::Slicing::WipeTowerGeometry;
using Slic3r::Biz::Slicing::ZDepth;
using Slic3r::Test::SlicingFixture;
using Slic3r::Test::StatusEvent;
using Slic3r::Test::StatusEvents;
using Slic3r::Biz::Slicing::ISLAResultListener;
using Slic3r::Biz::Slicing::ISLAObjectListener;
using Slic3r::Biz::Slicing::SLAResult;
using Slic3r::Biz::Slicing::Sla::ResultType;
using Slic3r::Biz::Slicing::Sla::Object;
using Slic3r::Test::ResultListener;
using Slic3r::Biz::Slicing::IFDMResultListener;
using Slic3r::Domain::ConfigPackSLA;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::ModelInstance;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::Model;
using Slic3r::Test::are_statistics_sane;

void translate_instances(Model& model, const Vec3d& translation) {
    for (ModelObject* object : model.objects) {
        for (ModelInstance* instance : object->instances) {
            instance->set_offset(instance->get_offset() + translation);
        }
    }
}

TEST_CASE_METHOD(SlicingFixture, "Slice N beds", "[slicing][slicing-interactor][timeout]")
{
    ResultListener result_listener;
    slicing.set_listener<IFDMResultListener>(&result_listener);
    using namespace std::chrono_literals;

    std::vector<Slic3r::Test::ModelOnBed> bed_models;
    const int beds_count{5};
    for (std::size_t i{}; i < beds_count; ++i) {
        const Vec3d bed_offset{i * Vec3d{200.0, 150.0, 0.0}};
        const auto cube_count{static_cast<int>(i + 1)};
        bed_models.emplace_back(get_cubes_model(cube_count, 5));
        bed_models.back().bed_instance.transformation.set_offset(bed_offset);
        translate_instances(bed_models.back().model, bed_offset);
        slicing.update_process(
            bed_models.back().model,
            bed_models.back().project_metadata,
            bed_models.back().preset_metadata,
            bed_models.back().config,
            bed_models.back().bed_instance
        );
        translate_instances(bed_models.back().model, -bed_offset);
    }

    slicing.slice_all();

    REQUIRE(wait_for_status(dispatcher, status_listener, 5s, [](const StatusEvents &events){
        return beds_count == std::ranges::count_if(events, [](const StatusEvent& event){
            return event.status_code == StatusCode::Finished;
        });
    }));

    StatusEvents update_events;
    std::ranges::copy(
        std::span{status_listener.status_events}.subspan(0, beds_count * 2),
        std::back_inserter(update_events)
    );

    StatusEvents slicing_events;
    std::ranges::copy(
        std::span{status_listener.status_events}.subspan(beds_count * 2),
        std::back_inserter(slicing_events)
    );

    StatusEvents expected_update_events;
    StatusEvents expected_slicing_events;
    for (const ModelOnBed& model_on_bed : bed_models) {
        const SelectionId bed_id{model_on_bed.bed_instance.id().id};
        expected_update_events.push_back({StatusCode::Updating, SlicingId{0, bed_id}});
        expected_update_events.push_back({StatusCode::Modified, SlicingId{0, bed_id}});

        expected_slicing_events.push_back({StatusCode::Running, SlicingId{0, bed_id}});
        expected_slicing_events.push_back({StatusCode::Finished, SlicingId{0, bed_id}});
    }

    CHECK_THAT(update_events, Equals(expected_update_events));
    CHECK_THAT(slicing_events, UnorderedEquals(expected_slicing_events));

    for (const auto& pair : result_listener.gcodes) {
        const SelectionId id{pair.first};
        const std::string& gcode{pair.second->str()};

        const auto model_on_bed{std::ranges::find_if(bed_models, [&](const ModelOnBed& model_on_bed) {
            return model_on_bed.bed_instance.id().id == id;
        })};
        REQUIRE(model_on_bed != bed_models.end());

        const auto error{is_gcode_sane(gcode, model_on_bed->model)};
        INFO((error ? *error : ""));
        CHECK(!error);
    }
}

struct WipeTowerGeometryListener : public IWipeTowerGeometryListener
{
    void on_wipe_tower_geometry_changed(
        OptWipeTowerGeometry wipe_tower_geometry, const SlicingId
    ) override
    {
        geometry = std::move(wipe_tower_geometry);
    }

    OptWipeTowerGeometry geometry;
};

TEST_CASE_METHOD(SlicingFixture, "Background process dispatches wipe_tower_geometry once available", "[slicing][slicing-callbacks][timeout]")
{
    using namespace std::chrono_literals;
    ResultListener result_listener;
    slicing.set_listener<IFDMResultListener>(&result_listener);

    WipeTowerGeometryListener wipe_tower_geometry_listener;
    slicing.add_listener<IWipeTowerGeometryListener>(&wipe_tower_geometry_listener);

    auto [model, config, wipe_towers]{Tests::load_3mf(Tests::get_datadir() / "wipe_tower.3mf")};

    ModelOnBed model_on_bed{std::move(model), std::move(config)};
    model_on_bed.bed_instance.wipe_tower = wipe_towers.at(0);
    model_on_bed.preset_metadata.hw_config.features.insert({"multi_extruder", true});
    slicing.update_process(
        model_on_bed.model,
        model_on_bed.project_metadata,
        model_on_bed.preset_metadata,
        model_on_bed.config,
        model_on_bed.bed_instance
    );
    slicing.slice_all();

    REQUIRE(wait_for_status(dispatcher, status_listener, 5s, [](const StatusEvents &events){
        return events.back().status_code == StatusCode::Finished;
    }));

    const SelectionId bed_id{model_on_bed.bed_instance.id().id};
    const auto& gcode{result_listener.gcodes.at(bed_id)->str()};
    const auto error{are_statistics_sane(gcode)};
    INFO((error ? *error : ""));
    CHECK(!error);

    REQUIRE(wipe_tower_geometry_listener.geometry);
    const WipeTowerGeometry geometry{*wipe_tower_geometry_listener.geometry};

    // Two step wipe tower with brim.
    REQUIRE(geometry.depths.size() == 4);
    CHECK(geometry.depths[0].z == 0);
    CHECK(geometry.depths[3].depth == 0);

    ZDepth previous_z_depth{geometry.depths.front()};
    for (const auto& [z, depth] : std::span{geometry.depths}.subspan(1)) {
        CHECK(previous_z_depth.z < z);
        CHECK(previous_z_depth.depth > depth);
        previous_z_depth = {z, depth};
    }
}

struct GeneratedSupportPointsListener : public Slic3r::Biz::Slicing::IGeneratedSupportPointsListener
{
    void on_generated_support_points_changed(
        Slic3r::Biz::Slicing::GeneratedSupportPointsSnapshot&& generated_support_points,
        const SlicingId
    ) override
    {
        support_points = std::move(generated_support_points);
        received       = true;
    }

    Slic3r::Biz::Slicing::GeneratedSupportPointsSnapshot support_points;
    bool received{false};
};

TEST_CASE_METHOD(
    SlicingFixture,
    "Background process dispatches generated support points once available",
    "[slicing][slicing-callbacks][timeout]"
)
{
    using namespace std::chrono_literals;

    GeneratedSupportPointsListener support_points_listener;
    slicing.set_listener<Slic3r::Biz::Slicing::IGeneratedSupportPointsListener>(
        &support_points_listener
    );

    ModelOnBed model_on_bed{get_cubes_model(1, 5)};
    slicing.update_process(
        model_on_bed.model,
        model_on_bed.project_metadata,
        model_on_bed.preset_metadata,
        model_on_bed.config,
        model_on_bed.bed_instance
    );
    slicing.slice_all();

    REQUIRE(wait_for_status(
        dispatcher,
        status_listener,
        5s,
        [](const StatusEvents& events) { return events.back().status_code == StatusCode::Finished; }
    ));

    REQUIRE(support_points_listener.received);
    REQUIRE(support_points_listener.support_points.size() == 1);
    CHECK(
        support_points_listener.support_points.front().model_object_id
        == model_on_bed.model.objects.front()->id()
    );
}

TEST_CASE_METHOD(
    SlicingFixture,
    "Partial slicing dispatches generated support points without the FDM result",
    "[slicing][slicing-callbacks][timeout]"
)
{
    using namespace std::chrono_literals;
    using Slic3r::Biz::Slicing::SliceUntilStep;

    ResultListener result_listener;
    slicing.set_listener<IFDMResultListener>(&result_listener);

    GeneratedSupportPointsListener support_points_listener;
    slicing.set_listener<Slic3r::Biz::Slicing::IGeneratedSupportPointsListener>(
        &support_points_listener
    );

    ModelOnBed model_on_bed{get_cubes_model(1, 5)};
    slicing.update_process(
        model_on_bed.model,
        model_on_bed.project_metadata,
        model_on_bed.preset_metadata,
        model_on_bed.config,
        model_on_bed.bed_instance
    );

    const SlicingId id{0, model_on_bed.bed_instance.id().id};
    slicing.slice_bed(
        id,
        SliceUntilStep{Slic3r::posSupportSpotsSearch, model_on_bed.model.objects.front()->id()}
    );

    REQUIRE(wait_for_status(
        dispatcher,
        status_listener,
        5s,
        [](const StatusEvents& events)
        {
            const bool was_running = std::ranges::any_of(
                events,
                [](const StatusEvent& event) { return event.status_code == StatusCode::Running; }
            );
            return was_running && events.back().status_code == StatusCode::Modified;
        }
    ));

    REQUIRE(support_points_listener.received);
    REQUIRE(support_points_listener.support_points.size() == 1);
    CHECK(
        support_points_listener.support_points.front().model_object_id
        == model_on_bed.model.objects.front()->id()
    );

    const SelectionId bed_id{model_on_bed.bed_instance.id().id};
    const auto gcode_it{result_listener.gcodes.find(bed_id)};

    CHECK((gcode_it == result_listener.gcodes.end() || gcode_it->second->str().empty()));
}

struct SLAResultListener : public ISLAResultListener {
    void on_sla_result_changed(const SlicingId& id, SLAResult && result) override{
        switch (result.type) {
        case ResultType::None: [[fallthrough]];
        case ResultType::Slices: this->result = std::move(result); break;
        case ResultType::Files: this->result.export_data->files = std::move(result.export_data->files); break;}
        this->result_recieved = true;
    }
    SLAResult result;
    bool result_recieved{false};
};

struct SLAObjectListener : public ISLAObjectListener{
    void on_sla_object_changed(const SlicingId& id, Object && instance) override
    {
        this->object_recieved = true;
    }
    void on_remove_bed(const SlicingId&) override {}
    bool object_recieved{false};
};

TEST_CASE_METHOD(SlicingFixture, "Update reinitializes the process if printer technology differs", "[slicing][slicing-interactor]") {
    using namespace std::chrono_literals;

    ModelOnBed model_on_bed{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::SLA)};

    SLAResultListener result_listener;
    SLAObjectListener object_listener;
    slicing.set_listener<ISLAResultListener>(&result_listener);
    slicing.set_listener<ISLAObjectListener>(&object_listener);
    slicing.update_process(
        model_on_bed.model,
        model_on_bed.project_metadata,
        model_on_bed.preset_metadata,
        model_on_bed.config,
        model_on_bed.bed_instance
    );

    slicing.update_process(
        model_on_bed.model,
        model_on_bed.project_metadata,
        model_on_bed.preset_metadata,
        model_on_bed.config,
        model_on_bed.bed_instance
    );
    slicing.slice_all();

    REQUIRE(wait_for_status(dispatcher, status_listener, 15s, [](const StatusEvents &events){
        return events.back().status_code == StatusCode::Finished;
    }));

    CHECK(result_listener.result_recieved);
    CHECK(object_listener.object_recieved);
}
