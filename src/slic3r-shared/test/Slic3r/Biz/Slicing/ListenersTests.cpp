#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <span>

#include <Slic3r/Biz/Slicing/SlicingInteractor.hpp>

#include "Slic3r/Biz/Slicing/TestUtils.hpp"
#include "Slic3r/Biz/Slicing/GCodeUtils.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

#include "libslic3r/SLAResult.hpp"
#include "Slic3r/Biz/Algorithms/SlaPoints.hpp"
#include "Slic3r/Biz/FDMResultCache.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"
#include "Slic3r/Biz/SLAObjectCache.hpp"

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

TEST_CASE_METHOD(SlicingFixture, "Switching the printer technology of a bed reslices it with the other technology", "[slicing][slicing-interactor][timeout]") {
    using namespace std::chrono_literals;

    // One bed with one model; only the selected preset (and so the technology) changes.
    ModelOnBed bed{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::FFF)};
    const ModelOnBed fff_settings{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::FFF)};
    const ModelOnBed sla_settings{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::SLA)};
    const SelectionId bed_id{bed.bed_instance.id().id};

    ResultListener fdm_result_listener;
    SLAResultListener sla_result_listener;
    slicing.set_listener<IFDMResultListener>(&fdm_result_listener);
    slicing.set_listener<ISLAResultListener>(&sla_result_listener);

    const auto slice_with = [&](const ModelOnBed& settings) {
        fdm_result_listener.gcodes.clear();
        sla_result_listener = SLAResultListener{};
        status_listener.status_events.clear();

        slicing.update_process(
            bed.model,
            bed.project_metadata,
            settings.preset_metadata,
            settings.config,
            bed.bed_instance
        );
        slicing.slice_all();

        REQUIRE(wait_for_status(dispatcher, status_listener, 30s, [](const StatusEvents& events) {
            return !events.empty() && events.back().status_code == StatusCode::Finished;
        }));
    };

    const auto has_gcode = [&]() {
        const auto it = fdm_result_listener.gcodes.find(bed_id);
        return it != fdm_result_listener.gcodes.end() && it->second && !it->second->str().empty();
    };
    const auto has_sla_slices = [&]() {
        return sla_result_listener.result_recieved
            && sla_result_listener.result.type != ResultType::None;
    };

    slice_with(fff_settings);
    CHECK(has_gcode());
    CHECK_FALSE(has_sla_slices());

    slice_with(sla_settings);
    CHECK(has_sla_slices());
    CHECK_FALSE(has_gcode());

    slice_with(fff_settings);
    CHECK(has_gcode());
    CHECK_FALSE(has_sla_slices());
}

TEST_CASE_METHOD(SlicingFixture, "Switching the printer technology of a bed clears the result of the old technology", "[slicing][slicing-interactor][timeout]") {
    using namespace std::chrono_literals;

    // The result caches the UI reads from must not keep the result of the old technology.
    ModelOnBed bed{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::FFF)};
    const ModelOnBed fff_settings{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::FFF)};
    const ModelOnBed sla_settings{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::SLA)};

    Slic3r::Biz::FDMResultCache fdm_cache;
    Slic3r::Biz::SLAResultCache sla_cache;
    slicing.set_listener<IFDMResultListener>(&fdm_cache);
    slicing.set_listener<ISLAResultListener>(&sla_cache);

    const auto slice_with = [&](const ModelOnBed& settings) {
        status_listener.status_events.clear();

        slicing.update_process(
            bed.model,
            bed.project_metadata,
            settings.preset_metadata,
            settings.config,
            bed.bed_instance
        );
        slicing.slice_all();

        REQUIRE(wait_for_status(dispatcher, status_listener, 30s, [](const StatusEvents& events) {
            return !events.empty() && events.back().status_code == StatusCode::Finished;
        }));
        return status_listener.status_events.back().slicing_id;
    };

    const SlicingId id{slice_with(fff_settings)};
    CHECK(fdm_cache.get_result(id));
    CHECK_FALSE(sla_cache.get_result(id));

    CHECK(slice_with(sla_settings) == id);
    CHECK(sla_cache.get_result(id));
    CHECK_FALSE(fdm_cache.get_result(id));

    CHECK(slice_with(fff_settings) == id);
    CHECK(fdm_cache.get_result(id));
    CHECK_FALSE(sla_cache.get_result(id));
}

