#pragma once

#include "Slic3r/Domain/Image.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/Size.hpp"

#include <optional>

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::Domain {
struct BedRef;
struct BedInstance;
class Project;
class ModelObject;
} // namespace Slic3r::Domain

namespace Slic3r::App::Plater {

struct ThumbnailRendererParams
{
    const Scene::Scene& scene;
    std::optional<Eigen::AlignedBox3d> zoom_aabb;
    Domain::PixelFormat pixel_format{Domain::PixelFormat::RGBA8};
    Domain::Sizes sizes;
};

class ThumbnailRenderer
{
public:
    explicit ThumbnailRenderer(Render::Device& device) : m_device(device) {}

    [[nodiscard]] Domain::Images generate_thumbnails(const ThumbnailRendererParams& params, Scene::Camera& camera);
    [[nodiscard]] Domain::Images generate_bed_thumbnails(
        const ThumbnailRendererParams& params,
        const Domain::Project& project,
        Domain::SelectionId bed_instance_id,
        bool bed_instance_with_error,
        Scene::CameraProjectionType camera_type
    );
    [[nodiscard]] Domain::Images generate_object_thumbnails(
        const Domain::ModelObject& object,
        const Domain::Sizes& sizes,
        Scene::CameraProjectionType camera_type,
        std::optional<Domain::ColorRGBA> color = std::nullopt
    );
    [[nodiscard]] Domain::Images generate_3mf_thumbnails(
        const ThumbnailRendererParams& params,
        const Domain::Project& project,
        Scene::CameraProjectionType camera_type
    );
    [[nodiscard]] Domain::Images generate_gcode_thumbnails(
        const ThumbnailRendererParams& params,
        const Domain::Project& project,
        Domain::SelectionId bed_instance_id,
        Scene::CameraProjectionType camera_type
    );

private:
    Render::Device& m_device;
};

} // namespace Slic3r::App::Plater
