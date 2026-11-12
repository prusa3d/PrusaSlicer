#pragma once

#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"
#include "Slic3r/Biz/libpgcode/LineView.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/Model.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>

namespace Slic3r::Test {
void precise_sleep(const std::chrono::milliseconds duration);

Domain::Model generate_cubes(const int count, const int row_size);

double get_cubes_filament_used(const Domain::Model &model);

Domain::Preset::SelectedPresetMetadata get_selected_preset_metadata();
Domain::ConfigPack get_config(
    Domain::PrinterTechnology technology = Domain::PrinterTechnology::FFF
);

struct ModelOnBed {
    ModelOnBed(Domain::Model&& model, Domain::ConfigPack&& config);

    static Domain::Bed bed;

    Domain::Model model;
    Domain::ProjectMetadata project_metadata;
    Domain::Preset::SelectedPresetMetadata preset_metadata;
    Domain::ConfigPack config;
    Domain::BedInstance bed_instance;
};

ModelOnBed get_cubes_model(
    const int count,
    const int row_size,
    Domain::PrinterTechnology technology = Domain::PrinterTechnology::FFF
);

struct StatusEvent {
    Biz::Slicing::StatusCode status_code;
    Domain::SlicingId slicing_id;
};

std::ostream& operator<<(std::ostream& output, const StatusEvent& status_event);

bool operator==(const StatusEvent& a, const StatusEvent& b);
using StatusEvents = std::vector<StatusEvent>;

struct StatusListener : public Biz::Slicing::IStatusListener {
    virtual void on_status_changed(const Biz::Slicing::StatusUpdate status_update, const Domain::SlicingId id) override {
        if (status_update.code) {
            status_events.push_back(StatusEvent{*status_update.code, id});
        }
    }

    std::vector<StatusEvent> status_events;
};

[[nodiscard]] bool wait_for_status(
    Biz::Platform::IMainThreadDispatcher& dispatcher,
    const StatusListener& status_listener,
    const std::chrono::seconds timeout,
    const std::function<bool(StatusEvents)>& condition
);

using GCodes = std::map<Domain::SelectionId, std::shared_ptr<const Biz::libpgcode::LineView>>;
struct ResultListener : public Biz::Slicing::IFDMResultListener
{
    void on_fdm_result_changed(
        Biz::Slicing::FDMResult&& result,
        const Domain::SlicingId id
    ) override;

    GCodes gcodes;
};

class MockThumbnailImageGenerator : public Biz::Slicing::IThumbnailImageGenerator
{
public:
    std::future<Biz::Slicing::ThumbnailImageResults> enqueue_thumbnail_requests(
        const Biz::Slicing::ThumbnailImageRequests& requests
    ) override
    {
        std::promise<Biz::Slicing::ThumbnailImageResults> promise;
        std::future<Biz::Slicing::ThumbnailImageResults> result{promise.get_future()};
        promise.set_value(Biz::Slicing::ThumbnailImageResults{});
        return result;
    }

    void handle_enqueued_requests() override {}
};

struct SlicingFixture {
    SlicingFixture();
    ~SlicingFixture();
public:
    App::Platform::StdMainThreadDispatcher dispatcher;
    MockThumbnailImageGenerator thumbnail_image_generator;
    Biz::Slicing::SlicingInteractor slicing{dispatcher, thumbnail_image_generator};
    StatusListener status_listener;
};

}