namespace {

// Remembers the last notification of what a bed process produces besides the results.
struct BedOutputsListener : public IWipeTowerGeometryListener,
                            public Slic3r::Biz::Slicing::IGeneratedSupportPointsListener,
                            public Slic3r::Biz::Slicing::IExtruderCandidatesListener
{
    void on_wipe_tower_geometry_changed(OptWipeTowerGeometry geometry, const SlicingId) override
    {
        wipe_tower = std::move(geometry);
        wipe_tower_notified = true;
    }

    void on_generated_support_points_changed(
        Slic3r::Biz::Slicing::GeneratedSupportPointsSnapshot&& points,
        const SlicingId
    ) override
    {
        support_points          = std::move(points);
        support_points_notified = true;
    }

    void on_extruder_candidates_changed(std::vector<unsigned> candidates, const SlicingId) override
    {
        extruder_candidates          = std::move(candidates);
        extruder_candidates_notified = true;
    }

    void reset() { *this = BedOutputsListener{}; }

    OptWipeTowerGeometry wipe_tower;
    Slic3r::Biz::Slicing::GeneratedSupportPointsSnapshot support_points;
    std::vector<unsigned> extruder_candidates;
    bool wipe_tower_notified{false};
    bool support_points_notified{false};
    bool extruder_candidates_notified{false};
};

} // namespace

TEST_CASE_METHOD(SlicingFixture, "Switching the printer technology of a bed clears the other outputs of the old process", "[slicing][slicing-interactor][timeout]") {
    using namespace std::chrono_literals;

    ModelOnBed bed{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::FFF)};
    const ModelOnBed fff_settings{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::FFF)};
    const ModelOnBed sla_settings{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::SLA)};

    BedOutputsListener listener;
    slicing.add_listener<IWipeTowerGeometryListener>(&listener);
    slicing.set_listener<Slic3r::Biz::Slicing::IGeneratedSupportPointsListener>(&listener);
    slicing.add_listener<Slic3r::Biz::Slicing::IExtruderCandidatesListener>(&listener);

    const auto slice_with = [&](const ModelOnBed& settings) {
        status_listener.status_events.clear();
        slicing.update_process(
            bed.model,
            bed.project_metadata,
            settings.preset_metadata,
            settings.config,
            bed.bed_instance
        );
        slicing.slice_all();
        REQUIRE(wait_for_status(dispatcher, status_listener, 30s, [](const StatusEvents& events) {
            return !events.empty() && events.back().status_code == StatusCode::Finished;
        }));
    };

    slice_with(fff_settings);

    // An SLA process never reports these, so only the switch itself can clear what the FFF one did.
    listener.reset();
    slice_with(sla_settings);
    CHECK(listener.wipe_tower_notified);
    CHECK_FALSE(listener.wipe_tower.has_value());
    CHECK(listener.support_points_notified);
    CHECK(listener.support_points.empty());
    CHECK(listener.extruder_candidates_notified);
    CHECK(listener.extruder_candidates.empty());
}

TEST_CASE_METHOD(SlicingFixture, "SLA bed with a pad but no supports ends as invalid data", "[slicing][slicing-interactor][timeout]") {
    using namespace std::chrono_literals;

    // A pad needs supports (no pad can be generated without them); it must not crash.
    ModelOnBed bed{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::SLA)};
    ConfigPackSLA& config{std::get<ConfigPackSLA>(bed.config)};
    config.sla_print_settings.items.opt("supports_enable").set(false);
    REQUIRE(config.sla_print_settings.items.opt("pad_enable").get<bool>());

    slicing.update_process(
        bed.model,
        bed.project_metadata,
        bed.preset_metadata,
        bed.config,
        bed.bed_instance
    );
    slicing.slice_all();

    CHECK(wait_for_status(dispatcher, status_listener, 30s, [](const StatusEvents& events) {
        return !events.empty() && events.back().status_code == StatusCode::InvalidData;
    }));
}

