#pragma once

#include "Slic3r/App/Plater/IBedVisuallyChangedListener.hpp"
#include "Slic3r/App/ThumbnailStore.hpp"
#include "Slic3r/App/Plater/BedThumbnailTexture.hpp"
#include "Slic3r/App/Scene/BedError.hpp"

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::App {

typedef std::function<void(const Plater::BedThumbnailTextures&)> ThumbnailUpdateCallback;

class ThumbnailStoreUpdater : public Plater::IBedVisuallyChangedListener
{
public:
    ThumbnailStoreUpdater(
        Biz::Slicing::IThumbnailImageGenerator& thumbnail_image_provider,
        std::shared_ptr<App::ThumbnailStore> thumbnail_store
    ) :
        m_thumbnail_image_generator(thumbnail_image_provider),
        m_thumbnail_store(thumbnail_store)
    {}

    /**
     * @name Implementation of IBedVisuallyChangedListener public interface
     * @{
     */
    void on_bed_changed(Domain::SelectionId project_id, const Domain::BedRefs& bed_refs,
        const Scene::BedError& bed_error) override;
    /**@}*/

    void update(Render::Device& device, ThumbnailUpdateCallback callback = nullptr);

private:
    Biz::Slicing::IThumbnailImageGenerator& m_thumbnail_image_generator;
    std::shared_ptr<App::ThumbnailStore> m_thumbnail_store;
    std::future<Biz::Slicing::ThumbnailImageResults> m_thumbnail_results;
    struct QueueItem
    {
        Domain::SelectionId project_id;
        Domain::BedRefs bed_refs;
        Scene::BedError bed_error;
    };
    std::queue<QueueItem> m_queue;
};

} // namespace Slic3r::App
