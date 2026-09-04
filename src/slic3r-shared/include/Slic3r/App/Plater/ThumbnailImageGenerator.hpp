#pragma once

#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "libslic3r/IThumbnailImageGenerator.hpp"
#include "Slic3r/App/Plater/ThumbnailRenderer.hpp"

#include <deque>

namespace Slic3r::Domain {
class Workbench;
} // namespace Slic3r::Domain

namespace Slic3r::App::Scene {
class IProjectSceneProvider;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Plater {

class ThumbnailImageGenerator : public Biz::Slicing::IThumbnailImageGenerator
{
public:
    void init(
        const Domain::Workbench& workbench,
        Render::Device& device,
        Scene::IProjectSceneProvider& scene_provider
    );

    std::future<Biz::Slicing::ThumbnailImageResults> enqueue_thumbnail_requests(
        const Biz::Slicing::ThumbnailImageRequests& requests
    ) override;
    void handle_enqueued_requests() override;

    bool initialized() const;

private:
    struct Item
    {
        Biz::Slicing::ThumbnailImageRequests requests;
        std::promise<Biz::Slicing::ThumbnailImageResults> promise;

        bool operator==(const Item& other) const
        {
            return requests == other.requests;
        }
    };

    const Domain::Workbench* m_workbench{nullptr};
    Render::Device* m_device{nullptr};
    Scene::IProjectSceneProvider* m_scene_provider{nullptr};
    std::unique_ptr<ThumbnailRenderer> m_renderer;
    std::deque<Item> m_queue;
};

} // namespace Slic3r::App::Plater