TEST_CASE_METHOD(SlicingFixture, "SLA bed without printable objects is empty", "[slicing][slicing-interactor][timeout]") {
    using namespace std::chrono_literals;

    // An empty bed must not be sliced, there would be no result to export.
    ModelOnBed bed{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::SLA)};
    SECTION("No objects") {
        bed.model.clear_objects();
        bed.bed_instance.model_instances.clear();
    }
    SECTION("Not printable instance") {
        bed.model.objects.front()->instances.front()->printable = false;
    }

    slicing.update_process(
        bed.model,
        bed.project_metadata,
        bed.preset_metadata,
        bed.config,
        bed.bed_instance
    );

    CHECK(wait_for_status(dispatcher, status_listener, 30s, [](const StatusEvents& events) {
        return !events.empty() && events.back().status_code == StatusCode::Empty;
    }));
}

TEST_CASE_METHOD(SlicingFixture, "SLA slicing uses the user modified support points as they are", "[slicing][slicing-interactor][timeout]") {
    using namespace std::chrono_literals;
    using Slic3r::Domain::SLA::PointsStatus;
    using Slic3r::Domain::SLA::SupportPoint;
    using Slic3r::Domain::SLA::SupportPointType;
    using Slic3r::Domain::Vec3f;

    ModelOnBed bed{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::SLA)};
    ModelObject& object{*bed.model.objects.front()};
    const float radius{0.123f};
    object.volumes.front()->sla_support_points = {
        SupportPoint{Vec3f{10.f, 10.f, 0.f}, radius, SupportPointType::island},
        SupportPoint{Vec3f{0.f, 10.f, 10.f}, radius, SupportPointType::slope},
        SupportPoint{Vec3f{5.f, 5.f, 0.f}, radius, SupportPointType::manual_add},
    };
    object.sla_points_status = PointsStatus::UserModified;

    Slic3r::Biz::SLAObjectCache object_cache;
    slicing.set_listener<ISLAObjectListener>(&object_cache);

    slicing.update_process(
        bed.model,
        bed.project_metadata,
        bed.preset_metadata,
        bed.config,
        bed.bed_instance
    );
    slicing.slice_all();

    REQUIRE(wait_for_status(dispatcher, status_listener, 30s, [](const StatusEvents& events) {
        return !events.empty() && events.back().status_code == StatusCode::Finished;
    }));

    const SlicingId id{status_listener.status_events.back().slicing_id};
    const auto sla_object{object_cache.get_instance({id, object.id()})};
    REQUIRE(sla_object);
    REQUIRE(sla_object->get().support_points);

    // No points are generated, the user ones are only transformed into the print object.
    const Slic3r::Domain::SLA::SupportPoints& points{*sla_object->get().support_points};
    REQUIRE(points.size() == object.volumes.front()->sla_support_points.size());
    for (size_t i{0}; i < points.size(); ++i) {
        CHECK(points[i].head_front_radius == radius);
        CHECK(points[i].type == object.volumes.front()->sla_support_points[i].type);
    }
}

