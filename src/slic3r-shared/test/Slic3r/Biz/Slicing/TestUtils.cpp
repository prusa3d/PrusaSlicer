#include "TestUtils.hpp"

#include "Slic3r/Uuid.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/TestUtils/HwConfigUtils.hpp"

#include <boost/filesystem.hpp>

namespace Slic3r::Test {

using Domain::ConfigPack;
using Slic3r::Domain::Vec3f;
using Biz::Algorithms::ModelObject::ensure_on_bed;
using Biz::Algorithms::ModelObject::add_volume;

void precise_sleep(const std::chrono::milliseconds duration) {
    const auto start = std::chrono::high_resolution_clock::now();
    const auto nano_duration{duration_cast<std::chrono::nanoseconds>(duration)};
    while (std::chrono::high_resolution_clock::now() - start < nano_duration) {
        // CPU intensive but precise. Suitable just for testing.
    }
}

Domain::Model generate_cubes(const int count, const int row_size)
{
    const float size{20};
    Domain::Model model;
    for (int i{}; i < count; ++i) {
        const int row{i / row_size};
        const int column{i % row_size};

        namespace TriMesh = Biz::Algorithms::TriangleMesh;
        Domain::TriangleMesh cube_mesh = TriMesh::make_cube(size, size, size);
        cube_mesh.translate(Vec3f{column * (size + 5.0f), row * (size + 5.0f), 0.0f});

        Domain::ModelObject* model_object = model.add_object();
        add_volume(model_object, cube_mesh);
        model_object->add_instance();
        ensure_on_bed(*model_object);
    }
    return model;
}


double get_cubes_filament_used(const Domain::Model &model) {
    return model.objects.size() * 1483.0;
}

Domain::Preset::SelectedPresetMetadata get_selected_preset_metadata()
{
    Domain::Preset::SelectedPresetMetadata metadata;
    return metadata;
}

Domain::ConfigPack get_config(Domain::PrinterTechnology technology)
{
    if (technology == Domain::PrinterTechnology::FFF) {
        Domain::ConfigPackFDM config;
        config.print.items.opt("skirts").set(0);
        // Make print statitistics non-zero.
        config.filament.at(0).items.opt("filament_density").set(1.0);
        config.filament.at(0).items.opt("filament_cost").set(44.0);
        return config;
    } else {
        return Domain::ConfigPackSLA{};
    }
}

ModelOnBed::ModelOnBed(Domain::Model&& model, ConfigPack&& input_config)
    : model{std::move(model)}, config{std::move(input_config)}, bed_instance{ModelOnBed::bed}
{
    for (Domain::ModelObject* object : this->model.objects) {
        for (Domain::ModelInstance* instance : object->instances) {
            this->bed_instance.model_instances.push_back(instance);
        }
    }

    if (std::holds_alternative<Domain::ConfigPackFDM>(this->config)) {
        auto config_fdm{std::get<Domain::ConfigPackFDM>(this->config)};
        preset_metadata.hw_config =
            create_dummy_hw_config(config_fdm.tool.size(), 0.4, Domain::PrinterTechnology::FFF);
    } else if (std::holds_alternative<Domain::ConfigPackSLA>(this->config)) {
        preset_metadata.hw_config =create_dummy_hw_config(0, 0.0, Domain::PrinterTechnology::SLA);
    } else {
        PANIC("Invalid config pack!");
    }
}

Domain::Bed ModelOnBed::bed{};

ModelOnBed
get_cubes_model(const int count, const int row_size, Domain::PrinterTechnology technology)
{
    return {generate_cubes(count, row_size), get_config(technology)};
}

std::ostream& operator<<(std::ostream& output, const StatusEvent& status_event)
{
    return output
        << "{status_code: "
        << status_event.status_code
        << ", slicing_id: "
        << status_event.slicing_id
        << "}";
}

bool operator==(const StatusEvent& a, const StatusEvent& b)
{
    return a.status_code == b.status_code && a.slicing_id == b.slicing_id;
}

using StatusEvents = std::vector<StatusEvent>;

[[nodiscard]] bool wait_for_status(
    Biz::Platform::IMainThreadDispatcher& dispatcher,
    const StatusListener& status_listener,
    const std::chrono::seconds timeout,
    const std::function<bool(StatusEvents)>& condition
)
{
    using namespace std::chrono_literals;
    using std::chrono::high_resolution_clock;
    using Biz::Platform::PlatformServices;

    const auto start{high_resolution_clock::now()};
    while(true) {
        dispatcher.dispatch_enqueued();
        const std::vector<StatusEvent> status_events{status_listener.status_events};
        if (!status_events.empty() && condition(status_events)) {
            return true;;
        }
        const auto now{high_resolution_clock::now()};
        if (duration_cast<std::chrono::seconds>(now - start) > timeout) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
}

using Biz::Slicing::FDMResult;
using Domain::SlicingId;
using Biz::Slicing::IStatusListener;

void ResultListener::on_fdm_result_changed(
    FDMResult&& result, const SlicingId id
)
{
    std::shared_ptr<const Biz::libpgcode::LineView> gcode_ptr = result.const_gcode();
    if (gcode_ptr) { 
        gcodes[id.bed_instance_id] = std::move(gcode_ptr);
    }
}

SlicingFixture::SlicingFixture() {
    slicing.on_selected_project_changed(0);
    slicing.add_listener<IStatusListener>(&status_listener);
}

SlicingFixture::~SlicingFixture() {
    dispatcher.close();
}

}
