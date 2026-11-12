#include "Slic3r/App/ThumbnailStoreUpdater.hpp"
#include "Slic3r/App/ObjectListWindow.hpp"
#include "Slic3r/App/ThumbnailStore.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/Context.hpp"
#include "Slic3r/App/Render/TextureManager.hpp"

#include <fmt/core.h>

namespace Slic3r::App {

using Biz::Slicing::ThumbnailImageRequest;
using Biz::Slicing::ThumbnailImageRequests;
using Biz::Slicing::ThumbnailImageResults;

void ThumbnailStoreUpdater::on_bed_changed(Domain::SelectionId project_id, const Domain::BedRefs& bed_refs,
    const Scene::BedError& bed_error)
{
    if (m_thumbnail_results.valid()) {
        // queue to process later
        m_queue.push(QueueItem{project_id, bed_refs, bed_error});
        return;
    }

    ThumbnailImageRequests requests;
    requests.reserve(bed_refs.size() + 1);

    // thumbnails for object list
    for (const auto& bed_ref : bed_refs) {
        ThumbnailImageRequest& request = requests.emplace_back(ThumbnailImageRequest());
        request.type                           = Biz::ThumbnailType::SceneBed;
        request.params.project_id              = project_id;
        request.params.bed_instance_id         = bed_ref.instance_id;
        request.params.bed_instance_with_error = bed_error.contains(Domain::SlicingId{ project_id, bed_ref.instance_id });
        request.params.pixel_format            = Domain::PixelFormat::RGBA8;
        request.params.sizes                   = {{128, 128}};
    }

    // thumbnails for 3mf
    ThumbnailImageRequest& request = requests.emplace_back(ThumbnailImageRequest());
    request.type                           = Biz::ThumbnailType::Scene;
    request.params.project_id              = project_id;
    request.params.bed_instance_id         = 0;
    request.params.bed_instance_with_error = false;
    request.params.pixel_format            = Domain::PixelFormat::RGBA8;
    request.params.sizes                   = {{256, 256}};

    m_thumbnail_results = m_thumbnail_image_generator.enqueue_thumbnail_requests(requests);
}

void ThumbnailStoreUpdater::update(Render::Device& device, ThumbnailUpdateCallback callback)
{
    if (m_thumbnail_results.valid()) {
        std::future_status status = m_thumbnail_results.wait_for(std::chrono::milliseconds(5));
        if (status == std::future_status::ready) {
            ThumbnailImageResults thumbnail_results = m_thumbnail_results.get();
            if (!thumbnail_results.empty()) {
                // all results have the same project_id, so we can use the first one
                Domain::SelectionId project_id = thumbnail_results.front().project_id;
                Plater::BedThumbnailTextures textures;
                for (auto& t : thumbnail_results) {
                    switch (t.type) {
                    // thumbnails for object list
                    case Biz::ThumbnailType::SceneBed: {
                        if (!t.images.empty()) {
                            for (const auto& image : t.images) {
                                std::string name = fmt::format(
                                    "thumbnail_bed_{}_{}_{}x{}",
                                    t.project_id,
                                    t.bed_instance_id,
                                    image.width(),
                                    image.height()
                                );
                                Plater::BedThumbnailTexture& tex = textures.emplace_back(
                                    Plater::BedThumbnailTexture()
                                );
                                tex.project_id      = t.project_id;
                                tex.bed_instance_id = t.bed_instance_id;
                                tex.thumbnail = device.context().texture_manager().get_or_create_dynamic(
                                    name, image.format(), image.width(), image.height());
                                tex.thumbnail->set_data(
                                    image.format(), 0, image.width(), image.height(), image.pixels.data(), image.pixels.size());
                            }
                        }
                        break;
                    }
                    // thumbnail for 3mf
                    case Biz::ThumbnailType::Scene: {
                        if (!t.images.empty())
                            m_thumbnail_store->update(project_id, std::move(t.images.front()));
                        break;
                    }
                    default: {
                        break;
                    }
                    }
                }

                m_thumbnail_store->update(project_id, textures);
                if (callback != nullptr)
                    callback(textures);
            }
        }
    }

    if (!m_queue.empty()) {
        // process queued items
        const QueueItem& item = m_queue.front();
        on_bed_changed(item.project_id, item.bed_refs, item.bed_error);
        m_queue.pop();
    }
}

} // namespace Slic3r::App