TEST_CASE_METHOD(SlicingFixture, "SLA object brings the transformation of its support points into the model", "[slicing][slicing-interactor][timeout]") {
    using namespace std::chrono_literals;
    using Slic3r::Domain::SLA::PointsStatus;
    using Slic3r::Domain::SLA::SupportPoint;
    using Slic3r::Domain::SLA::SupportPointType;
    using Slic3r::Domain::Vec3f;

    ModelOnBed bed{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::SLA)};
    ModelObject& object{*bed.model.objects.front()};
    ModelInstance& instance{*object.instances.front()};
    instance.set_scaling_factor(Vec3d{1.5, 1.5, 1.5});
    instance.set_rotation(Vec3d{0., 0., 0.5});
    object.volumes.front()->sla_support_points = {
        SupportPoint{Vec3f{10.f, 10.f, 0.f}, 0.2f, SupportPointType::manual_add},
        SupportPoint{Vec3f{0.f, 10.f, 10.f}, 0.2f, SupportPointType::manual_add},
    };
    object.sla_points_status = PointsStatus::UserModified;

    Slic3r::Biz::SLAObjectCache object_cache;
    slicing.set_listener<ISLAObjectListener>(&object_cache);

    slicing.update_process(
        bed.model,
        bed.project_metadata,
        bed.preset_metadata,
        bed.config,
        bed.bed_instance
    );
    slicing.slice_all();

    REQUIRE(wait_for_status(dispatcher, status_listener, 30s, [](const StatusEvents& events) {
        return !events.empty() && events.back().status_code == StatusCode::Finished;
    }));

    const SlicingId id{status_listener.status_events.back().slicing_id};
    const auto sla_object{object_cache.get_instance({id, object.id()})};
    REQUIRE(sla_object);
    REQUIRE(sla_object->get().support_points);

    const Slic3r::Domain::Transform3d to_model{sla_object->get().object_trafo.inverse()};
    CHECK_FALSE(to_model.isApprox(Slic3r::Domain::Transform3d::Identity()));
    const Slic3r::Domain::SLA::SupportPoints& points{*sla_object->get().support_points};
    const Slic3r::Domain::SLA::SupportPoints in_object{
        Slic3r::Biz::Algorithms::SlaPoints::support_points_in_object(object)
    };
    REQUIRE(points.size() == in_object.size());
    for (size_t i{0}; i < points.size(); ++i) {
        const Vec3d model_position{to_model * points[i].pos.cast<double>()};
        CHECK(model_position.isApprox(in_object[i].pos.cast<double>(), 1e-5));
    }
}

TEST_CASE_METHOD(SlicingFixture, "SLA support points follow the transformation of their part", "[slicing][slicing-interactor][timeout]") {
    using namespace std::chrono_literals;
    using Slic3r::Domain::SLA::PointsStatus;
    using Slic3r::Domain::SLA::SupportPoint;
    using Slic3r::Domain::SLA::SupportPointType;
    using Slic3r::Domain::Vec3f;

    ModelOnBed bed{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::SLA)};
    ModelObject& object{*bed.model.objects.front()};
    Slic3r::Domain::ModelVolume& volume{*object.volumes.front()};
    const Vec3d mesh_point{volume.mesh().bounding_box().max};
    volume.sla_support_points = {
        SupportPoint{mesh_point.cast<float>(), 0.2f, SupportPointType::manual_add},
    };
    object.sla_points_status = PointsStatus::UserModified;
    // The new UI puts the object scale and tilt into its volumes.
    volume.set_scaling_factor(Vec3d{2., 1.5, 1.});
    volume.set_rotation(Vec3d{0.3, 0., 0.});

    Slic3r::Biz::SLAObjectCache object_cache;
    slicing.set_listener<ISLAObjectListener>(&object_cache);

    slicing.update_process(
        bed.model,
        bed.project_metadata,
        bed.preset_metadata,
        bed.config,
        bed.bed_instance
    );
    slicing.slice_all();

    REQUIRE(wait_for_status(dispatcher, status_listener, 30s, [](const StatusEvents& events) {
        return !events.empty() && events.back().status_code == StatusCode::Finished;
    }));

    const SlicingId id{status_listener.status_events.back().slicing_id};
    const auto sla_object{object_cache.get_instance({id, object.id()})};
    REQUIRE(sla_object);
    REQUIRE(sla_object->get().support_points);
    const Slic3r::Domain::SLA::SupportPoints& points{*sla_object->get().support_points};
    REQUIRE(points.size() == 1);

    // The point is still on the same corner of the scaled and tilted mesh.
    const Vec3d model_position{sla_object->get().object_trafo.inverse() * points.front().pos.cast<double>()};
    CHECK(model_position.isApprox(volume.get_matrix() * mesh_point, 1e-5));
    CHECK_FALSE(model_position.isApprox(mesh_point, 1e-2));
}

