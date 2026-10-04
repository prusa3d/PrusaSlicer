#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/SLAObjectRequest.hpp"
#include "Slic3r/Biz/Slicing/TestUtils.hpp"

#include "libslic3r/SLAResult.hpp"

using Slic3r::Biz::SLAObjectCache;
using Slic3r::Biz::SLAObjectOptRef;
using Slic3r::Biz::SLAObjectRequest;
using Slic3r::Biz::StatusCache;
using Slic3r::Biz::Slicing::ISLAObjectListener;
using Slic3r::Biz::Slicing::IStatusListener;
using Slic3r::Biz::Slicing::StatusCode;
using Slic3r::Domain::ConfigPackSLA;
using Slic3r::Domain::SlicingId;
using Slic3r::Test::get_cubes_model;
using Slic3r::Test::ModelOnBed;
using Slic3r::Test::SlicingFixture;
using Slic3r::Test::StatusEvent;
using Slic3r::Test::StatusEvents;

namespace {

struct SLAObjectRequestFixture : SlicingFixture
{
    SLAObjectRequestFixture()
    {
        slicing.add_listener<IStatusListener>(&status_cache);
        slicing.set_listener<ISLAObjectListener>(&object_cache);
        request.callbacks().completed = [this](const SLAObjectOptRef object) {
            ++completed_count;
            completed_object = object ? std::optional{object->get()} : std::nullopt;
        };
    }

    ~SLAObjectRequestFixture()
    {
        request.cancel();
        slicing.remove_listener<IStatusListener>(&status_cache);
    }

    void update_process()
    {
        slicing.update_process(
            bed.model,
            bed.project_metadata,
            bed.preset_metadata,
            bed.config,
            bed.bed_instance
        );
        dispatcher.dispatch_enqueued();
    }

    [[nodiscard]] bool wait_for_completion()
    {
        using namespace std::chrono_literals;
        const int count_before{completed_count};
        return wait_for_status(dispatcher, status_listener, 30s, [&](const StatusEvents&) {
            return completed_count > count_before;
        });
    }

    void start(const Slic3r::SLAPrintObjectStep step)
    {
        request.start(id, bed.model.objects.front()->id(), step);
    }

    ModelOnBed bed{get_cubes_model(1, 5, Slic3r::Domain::PrinterTechnology::SLA)};
    StatusCache status_cache;
    SLAObjectCache object_cache;
    SLAObjectRequest request{slicing, status_cache, object_cache};
    const SlicingId id{0, bed.bed_instance.id().id};

    int completed_count{0};
    std::optional<Slic3r::Biz::Slicing::Sla::Object> completed_object;
};

} // namespace

TEST_CASE_METHOD(SLAObjectRequestFixture, "SLA object request slices the object until the step", "[slicing][sla-object-request][timeout]")
{
    update_process();

    start(Slic3r::slaposSupportPoints);
    REQUIRE(wait_for_completion());
    CHECK_FALSE(request.running());
    REQUIRE(completed_object);
    CHECK(completed_object->mesh);
    CHECK(completed_object->support_points);
    CHECK_FALSE(completed_object->support_structure);
    CHECK(slicing.get_status(id) == StatusCode::Modified);

    SECTION("A sliced object is provided right away") {
        start(Slic3r::slaposDrillHoles);
        CHECK(completed_count == 2);
        CHECK_FALSE(request.running());
        CHECK(completed_object);
    }
}

TEST_CASE_METHOD(SLAObjectRequestFixture, "SLA object request completes without support points when supports are disabled", "[slicing][sla-object-request][timeout]")
{
    ConfigPackSLA& config{std::get<ConfigPackSLA>(bed.config)};
    config.sla_print_settings.items.opt("supports_enable").set(false);
    config.sla_print_settings.items.opt("pad_enable").set(false);
    update_process();

    start(Slic3r::slaposSupportPoints);
    REQUIRE(wait_for_completion());
    REQUIRE(completed_object);
    CHECK(completed_object->mesh);
    CHECK_FALSE(completed_object->support_points);

    // The steps are done, the request must not wait for slicing that does nothing.
    start(Slic3r::slaposSupportPoints);
    REQUIRE(wait_for_completion());
    CHECK_FALSE(request.running());
}

TEST_CASE_METHOD(SLAObjectRequestFixture, "Canceled SLA object request doesn't complete", "[slicing][sla-object-request][timeout]")
{
    using namespace std::chrono_literals;

    update_process();

    start(Slic3r::slaposSupportPoints);
    REQUIRE(request.running());
    request.cancel();

    // Let the slicing that was already requested finish.
    REQUIRE(wait_for_status(dispatcher, status_listener, 30s, [](const StatusEvents& events) {
        const bool was_running = std::ranges::any_of(events, [](const StatusEvent& event) {
            return event.status_code == StatusCode::Running;
        });
        return was_running && events.back().status_code == StatusCode::Modified;
    }));
    CHECK(completed_count == 0);
}

TEST_CASE_METHOD(SLAObjectRequestFixture, "SLA object request can ignore an object not updated yet", "[slicing][sla-object-request][timeout]")
{
    update_process();
    start(Slic3r::slaposSupportPoints);
    REQUIRE(wait_for_completion());
    REQUIRE(completed_object);
    REQUIRE(completed_object->support_points);
    const auto old_points{completed_object->support_points};

    // The change of the support points reaches the cache only when dispatched.
    ConfigPackSLA& config{std::get<ConfigPackSLA>(bed.config)};
    config.sla_print_settings.items.opt("support_points_density_relative").set(150);
    slicing.update_process(
        bed.model,
        bed.project_metadata,
        bed.preset_metadata,
        bed.config,
        bed.bed_instance
    );

    request.start(id, bed.model.objects.front()->id(), Slic3r::slaposSupportPoints, false);
    CHECK(completed_count == 1);
    REQUIRE(wait_for_completion());
    REQUIRE(completed_object);
    REQUIRE(completed_object->support_points);
    CHECK(completed_object->support_points != old_points);
}
