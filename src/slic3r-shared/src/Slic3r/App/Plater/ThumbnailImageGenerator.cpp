#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Domain/Workbench.hpp"
#include "Slic3r/App/Scene/IProjectSceneProvider.hpp"
#include "Slic3r/App/Scene/Scene.hpp"

#include "Slic3r/Assert.hpp"
#include "libslic3r/ThumbnailImageRequest.hpp"
#include "libslic3r/ThumbnailImageResult.hpp"

namespace Slic3r::App::Plater {

using Biz::Slicing::ThumbnailImageRequests;
using Biz::Slicing::ThumbnailImageResult;
using Biz::Slicing::ThumbnailImageResults;

void ThumbnailImageGenerator::init(
    const Domain::Workbench& workbench,
    Render::Device& device,
    Scene::IProjectSceneProvider& scene_provider
)
{
    m_workbench      = &workbench;
    m_device         = &device;
    m_scene_provider = &scene_provider;

    m_renderer = std::make_unique<ThumbnailRenderer>(device);
}

std::future<ThumbnailImageResults> ThumbnailImageGenerator::enqueue_thumbnail_requests(
    const ThumbnailImageRequests& requests
)
{
    ASSERT(initialized());
    DEBUG_ASSERT(!requests.empty());

    std::promise<ThumbnailImageResults> promise;
    std::future<ThumbnailImageResults> future{promise.get_future()};

    Biz::Platform::IMainThreadDispatcher& dispatcher{
        Biz::Platform::PlatformServices::instance().main_thread_dispatcher()
    };

    if (!dispatcher.dispatch_on_main_thread([this, requests, _promise = std::move(promise)]() mutable {
            Item item{.requests = requests, .promise = std::move(_promise)};

            if (std::ranges::find(m_queue, item) == m_queue.end()) {
                m_queue.push_back(std::move(item));
            }

            Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
        }))
    {
        SPDLOG_INFO("Thumbnail generation request not dispatched!");
    }
    return future;
}

void ThumbnailImageGenerator::handle_enqueued_requests()
{
    ASSERT(initialized());
    while (!m_queue.empty()) {
        Item& item = m_queue.front();

        ThumbnailImageResults results;
        for (const auto& request : item.requests) {
            ASSERT(!request.params.sizes.empty());
            const Domain::Project* project = m_workbench->find_project_by_id(request.params.project_id);
            if (project == nullptr) {
                // The project may have been removed in the meantime.
                continue;
            }
            Scene::Scene& scene = m_scene_provider->project_scene(request.params.project_id);

            ThumbnailImageResult& result = results.emplace_back(ThumbnailImageResult());
            result.type                  = request.type;
            result.project_id            = request.params.project_id;
            result.bed_instance_id       = request.params.bed_instance_id;

            switch (request.type) {
            // gallery
            case Biz::ThumbnailType::Object: {
                break;
            }
            // objects list
            case Biz::ThumbnailType::SceneBed: {
                Plater::ThumbnailRendererParams params{
                    .scene        = scene,
                    .pixel_format = request.params.pixel_format,
                    .sizes        = request.params.sizes
                };
                result.images = m_renderer->generate_bed_thumbnails(
                    params,
                    *project,
                    request.params.bed_instance_id,
                    request.params.bed_instance_with_error,
                    Scene::CameraProjectionType::Orthographic
                );
                break;
            }
            // gcode
            case Biz::ThumbnailType::SlicingBed: {
                Plater::ThumbnailRendererParams params{
                    .scene        = scene,
                    .pixel_format = request.params.pixel_format,
                    .sizes        = request.params.sizes
                };
                result.images = m_renderer->generate_gcode_thumbnails(
                    params,
                    *project,
                    request.params.bed_instance_id,
                    Scene::CameraProjectionType::Orthographic
                );
                break;
            }
            // 3mf
            case Biz::ThumbnailType::Scene: {
                Plater::ThumbnailRendererParams params{
                    .scene        = scene,
                    .pixel_format = request.params.pixel_format,
                    .sizes        = request.params.sizes
                };
                result.images = m_renderer->generate_3mf_thumbnails(
                    params,
                    *project,
                    Scene::CameraProjectionType::Orthographic
                );
                break;

                break;
            }
            }
        }

        item.promise.set_value(std::move(results));
        m_queue.pop_front();
    }
}

bool ThumbnailImageGenerator::initialized() const
{
    return m_workbench != nullptr && m_device != nullptr && m_scene_provider != nullptr && m_renderer;
}

} // namespace Slic3r::App::Plater