TEST_CASE_METHOD(SlicingFixture, "SLA object keeps the valid part of a partial slice after an update", "[slicing][slicing-interactor][timeout]") {
    using namespace std::chrono_literals;
    using Slic3r::Biz::Slicing::SliceUntilStep;

    ModelOnBed bed{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::SLA)};
    ConfigPackSLA& config{std::get<ConfigPackSLA>(bed.config)};
    const Slic3r::Domain::ObjectID object_id{bed.model.objects.front()->id()};

    Slic3r::Biz::SLAObjectCache object_cache;
    slicing.set_listener<ISLAObjectListener>(&object_cache);

    slicing.update_process(
        bed.model,
        bed.project_metadata,
        bed.preset_metadata,
        bed.config,
        bed.bed_instance
    );

    const SlicingId id{0, bed.bed_instance.id().id};
    slicing.slice_bed(id, SliceUntilStep{Slic3r::slaposSupportPoints, object_id});

    REQUIRE(wait_for_status(dispatcher, status_listener, 30s, [](const StatusEvents& events) {
        const bool was_running = std::ranges::any_of(events, [](const StatusEvent& event) {
            return event.status_code == StatusCode::Running;
        });
        return was_running && events.back().status_code == StatusCode::Modified;
    }));

    const auto cached_object = [&]() { return object_cache.get_instance({id, object_id}); };
    REQUIRE(cached_object());
    REQUIRE(cached_object()->get().mesh);
    REQUIRE(cached_object()->get().support_points);
    CHECK_FALSE(cached_object()->get().support_structure);

    const auto update_with = [&](const ConfigPackSLA& new_config) {
        slicing.update_process(
            bed.model,
            bed.project_metadata,
            bed.preset_metadata,
            new_config,
            bed.bed_instance
        );
        dispatcher.dispatch_enqueued();
    };

    SECTION("A change of a later print step keeps the object") {
        // The finished steps don't run again, so nothing would send the object later.
        ConfigPackSLA new_config{config};
        Slic3r::Domain::ConfigItem& exposure_time{new_config.sla_material_settings.items.opt("exposure_time")};
        exposure_time.set(exposure_time.get<double>() + 1.);
        update_with(new_config);

        REQUIRE(cached_object());
        CHECK(cached_object()->get().mesh);
        CHECK(cached_object()->get().support_points);
    }

    SECTION("A change of the support points keeps only the mesh") {
        ConfigPackSLA new_config{config};
        new_config.sla_print_settings.items.opt("support_points_density_relative").set(150);
        update_with(new_config);

        REQUIRE(cached_object());
        CHECK(cached_object()->get().mesh);
        CHECK_FALSE(cached_object()->get().support_points);
    }
}

struct SLAObjectCacheChanges : public Slic3r::Biz::ISLAObjectCacheChangedListener {
    void on_sla_object_cache_changed(const SlicingId&, Slic3r::Domain::ObjectID object_id) override
    {
        changed_objects.push_back(object_id);
    }
    std::vector<Slic3r::Domain::ObjectID> changed_objects;
};

