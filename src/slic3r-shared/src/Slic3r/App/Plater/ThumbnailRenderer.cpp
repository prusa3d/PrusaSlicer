#include "Slic3r/App/Plater/ThumbnailRenderer.hpp"
#include "Slic3r/App/Render/Framebuffer.hpp"
#include "Slic3r/App/Render/FramebufferManager.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Plater/ScopedBedThumbnailSceneCustomizer.hpp"
#include "Slic3r/App/Plater/Scoped3mfThumbnailSceneCustomizer.hpp"
#include "Slic3r/App/Plater/ScopedGCodeThumbnailSceneCustomizer.hpp"
#include "Slic3r/App/Scene/NodeVisitor.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/Biz/Algorithms/ImageUtils.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/App/Scene/CameraHelper.hpp"

#include "Slic3r/Assert.hpp"

using Slic3r::Domain::ColorRGBA;
using Slic3r::App::Scene::SceneNodeTag;

namespace Slic3r::App::Plater {

using Domain::Image;
using Domain::Images;
using Biz::Algorithms::ImageUtils::flip_vertical;

Images ThumbnailRenderer::generate_thumbnails(const ThumbnailRendererParams& params, Scene::Camera& camera)
{
    if (params.sizes.empty())
        PANIC("No thumbnail sizes specified");
    if (params.scene.root().children().empty())
        PANIC("Empty scene");

    Images ret;
    ret.reserve(params.sizes.size());

    // we need to modify the camera viewport, which changes for every thumbnail to produce
    // we may also modify the camera zoom, if params.zoom_aabb is set
    Scene::Scene* scene = const_cast<Scene::Scene*>(&params.scene);
    Scene::MinimalSceneRenderCustomizer render_customizer;

    for (const auto& size : params.sizes) {
        if (size.width * size.height > 0) {
            // set camera viewport
            Render::Rect viewport = {0, 0, size.width, size.height};
            camera.set_viewport(viewport);
            if (params.zoom_aabb.has_value())
                zoom_to_box(camera, *params.zoom_aabb);

            // create target framebuffer
            Render::FramebufferCreationData fb_data;
            fb_data.width       = size_t(size.width);
            fb_data.height      = size_t(size.height);
            fb_data.num_samples = 4;
            fb_data.color_attachments.resize(1);
            fb_data.color_attachments[0].format     = params.pixel_format;
            fb_data.color_attachments[0].mag_filter = Render::TextureMagFilter::Linear;
            fb_data.color_attachments[0].min_filter = Render::TextureMinFilter::Linear;
            Render::Framebuffer* fb = m_device.context().framebuffer_manager().create(fb_data);

            // render scene to image
            Image& image = ret.emplace_back(params.pixel_format, size.width, size.height);

            auto cmd_buffer = m_device.create_command_buffer();
            cmd_buffer->bind_framebuffer(*fb);
            cmd_buffer->set_depth_test_enabled(true);
            cmd_buffer->set_cull_face_enabled(true);
            cmd_buffer->set_viewport(viewport);
            cmd_buffer->clear_buffers(true, true);

            scene->render(m_device, *cmd_buffer, &render_customizer, &camera);

            if (fb_data.num_samples > 1) {
                // resolve framebuffer if multisampling is enabled
                Render::FramebufferCreationData resolve_fb_data;
                resolve_fb_data.width  = size_t(size.width);
                resolve_fb_data.height = size_t(size.height);
                resolve_fb_data.color_attachments.resize(1);
                resolve_fb_data.color_attachments[0].format     = params.pixel_format;
                resolve_fb_data.color_attachments[0].mag_filter = Render::TextureMagFilter::Linear;
                resolve_fb_data.color_attachments[0].min_filter = Render::TextureMinFilter::Linear;
                Render::Framebuffer* resolve_fb = m_device.context().framebuffer_manager().create(
                    resolve_fb_data
                );

                cmd_buffer->blit_framebuffer(
                    *fb,
                    *resolve_fb,
                    0,
                    0,
                    size.width,
                    size.height,
                    Render::BlitFramebufferMask::ColorBufferBit,
                    Render::BlitFramebufferFilter::Linear
                );
                // extract image from framebuffer
                cmd_buffer->read_pixels(
                    *resolve_fb,
                    0,
                    0,
                    size.width,
                    size.height,
                    params.pixel_format,
                    image.pixels.data()
                );
                m_device.context().framebuffer_manager().destroy(resolve_fb);
            } else {
                // extract image from framebuffer
                cmd_buffer
                    ->read_pixels(*fb, 0, 0, size.width, size.height, params.pixel_format, image.pixels.data());
            }
            flip_vertical(image);

            cmd_buffer->unbind_framebuffer(*fb);
            m_device.context().framebuffer_manager().destroy(fb);
        } else
            PANIC("Found invalid thumbnail size");
    }

    return ret;
}

Images ThumbnailRenderer::generate_bed_thumbnails(
    const ThumbnailRendererParams& params,
    const Domain::Project& project,
    Domain::SelectionId bed_instance_id,
    bool bed_instance_with_error,
    Scene::CameraProjectionType camera_type
)
{
    const Domain::BedInstance* bed_instance = project.find_bed_instance_by_id(bed_instance_id);
    if (bed_instance == nullptr) {
        SPDLOG_ERROR("Invalid bed instance id {}. Skipping thumbnail generation.", bed_instance_id);
        return Images();
    }

    Scene::Scene& scene = *const_cast<Scene::Scene*>(&params.scene);
    ScopedBedThumbnailSceneCustomizer customizer(scene, project, bed_instance_id, bed_instance_with_error, camera_type);

    // aabb for auto zoom
    Eigen::AlignedBox3d world_aabb;
    Scene::visit(scene.root(), [&](const Scene::Node& n) {
        if (n.has_raycast_component())
            world_aabb.extend(
                n.raycast_component()->world_bounding_box(n.world_transform().matrix()).cast<double>()
            );
    });

    ThumbnailRendererParams mod_params = params;
    mod_params.zoom_aabb               = world_aabb;

    return generate_thumbnails(mod_params, customizer.camera());
}

Images ThumbnailRenderer::generate_object_thumbnails(
    const Domain::ModelObject& object,
    const Domain::Sizes& sizes,
    Scene::CameraProjectionType camera_type,
    std::optional<ColorRGBA> color
)
{
    Images ret;

    auto it = std::find_if(
        object.volumes.begin(),
        object.volumes.end(),
        [](const Domain::ModelVolume* vol) {
        return vol->type() == Domain::ModelVolumeType::MODEL_PART;
    }
    );

    if (it == object.volumes.end()) {
        SPDLOG_ERROR("Object {} has no model part volumes", object.name);
        return ret;
    }

    const Domain::ModelVolume* vol = *it;

    // setup scene for thumbnail generation
    Scene::Scene scene;
    Scene::NodeBuilder builder(scene);

    // add volume
    Scene::TriangleMesh mesh(vol->mesh_ptr());
    std::unique_ptr<Render::Geometry> geom = Render::geometry_from_triangle_mesh(
        m_device,
        mesh.triangles()
    );
    ColorRGBA clr = color.has_value() ? *color : ColorRGBA(1.0f, 0.5f, 0.0f, 1.0f);
    auto material = Render::Material{}
                        .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
                        .set_uniform("uniform_color", clr)
                        .set_transparent(clr.is_transparent());
    builder.transform([vol](auto& xform) { xform = vol->get_matrix(); })
        .set_mesh(geom.get(), material, 0)
        .set_aabb(mesh.aabb_mesh())
        .set_shadows(Render::Shadows{true, true})
        .set_pbr(Scene::DEFAULT_VOLUME_PBRPARAMS);
    scene.add_child(builder.build().release());

    // setup shading
    scene.set_background_enabled(false);
    Scene::Scene::set_shading_type(Scene::ShadingType::PBR);

    // aabb for auto zoom
    Eigen::AlignedBox3d world_aabb;
    Scene::visit(scene.root(), [&](const Scene::Node& n) {
        if (n.has_raycast_component())
            world_aabb.extend(
                n.raycast_component()->world_bounding_box(n.world_transform().matrix()).cast<double>()
            );
    });

    Scene::Camera& camera = scene.camera();
    if (camera_type != camera.cam_projection().type())
        camera.switch_projection_type();

    // setup camera trackball
    Scene::CameraTrackballController& trackball = scene.camera_trackball();
    trackball.set_target(world_aabb.center());
    trackball.set_azimuth_and_zenith(0.25 * std::numbers::pi, 0.75 * std::numbers::pi);
    trackball.set_distance_to_target(world_aabb.diagonal().norm());

    // setup thumbnail generation parameters
    ThumbnailRendererParams params{
        .scene        = scene,
        .zoom_aabb    = world_aabb,
        .pixel_format = Domain::PixelFormat::RGBA8,
        .sizes        = sizes
    };

    // generate thumbnails
    return generate_thumbnails(params, camera);
}

Images ThumbnailRenderer::generate_3mf_thumbnails(
    const ThumbnailRendererParams& params,
    const Domain::Project& project,
    Scene::CameraProjectionType camera_type
)
{
    Scene::Scene& scene = *const_cast<Scene::Scene*>(&params.scene);
    Scoped3mfThumbnailSceneCustomizer customizer(scene, project, camera_type);

    // aabb for auto zoom
    Eigen::AlignedBox3d world_aabb;
    Scene::visit(scene.root(), [&](const Scene::Node& n) {
        const auto* tag = n.tag_of_type<SceneNodeTag>();
        if (tag != nullptr
            && tag->volume_type == Domain::ModelVolumeType::MODEL_PART
            && n.has_raycast_component())
            world_aabb.extend(
                n.raycast_component()->world_bounding_box(n.world_transform().matrix()).cast<double>()
            );
    });

    ThumbnailRendererParams mod_params = params;
    mod_params.zoom_aabb               = world_aabb;

    return generate_thumbnails(mod_params, customizer.camera());
}

Images ThumbnailRenderer::generate_gcode_thumbnails(
    const ThumbnailRendererParams& params,
    const Domain::Project& project,
    Domain::SelectionId bed_instance_id,
    Scene::CameraProjectionType camera_type
)
{
    const Domain::BedInstance* bed_instance = project.find_bed_instance_by_id(bed_instance_id);
    if (bed_instance == nullptr) {
        SPDLOG_ERROR("Invalid bed instance id {}. Skipping thumbnail generation.", bed_instance_id);
        return Images();
    }

    bool printable = false;
    for (const auto& model_instance : bed_instance->model_instances) {
        if (model_instance->printable) {
            printable = true;
            break;
        }
    }
    if (!printable) {
        SPDLOG_ERROR(
            "No printable model instances found for bed instance {}. Skipping thumbnail generation.",
            bed_instance_id
        );
        return Images();
    }

    Scene::Scene& scene = *const_cast<Scene::Scene*>(&params.scene);
    ScopedGCodeThumbnailSceneCustomizer customizer(scene, project, bed_instance_id, camera_type);

    // aabb for auto zoom
    Eigen::AlignedBox3d world_aabb;
    Scene::visit(scene.root(), [&](const Scene::Node& n) {
        const auto* tag = n.tag_of_type<SceneNodeTag>();
        if (tag != nullptr
            && tag->volume_type == Domain::ModelVolumeType::MODEL_PART
            && n.has_raycast_component())
            world_aabb.extend(
                n.raycast_component()->world_bounding_box(n.world_transform().matrix()).cast<double>()
            );
    });

    ThumbnailRendererParams mod_params = params;
    mod_params.zoom_aabb               = world_aabb;

    return generate_thumbnails(mod_params, customizer.camera());
}

} // namespace Slic3r::App::Plater
