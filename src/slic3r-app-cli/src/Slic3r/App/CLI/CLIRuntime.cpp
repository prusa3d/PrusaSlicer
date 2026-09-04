#include "Slic3r/App/CLI/CLIRuntime.hpp"

#include "Slic3r/App/Init.hpp"
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

CLIThumbnailImageGenerator::CLIThumbnailImageGenerator(const std::vector<std::string>& input_files)
{
    if (input_files.size() == 1 && boost::iends_with(input_files[0], ".3mf")) {
        m_input_3mf_filename = input_files[0];
    }
}

std::future<ThumbnailImageResults> CLIThumbnailImageGenerator::enqueue_thumbnail_requests(
    const ThumbnailImageRequests& thumbnail_requests
)
{
    std::promise<ThumbnailImageResults> promise;
    std::future<ThumbnailImageResults> result{promise.get_future()};
    if (m_input_3mf_filename.empty()) {
        promise.set_value(ThumbnailImageResults{});
        return result;
    }

    // Create a list of all sizes that we need to generate.
    std::vector<Size> requested_sizes;
    for (const ThumbnailImageRequest& thumbnail_request : thumbnail_requests) {
        for (const Size& requested_size : thumbnail_request.params.sizes) {
            requested_sizes.emplace_back(requested_size);
        }
    }

    // Now actually generate the thumbnails:
    std::vector<Image> source_images =
        get_thumbnail_images_from_3mf(m_input_3mf_filename, requested_sizes);

    if (source_images.empty()
        || source_images.size() != requested_sizes.size()
        || std::any_of(
            source_images.begin(),
            source_images.end(),
            [](const Image& image) { return image.width() == 0 || image.height() == 0; }
        ))
    {
        promise.set_value(ThumbnailImageResults{});
        return result;
    }

    ThumbnailImageResults thumbnail_results;
    size_t source_image_index = 0;
    for (const ThumbnailImageRequest& thumbnail_request : thumbnail_requests) {
        ThumbnailImageResult thumbnail_result;
        thumbnail_result.type            = thumbnail_request.type;
        thumbnail_result.project_id      = thumbnail_request.params.project_id;
        thumbnail_result.bed_instance_id = thumbnail_request.params.bed_instance_id;

        for (size_t size_index = 0; size_index < thumbnail_request.params.sizes.size();
             ++size_index)
        {
            thumbnail_result.images.push_back(source_images[source_image_index++]);
            ASSERT(
                thumbnail_result.images.back().width()
                == thumbnail_request.params.sizes[size_index].width
            );
            ASSERT(
                thumbnail_result.images.back().height()
                == thumbnail_request.params.sizes[size_index].height
            );
        }

        thumbnail_results.push_back(std::move(thumbnail_result));
    }

    ASSERT(source_image_index == source_images.size());
    promise.set_value(std::move(thumbnail_results));

    return result;
}

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
    m_thumbnail_image_generator{init_params.input.input_files}
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