TEST_CASE_METHOD(SlicingFixture, "SLA objects are dropped when a bed gets a process of another printer", "[slicing][slicing-interactor][timeout]") {
    using namespace std::chrono_literals;

    ModelOnBed bed{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::SLA)};
    const Slic3r::Domain::ObjectID object_id{bed.model.objects.front()->id()};

    Slic3r::Biz::SLAObjectCache object_cache;
    slicing.set_listener<ISLAObjectListener>(&object_cache);
    SLAObjectCacheChanges changes;
    object_cache.add_listener<Slic3r::Biz::ISLAObjectCacheChangedListener>(&changes);

    slicing.update_process(
        bed.model,
        bed.project_metadata,
        bed.preset_metadata,
        bed.config,
        bed.bed_instance
    );
    slicing.slice_all();

    REQUIRE(wait_for_status(dispatcher, status_listener, 30s, [](const StatusEvents& events) {
        return !events.empty() && events.back().status_code == StatusCode::Finished;
    }));
    const SlicingId id{status_listener.status_events.back().slicing_id};
    REQUIRE(object_cache.get_instance({id, object_id}));

    // Another SLA printer: its process never sends anything about the objects of the old one.
    Slic3r::Domain::Preset::SelectedPresetMetadata other_printer{bed.preset_metadata};
    other_printer.hw_config.id = "other SLA printer";
    changes.changed_objects.clear();
    slicing.update_process(
        bed.model,
        bed.project_metadata,
        other_printer,
        bed.config,
        bed.bed_instance
    );
    dispatcher.dispatch_enqueued();

    CHECK_FALSE(object_cache.get_instance({id, object_id}));
    CHECK_THAT(changes.changed_objects, Equals(std::vector{object_id}));

    object_cache.remove_listener<Slic3r::Biz::ISLAObjectCacheChangedListener>(&changes);
}

TEST_CASE_METHOD(SlicingFixture, "Changing user modified SLA support points reslices the supports", "[slicing][slicing-interactor][timeout]") {
    using namespace std::chrono_literals;
    using Slic3r::Domain::SLA::PointsStatus;
    using Slic3r::Domain::SLA::SupportPoint;
    using Slic3r::Domain::SLA::SupportPointType;
    using Slic3r::Domain::Vec3f;

    ModelOnBed bed{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::SLA)};
    ModelObject& object{*bed.model.objects.front()};
    object.volumes.front()->sla_support_points = {
        SupportPoint{Vec3f{5.f, 5.f, 0.f}, 0.2f, SupportPointType::manual_add},
        SupportPoint{Vec3f{15.f, 15.f, 0.f}, 0.2f, SupportPointType::manual_add},
    };
    object.sla_points_status = PointsStatus::UserModified;

    Slic3r::Biz::SLAObjectCache object_cache;
    slicing.set_listener<ISLAObjectListener>(&object_cache);
    SLAResultListener result_listener;
    slicing.set_listener<ISLAResultListener>(&result_listener);

    const auto slice = [&]() {
        status_listener.status_events.clear();
        slicing.update_process(
            bed.model,
            bed.project_metadata,
            bed.preset_metadata,
            bed.config,
            bed.bed_instance
        );
        slicing.slice_all();
        REQUIRE(wait_for_status(dispatcher, status_listener, 30s, [](const StatusEvents& events) {
            return !events.empty() && events.back().status_code == StatusCode::Finished;
        }));
        return status_listener.status_events.back().slicing_id;
    };

    const SlicingId id{slice()};
    const auto first{object_cache.get_instance({id, object.id()})};
    REQUIRE(first);
    REQUIRE(first->get().support_structure);
    const auto first_tree{first->get().support_structure};
    REQUIRE(result_listener.result.export_data);
    REQUIRE_FALSE(result_listener.result.export_data->files.data.empty());

    // The user adds a point (as the support points gizmo does on Apply).
    object.volumes.front()->sla_support_points.push_back(
        SupportPoint{Vec3f{5.f, 15.f, 0.f}, 0.2f, SupportPointType::manual_add}
    );
    result_listener.result_recieved = false;
    result_listener.result          = {};
    CHECK(slice() == id);

    const auto second{object_cache.get_instance({id, object.id()})};
    REQUIRE(second);
    REQUIRE(second->get().support_points);
    CHECK(second->get().support_points->size() == 3);
    REQUIRE(second->get().support_structure);
    CHECK(second->get().support_structure != first_tree);
    // The archive is made again too.
    REQUIRE(result_listener.result.export_data);
    CHECK_FALSE(result_listener.result.export_data->files.data.empty());
}

