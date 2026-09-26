#include "Slic3r/App/CLI/CLIRuntime.hpp"

#include "Slic3r/App/Init.hpp"
#include "CLIThumbnailRenderer.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/AppInstance/AppInstanceMessageHandlerFactory.hpp"
#include "Slic3r/Biz/Format/3mf.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/Preset/IO/BundlePaths.hpp"
#include "Slic3r/Biz/SecretStoreDummy.hpp"
#include "Slic3r/Domain/Image.hpp"

#include <chrono>
#include <thread>

#include <boost/algorithm/string/predicate.hpp>

using namespace Slic3r;
using namespace Slic3r::Biz;

using Slic3r::App::Platform::StdMainThreadDispatcher;
using Slic3r::Biz::ProjectInteractor;
using Slic3r::Biz::SecretStoreDummy;
using Slic3r::Biz::Platform::IMainThreadDispatcher;
using Slic3r::Biz::Platform::PlatformServices;
using Slic3r::Biz::Platform::JobManager::JobManager;
using Slic3r::Biz::Platform::JobManager::JobManagerStatus;
using Slic3r::Biz::PrintHost::PrintHostJobProgressPayload;
using Slic3r::Biz::Slicing::ThumbnailImageRequest;
using Slic3r::Biz::Slicing::ThumbnailImageRequests;
using Slic3r::Biz::Slicing::ThumbnailImageResult;
using Slic3r::Biz::Slicing::ThumbnailImageResults;
using Slic3r::Domain::Image;
using Slic3r::Domain::JobStatus;
using Slic3r::Domain::Size;

namespace Slic3r::App::CLI {

CLIThumbnailImageGenerator::CLIThumbnailImageGenerator(const Domain::Workbench& workbench) :
    m_workbench(&workbench)
{}

std::future<ThumbnailImageResults> CLIThumbnailImageGenerator::enqueue_thumbnail_requests(
    const ThumbnailImageRequests& requests
)
{
    auto promise = std::make_shared<std::promise<ThumbnailImageResults>>();
    auto future = promise->get_future();
    if (!m_workbench || requests.empty()) {
        promise->set_value({});
        return future;
    }

    // Access the live project and create/use/destroy its GL context on the CLI
    // main thread. Slicing workers only wait for the resulting images.
    if (!PlatformServices::instance().main_thread_dispatcher().dispatch_on_main_thread(
            [this, requests, promise]() {
                try {
                    promise->set_value(generate(requests));
                } catch (const std::exception& error) {
                    SPDLOG_WARN("CLI thumbnails skipped: {}", error.what());
                    promise->set_value({});
                }
            })) {
        promise->set_value({});
    }
    return future;
}

ThumbnailImageResults CLIThumbnailImageGenerator::generate(
    const ThumbnailImageRequests& requests
) const
{
    ThumbnailImageResults results;
    for (const auto& request : requests) {
        if (request.params.sizes.empty()
            || request.params.pixel_format != Domain::PixelFormat::RGBA8
            || request.type == Biz::ThumbnailType::Object) continue;
        const auto* project = m_workbench->find_project_by_id(request.params.project_id);
        if (!project) continue;
        if (request.type != Biz::ThumbnailType::Scene
            && !project->find_bed_instance_by_id(request.params.bed_instance_id)) continue;

        Domain::Images images;
        const std::string filename = project->loaded_file_path().string();
        size_t bed_count = 0;
        for (const auto& config : project->config_containers())
            bed_count += config->bed_instances().size();

        // A stored scene preview cannot represent an individual bed in a
        // multi-bed project. Render that bed instead of reusing the scene.
        if (boost::iends_with(filename, ".3mf")
            && (request.type == Biz::ThumbnailType::Scene || bed_count == 1)) {
            images = get_thumbnail_images_from_3mf(filename, request.params.sizes);
        }
        bool valid = images.size() == request.params.sizes.size();
        for (size_t i = 0; valid && i < images.size(); ++i) {
            valid = images[i].width() > 0 && images[i].height() > 0
                && images[i].width() == request.params.sizes[i].width
                && images[i].height() == request.params.sizes[i].height;
        }
        if (!valid) {
            images = CLIThumbnails::render_thumbnails(*project, request, resources_dir());
        }
        if (!images.empty()) {
            results.push_back({request.type, request.params.project_id,
                               request.params.bed_instance_id, std::move(images)});
        }
    }
    return results;
}

// Requests are handled by the dispatcher serviced by CLIRuntime::wait_until.
void CLIThumbnailImageGenerator::handle_enqueued_requests() {}

void ExportFinishedJobManagerStatusListener::on_job_manager_status_changed(
    const JobManagerStatus& job_manager_status
)
{
    for (const auto& [job_name, job_progress] : job_manager_status) {
        if (!job_name.starts_with("printhost")) {
            continue;
        }

        std::string payload_message;
        if (const PrintHostJobProgressPayload* progress_payload =
                std::any_cast<PrintHostJobProgressPayload>(&job_progress.progress_detail.payload))
        {
            payload_message = progress_payload->message;
        }

        if (job_progress.status == JobStatus::Failed) {
            export_finished = true;
            export_error    = payload_message;
        } else if (job_progress.status == JobStatus::Finished) {
            export_finished = true;
            export_error    = std::nullopt;
        }
    }
}

void ProjectLoadResultListener::on_project_loaded(Domain::SelectionId project_id)
{
    loaded_project_id = project_id;
}

void ProjectLoadResultListener::on_project_load_failed(const std::string& error)
{
    load_error = error;
}

bool ProjectLoadResultListener::finished() const
{
    return loaded_project_id.has_value() || load_error.has_value();
}

CLIRuntime::CLIRuntime(const InitParams& init_params) :
    m_thumbnail_image_generator{m_workbench}
{
    PlatformServices& platform_services = PlatformServices::instance();
    platform_services.set_secret_store(std::make_unique<SecretStoreDummy>());
    platform_services.set_job_manager(nullptr);
    platform_services.set_app_instance_message_handler(nullptr);
    platform_services.set_main_thread_dispatcher(std::make_unique<StdMainThreadDispatcher>());
    platform_services.set_job_manager(
        std::make_unique<JobManager>(platform_services.main_thread_dispatcher())
    );
    platform_services.set_app_instance_message_handler(
        Biz::AppInstance::create_app_instance_message_handler(platform_services.main_thread_dispatcher())
    );

    m_project_interactor.emplace(
        m_workbench,
        platform_services.main_thread_dispatcher(),
        m_thumbnail_image_generator
    );

    // subcommands do not need loaded presets
    if (!init_params.action.has_subcommand_action()) {
        m_project_interactor->preset_interactor().load_preset_bundle(
            Preset::IO::BundlePaths::make_standard_runtime()
        );
    }
}

CLIRuntime::~CLIRuntime()
{
    dispatcher().close();
    m_project_interactor.reset();
}

const ProjectInteractor& CLIRuntime::project_interactor() const
{
    ASSERT(m_project_interactor.has_value());
    return m_project_interactor.value();
}

ProjectInteractor& CLIRuntime::project_interactor()
{
    ASSERT(m_project_interactor.has_value());
    return m_project_interactor.value();
}

IMainThreadDispatcher& CLIRuntime::dispatcher()
{
    return PlatformServices::instance().main_thread_dispatcher();
}

void CLIRuntime::wait_until(const std::function<bool()>& predicate)
{
    while (true) {
        this->dispatcher().dispatch_enqueued();
        if (predicate()) {
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace Slic3r::App::CLI
