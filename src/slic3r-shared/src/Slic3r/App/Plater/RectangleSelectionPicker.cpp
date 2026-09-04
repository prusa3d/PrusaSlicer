#include "Slic3r/App/Plater/RectangleSelectionPicker.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/App/Scene/BedNodeTag.hpp"
#include "Slic3r/App/Scene/NodeVisitor.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Render/Types.hpp"
#include "Slic3r/App/Render/ScreenInfo.hpp"
#include "Slic3r/App/Scene/PickerFrustum.hpp"
#include "Slic3r/App/Render/Framebuffer.hpp"
#include "Slic3r/App/Render/FramebufferManager.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/Domain/Transformation.hpp"
#include "Slic3r/App/Platform/CameraSynchData.hpp"
#include "Slic3r/App/Scene/CameraHelper.hpp"

//#include "Slic3r/Biz/Algorithms/ImageUtils.hpp" // debug -> ENABLE_DEBUG_EXPORT_TO_PNG

#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/enumerable_thread_specific.h>

using Slic3r::App::Scene::SceneNodeTag;
using Slic3r::App::Scene::BedNodeTag;

namespace Slic3r::App::Plater {

RectangleSelectionPicker::RectangleSelectionPicker(Render::Device& device)
    : m_device(device)
{
    m_material.set_shader(m_device.context().shader_manager().shader("flat"));
}

void RectangleSelectionPicker::pick(Scene::Scene& scene, const Render::ScreenInfo& screen_info, const Render::Rect& rect)
{
    m_contained_nodes.clear();

    if (rect.width <= 0 || rect.height <= 0)
        return;

    setup_scene(scene, rect);
    if (!m_modified_nodes.empty()) {
        Domain::Image image = render(scene, rect);
        m_contained_nodes = collect_contained_nodes(image);
    }
    restore_scene(scene);
}

static Domain::ColorRGBA index_to_color(size_t id)
{
    return {
        static_cast<unsigned char>((id >> 16) & 0xFF), 
        static_cast<unsigned char>((id >> 8) & 0xFF),
        static_cast<unsigned char>(id & 0xFF),
        255
    };
}

static size_t color_to_index(unsigned char r, unsigned char g, unsigned char b) {
    return (static_cast<size_t>(r) << 16) |
           (static_cast<size_t>(g) << 8) |
           static_cast<size_t>(b);
}

void RectangleSelectionPicker::setup_scene(Scene::Scene& scene, const Render::Rect& rect)
{
    // 
    // setup the scene by disabling non-volume and non-intersecting nodes and modifying intersecting nodes materials, so that
    // each volume is rendered with a unique flat color.
    // if the camera is pointing downward, bed plate and model are rendered too, to avoid picking an object by intersecting
    // its hidden geometry.
    // color index 0 is reserved for bed elements.
    // 

    m_disabled_nodes.clear();
    m_modified_nodes.clear();
    m_bed_nodes.clear();

    Scene::PickerFrustum frustum;
    frustum.set_from(scene.camera(), rect);

    Scene::visit(scene.root(),
        [&](Scene::Node& n) {
            if (n.has_render_component()) {
                const auto* tag = n.tag_of_type<SceneNodeTag>();
                if (tag != nullptr) {
                    // volume nodes
                    if (tag->volume_id != 0 || tag->is_wipe_tower()) {
                        DEBUG_ASSERT(n.has_raycast_component());
                        const auto* rcc = n.raycast_component();
                        // if the node's bounding box intersects the selection frustum
                        // change its material and shadows properties
                        if (rcc->intersects(n.world_transform().matrix(), frustum)) {
                            ModifiedNode& mod_node = m_modified_nodes.emplace_back();
                            mod_node.node = &n;
                            if (n.has_material_override())
                                mod_node.override_material = *n.material_override();
                            mod_node.shadows = { n.render_component()->cast_shadows(), n.render_component()->receive_shadows() };

                            n.render_component()->set_shadows(Render::Shadows{ false, false });
                            m_material.set_uniform("uniform_color", index_to_color(m_modified_nodes.size()));
                            n.set_material_override(m_material);
                        }
                        // if the node's bounding box does not intersect the selection frustum
                        // disable it from the rendering
                        else {
                            if (n.enabled()) {
                                n.set_enabled(false);
                                m_disabled_nodes.push_back(&n);
                            }
                        }
                    }
                }
                // disable non-volume nodes from the rendering
                // apart from bed plate and model when the camera is pointing downward
                else {
                    bool disable = true;
                    if (!scene.camera().pointing_upward()) {
                        const auto* bed_tag = n.tag_of_type<BedNodeTag>();
                        if (bed_tag != nullptr)
                            disable = bed_tag->type != Scene::BedElementType::PlateDefault &&
                                      bed_tag->type != Scene::BedElementType::PlateTextured &&
                                      bed_tag->type != Scene::BedElementType::Model;
                    }

                    if (disable) {
                        if (n.enabled()) {
                            n.set_enabled(false);
                            m_disabled_nodes.push_back(&n);
                        }
                    }
                    else {
                        ModifiedNode& mod_node = m_bed_nodes.emplace_back();
                        mod_node.node = &n;
                        if (n.has_material_override())
                            mod_node.override_material = *n.material_override();
                        mod_node.shadows = { n.render_component()->cast_shadows(), n.render_component()->receive_shadows() };

                        n.render_component()->set_shadows(Render::Shadows{ false, false });
                        m_material.set_uniform("uniform_color", index_to_color(0));
                        n.set_material_override(m_material);
                    }
                }
            }
        }
    );

    // disable scene background
    scene.set_background_enabled(false);
}

Domain::Image RectangleSelectionPicker::render(Scene::Scene& scene, const Render::Rect& rect)
{
    //
    // render only the part of the scene within the selection rectangle
    //

    auto& gfx_settings = Scene::Scene::graphics_settings();
    auto orig_shading = gfx_settings.shading_type();
    Scene::Scene::set_shading_type(Scene::ShadingType::Legacy);

    const Scene::Camera& camera = scene.camera();
    const Scene::CameraTrackballController& trackball = scene.camera_trackball();
    const Render::Rect& viewport = camera.viewport();
    const Domain::SquareMatrix4d& projection = camera.projection();

    Domain::Vec2d rect_center = { rect.x + 0.5 * rect.width, viewport.height - (rect.y + 0.5 * rect.height) };
    Render::Rect rect_viewport = { 0, 0, rect.width, rect.height };

    Domain::Vec3d pick_translation = {
        (viewport.width - 2.0 * (rect_center.x() - viewport.x)) / rect.width,
        (viewport.height - 2.0 * (rect_center.y() - viewport.y)) / rect.height,
        0.0
    };

    Domain::Vec3d pick_scale = {
        double(viewport.width) / double(rect.width),
        double(viewport.height) / double(rect.height),
        1.0
    };

    Domain::Transform3d pick_trafo = Domain::translation_transform(pick_translation) * Domain::scale_transform(pick_scale);

    // use temporary camera and trackball to render the scene
    // to avoid triggering side-effects updates when modifying camera parameters
    Platform::CameraSynchData data;
    camera.update_synch_data(data);
    trackball.update_synch_data(data);
    Scene::Camera tmp_camera;
    Scene::CameraTrackballController tmp_trackball(tmp_camera);
    Scene::synchronize_camera(data, tmp_camera, tmp_trackball);

    tmp_camera.set_viewport(rect_viewport);
    tmp_camera.set_projection(pick_trafo * projection);

    Render::FramebufferCreationData fb_data;
    fb_data.width = size_t(rect.width);
    fb_data.height = size_t(rect.height);
    fb_data.color_attachments.resize(1);
    Render::Framebuffer* fb = m_device.context().framebuffer_manager().create(fb_data);

    auto cmd_buffer = m_device.create_command_buffer();
    cmd_buffer->bind_framebuffer(*fb);
    cmd_buffer->set_depth_test_enabled(true);
    cmd_buffer->set_cull_face_enabled(true);
    cmd_buffer->set_viewport(rect_viewport);
    cmd_buffer->set_clear_values({ 0.0f, 0.0f, 0.0f, 0.0f });
    cmd_buffer->clear_buffers(true, true);

    Scene::MinimalSceneRenderCustomizer render_customizer;
    scene.render(m_device, *cmd_buffer, &render_customizer, &tmp_camera);

    Scene::Scene::set_shading_type(orig_shading);

    Domain::Image ret = Domain::Image(Domain::PixelFormat::RGBA8, rect.width, rect.height);
    // extract image from framebuffer
    cmd_buffer->read_pixels(*fb, 0, 0, rect.width, rect.height, Domain::PixelFormat::RGBA8, ret.pixels.data());

//#if ENABLE_DEBUG_EXPORT_TO_PNG
//    Biz::Algorithms::ImageUtils::flip_vertical(ret);
//    Biz::Algorithms::ImageUtils::export_to_png_file(ret, "C:/test/images/rectangle_selection");
//#endif // ENABLE_DEBUG_EXPORT_TO_PNG

    cmd_buffer->unbind_framebuffer(*fb);
    m_device.context().framebuffer_manager().destroy(fb);

    return ret;
}

Scene::Node::NodeList RectangleSelectionPicker::collect_contained_nodes(const Domain::Image& image)
{
    DEBUG_ASSERT(image.format() == Domain::PixelFormat::RGBA8);

    //
    // extract all unique indices from the rendered image
    //

    // Parallelized pixel scanning
    tbb::enumerable_thread_specific<std::vector<size_t>> local_ids;
    tbb::parallel_for(0, image.height(), [&](int y) {
        auto& local = local_ids.local();
        local.reserve(image.width());

        for (int x = 0; x < image.width(); ++x) {
            size_t px_offset = 4 * size_t(y * image.width() + x);
            if (image.pixels[px_offset + 3] == 255)
                local.emplace_back(color_to_index(
                    image.pixels[px_offset + 0], 
                    image.pixels[px_offset + 1], 
                    image.pixels[px_offset + 2] 
                ));
        }

        std::sort(local.begin(), local.end());
        local.erase(std::unique(local.begin(), local.end()), local.end());
    });

    size_t total_size = 0;
    for (const auto& vec : local_ids) {
        total_size += vec.size();
    }
    std::vector<size_t> nodes_ids;
    nodes_ids.reserve(total_size);
    for (const auto& vec : local_ids) {
        nodes_ids.insert(nodes_ids.end(), vec.begin(), vec.end());
    }
    std::sort(nodes_ids.begin(), nodes_ids.end());
    nodes_ids.erase(std::unique(nodes_ids.begin(), nodes_ids.end()), nodes_ids.end());

    //
    // map extracted unique indices to scene nodes
    //

    Scene::Node::NodeList ret;
    ret.reserve(nodes_ids.size());
    for (size_t id : nodes_ids) {
        if (0 < id && id - 1 < m_modified_nodes.size())
            ret.emplace_back(m_modified_nodes[id - 1].node);
    }
    return ret;
}

void RectangleSelectionPicker::restore_scene(Scene::Scene& scene)
{
    // 
    // restore the scene to its initial state
    // 

    for (auto& node : m_disabled_nodes) {
        node->set_enabled(true);
    }

    for (auto& node : m_modified_nodes) {
        node.node->render_component()->set_shadows(node.shadows);
        if (node.override_material.has_value())
            node.node->set_material_override(*node.override_material);
        else
            node.node->remove_material_override();
    }

    for (auto& node : m_bed_nodes) {
        node.node->render_component()->set_shadows(node.shadows);
        if (node.override_material.has_value())
            node.node->set_material_override(*node.override_material);
        else
            node.node->remove_material_override();
    }

    scene.set_background_enabled(true);
}

} // namespace Slic3r::App::Plater
