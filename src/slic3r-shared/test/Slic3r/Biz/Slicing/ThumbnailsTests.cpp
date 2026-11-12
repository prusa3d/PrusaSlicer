#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/Slicing/TestUtils.hpp"

using namespace Slic3r;
using Biz::Slicing::StatusCode;
using Domain::SlicingId;
using Test::SlicingFixture;
using Test::get_cubes_model;
using Test::wait_for_status;

namespace {

// Extends the basic StatusListener to also capture errors reported alongside
// status updates, so tests can inspect the ErrorCode.
struct ErrorCapturingListener : public Biz::Slicing::IStatusListener
{
    void on_status_changed(
        const Biz::Slicing::StatusUpdate status_update,
        const Domain::SlicingId /*id*/
    ) override
    {
        for (const auto& error : status_update.errors_to_append) {
            errors.push_back(error);
        }
    }

    std::vector<Biz::Slicing::Error> errors;
};

} // namespace


// When the 'thumbnails' printer-config option is empty, slicing must complete
// normally.  Previously this caused a crash because empty sizes were forwarded
// to ThumbnailImageGenerator.
TEST_CASE_METHOD(
    SlicingFixture,
    "Empty thumbnails config: slicing finishes successfully",
    "[slicing][thumbnails][timeout]")
{
    using namespace std::chrono_literals;

    auto model_on_bed = get_cubes_model(1, 5);
    std::get<Domain::ConfigPackFDM>(model_on_bed.config).printer.items.opt("thumbnails").set(std::string(""));

    const SlicingId id{0, model_on_bed.bed_instance.id().id};
    slicing.update_process(
        model_on_bed.model,
        model_on_bed.project_metadata,
        model_on_bed.preset_metadata,
        model_on_bed.config,
        model_on_bed.bed_instance
    );
    slicing.slice_bed(id);

    REQUIRE(wait_for_status(dispatcher, status_listener, 5s, [](const auto& events) {
        return !events.empty() && events.back().status_code == StatusCode::Finished;
    }));
}

// When the 'thumbnails' printer-config option is malformed, slicing must report
// an InvalidThumbnailRequest error through the normal status channel instead of
// propagating an unhandled exception.
TEST_CASE_METHOD(
    SlicingFixture,
    "Malformed thumbnails config: slicing reports InvalidThumbnailRequest error",
    "[slicing][thumbnails][timeout]")
{
    using namespace std::chrono_literals;

    ErrorCapturingListener error_listener;
    slicing.add_listener<Biz::Slicing::IStatusListener>(&error_listener);

    auto model_on_bed = get_cubes_model(1, 5);
    std::get<Domain::ConfigPackFDM>(model_on_bed.config).printer.items.opt("thumbnails").set(std::string("160x/PNG"));

    const SlicingId id{0, model_on_bed.bed_instance.id().id};
    slicing.update_process(
        model_on_bed.model,
        model_on_bed.project_metadata,
        model_on_bed.preset_metadata,
        model_on_bed.config,
        model_on_bed.bed_instance
    );
    slicing.slice_bed(id);

    REQUIRE(wait_for_status(dispatcher, status_listener, 5s, [](const auto& events) {
        return !events.empty()
            && (events.back().status_code == StatusCode::InvalidData
                || events.back().status_code == StatusCode::Finished);
    }));

    REQUIRE(status_listener.status_events.back().status_code == StatusCode::InvalidData);
    REQUIRE_FALSE(error_listener.errors.empty());
    REQUIRE(error_listener.errors.front().code == Biz::Slicing::ErrorCode::InvalidThumbnailRequest);
}