TEST_CASE_METHOD(SlicingFixture, "Changing an SLA object setting rasterizes the slices again", "[slicing][slicing-interactor][timeout]") {
    using namespace std::chrono_literals;

    ModelOnBed bed{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::SLA)};
    SLAResultListener result_listener;
    slicing.set_listener<ISLAResultListener>(&result_listener);

    const auto slice = [&]() {
        status_listener.status_events.clear();
        result_listener.result = {};
        slicing.update_process(
            bed.model,
            bed.project_metadata,
            bed.preset_metadata,
            bed.config,
            bed.bed_instance
        );
        slicing.slice_all();
        REQUIRE(wait_for_status(dispatcher, status_listener, 30s, [](const StatusEvents& events) {
            return !events.empty() && events.back().status_code == StatusCode::Finished;
        }));
    };

    slice();
    REQUIRE(result_listener.result.export_data);
    REQUIRE_FALSE(result_listener.result.export_data->files.data.empty());

    // A change of the supports makes new slices, which have to be rasterized for the archive.
    Slic3r::Domain::ConfigItem& pillar_diameter{
        std::get<ConfigPackSLA>(bed.config).sla_print_settings.items.opt("support_pillar_diameter")
    };
    pillar_diameter.set(pillar_diameter.get<double>() + 0.2);
    slice();
    REQUIRE(result_listener.result.export_data);
    CHECK_FALSE(result_listener.result.export_data->files.data.empty());
}

TEST_CASE_METHOD(SlicingFixture, "SLA slicing reads the options of every support tree type and archive format", "[slicing][slicing-interactor][timeout]") {
    using namespace std::chrono_literals;

    ModelOnBed bed{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::SLA)};
    ConfigPackSLA& config{std::get<ConfigPackSLA>(bed.config)};

    SECTION("Branching supports") {
        config.sla_print_settings.items.opt("support_tree_type").set(std::string("branching"));
    }
    SECTION("SL1 SVG archive") {
        config.sla_printer_settings.items.opt("sla_archive_format").set(std::string("SL1SVG"));
    }

    slicing.update_process(
        bed.model,
        bed.project_metadata,
        bed.preset_metadata,
        bed.config,
        bed.bed_instance
    );
    slicing.slice_all();

    CHECK(wait_for_status(dispatcher, status_listener, 30s, [](const StatusEvents& events) {
        return !events.empty() && events.back().status_code == StatusCode::Finished;
    }));
}

TEST_CASE_METHOD(SlicingFixture, "Changing the resin color of an SLA bed doesn't need a reslice", "[slicing][slicing-interactor][timeout]") {
    using namespace std::chrono_literals;

    ModelOnBed bed{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::SLA)};
    SLAResultListener result_listener;
    slicing.set_listener<ISLAResultListener>(&result_listener);

    const auto update_and_slice = [&]() {
        status_listener.status_events.clear();
        slicing.update_process(
            bed.model,
            bed.project_metadata,
            bed.preset_metadata,
            bed.config,
            bed.bed_instance
        );
        slicing.slice_all();
        REQUIRE(wait_for_status(dispatcher, status_listener, 30s, [](const StatusEvents& events) {
            return !events.empty() && events.back().status_code == StatusCode::Finished;
        }));
    };

    update_and_slice();
    REQUIRE(result_listener.result.export_data);

    // The color is not used for slicing, so the result stays valid.
    std::get<ConfigPackSLA>(bed.config)
        .sla_project_settings.items.opt("resin_colour")
        .set(std::string{"#123456"});
    update_and_slice();
    CHECK(result_listener.result.export_data);
}
