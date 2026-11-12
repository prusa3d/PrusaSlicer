#include "Slic3r/App/Plater/SinkingContours.hpp"
#include "Slic3r/App/Plater/PlaterSceneLayer.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/App/Scene/Node.hpp"
#include "Slic3r/App/Scene/NodeVisitor.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Scene/BedNodeTag.hpp"
#include "Slic3r/App/Scene/MeshRenderNodeComponent.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/Tesselate.hpp"
#include "Slic3r/App/Platform/MouseEvent.hpp"

#include "libslic3r/TriangleMeshSlicer.hpp"

using namespace Slic3r::Biz::Algorithms::BoundingBox;
using namespace Slic3r::Biz::Algorithms::ClipperUtils;
using namespace Slic3r::Biz::Algorithms::Tesselate;

using Slic3r::App::Scene::SceneNodeTag;

namespace Slic3r::App::Plater {

static Domain::ElementRef tag_to_element_ref(const SceneNodeTag& tag)
{
    return { tag.object_id, tag.instance_id, tag.volume_id };
}

static SinkingAuxiliaryElementId auxiliary_element_id_from_tag(const SceneNodeTag& tag)
{
    return { tag.instance_id, tag.volume_id };
}

static Domain::ElementRefs collect_selected_volumes_refs(const Domain::Project& project, const Domain::ElementRefs& elements)
{
    Domain::ElementRefs ret;
    for (const auto& e : elements) {
        const Domain::ModelObject* obj = project.find_object_by_id(e.object_id);
        if (obj != nullptr) {
            if (!e.has_volume()) {
                for (const auto& v : obj->volumes) {
                    if (v->is_model_part()){
                        for (const Domain::ModelInstance* inst : obj->instances) {
                            ret.emplace_back(e.object_id, inst->id().id, v->id().id);
                        }
                    }
                }
            }
            else {
                const Domain::ModelVolume* vol = project.find_volume_by_id(e.object_id, e.volume_id);
                if (vol != nullptr && vol->is_model_part()) {
                    for (const Domain::ModelInstance* inst : obj->instances) {
                        ret.emplace_back(e.object_id, inst->id().id, e.volume_id);
                    }
                }
            }
        }
    }
    std::sort(ret.begin(), ret.end(), [](const Domain::ElementRef& a, const Domain::ElementRef& b) { return a < b; });
    ret.erase(std::unique(ret.begin(), ret.end(), [](const Domain::ElementRef& a, const Domain::ElementRef& b) { return a == b; }), ret.end());
    return ret;
}

static Scene::Node::NodeList collect_selected_nodes(Scene::Scene& scene, const Domain::ElementRefs& volumes)
{
    Scene::Node::NodeList ret;
    scene.root().query([&](const Scene::Node* n) {
        const SceneNodeTag* tag = n->tag_of_type<SceneNodeTag>();
        if (tag != nullptr) {
            if (std::find(volumes.begin(), volumes.end(), tag_to_element_ref(*tag)) != volumes.end())
                return true;
        }
        return false;
    }, ret, true);
    return ret;
}

static Domain::Transformation volume_world_transformation(const Domain::Project& project, const Domain::ElementRef& volume)
{
    const Domain::ModelInstance* inst = project.find_instance_by_id(volume.object_id, volume.instance_id);
    const Domain::ModelVolume* vol = project.find_volume_by_id(volume.object_id, volume.volume_id);
    return ASSERT_VAL(inst)->get_transformation() * ASSERT_VAL(vol)->get_transformation();
}

static Domain::Transform3d volume_world_matrix(const Domain::Project& project, const Domain::ElementRef& volume)
{
    return volume_world_transformation(project, volume).get_matrix();
}

static Domain::ElementRefs detect_sinking_volumes(const Domain::Project& project, const Domain::ElementRefs& volumes)
{
    Domain::ElementRefs ret;
    for (const auto& v : volumes) {
        const Domain::ModelVolume* vol = project.find_volume_by_id(v.object_id, v.volume_id);
        Domain::BoundingBox3d box = transformed(vol->mesh().bounding_box(), volume_world_matrix(project, v));
        if (box.min.z() < Domain::SINKING_Z_THRESHOLD && box.max.z() >= Domain::SINKING_Z_THRESHOLD)
            ret.emplace_back(v);
    }
    return ret;
}

static Scene::Node::NodeList remove_no_more_sinking(Scene::Scene& scene, const Scene::Node::NodeList& nodes,
    const Domain::ElementRefs& sinking_volumes, SinkingContours::ModelGeometryManager& model_geometry_manager)
{
    Scene::Node::NodeList ret;

    for (Scene::Node* n : nodes) {
        const SceneNodeTag* tag = n->tag_of_type<SceneNodeTag>();
        bool found = false;

        if (std::find(sinking_volumes.begin(), sinking_volumes.end(), tag_to_element_ref(*tag)) == sinking_volumes.end()) {
            Scene::Node* child = n->query_first([](const Scene::Node* c) {
                const SinkingSceneNodeTag* tag = c->tag_of_type<SinkingSceneNodeTag>();
                return tag != nullptr;
            }, true);
            if (child != nullptr) {
                scene.remove_child(child);
                model_geometry_manager.release(auxiliary_element_id_from_tag(*tag));
                found = true;
            }
        }

        if (!found)
            ret.push_back(n);
    }

    return ret;
}

static std::vector<Domain::Vec3f> generate_triangles(const Domain::Project& project, Scene::Node& node)
{
    static constexpr float HALF_WIDTH = float(scale_(0.3f));

    const SceneNodeTag* tag = node.tag_of_type<SceneNodeTag>();
    MeshSlicingParams slicing_params;
    slicing_params.trafo = volume_world_matrix(project, tag_to_element_ref(*tag));
    const Domain::ModelVolume* vol = project.find_volume_by_id(tag->object_id, tag->volume_id);
    Domain::Polygons polygons = union_(slice_mesh(vol->mesh().its, 0.0f, slicing_params));
    std::vector<Domain::Vec3f> ret;
    if (!polygons.empty()) {
        for (const Domain::ExPolygon& expoly : diff_ex(expand(polygons, HALF_WIDTH), shrink(polygons, HALF_WIDTH))) {
            std::vector<Domain::Vec3d> triangulation = triangulate_expolygon_3d(expoly);
            std::transform(triangulation.begin(), triangulation.end(), std::back_inserter(ret),
                [](const Domain::Vec3d& v) { return v.cast<float>(); });
        }
    }

    // duplicates triangles to render both sides
    ret.reserve(2 * ret.size());
    size_t original_size = ret.size();
    for (size_t i = 0; i < original_size; i += 3) {
        ret.emplace_back(ret[i + 0]);
        ret.emplace_back(ret[i + 2]);
        ret.emplace_back(ret[i + 1]);
    }

    Domain::Transform3f xtrafo = slicing_params.trafo.inverse().cast<float>();
    std::for_each(ret.begin(), ret.end(), [&](Domain::Vec3f& v) { v = xtrafo * v; });

    return ret;
}

static Scene::Node::NodeList update_already_sinking(Render::Device& device, const Domain::Project& project, Scene::Scene& scene,
    const Scene::Node::NodeList& nodes, const Render::Material& material, SinkingContours::ModelGeometryManager& model_geometry_manager,
    std::vector<SinkingAuxiliaryElementId>& processed_volumes)
{
    Scene::Node::NodeList ret;

    for (Scene::Node* n : nodes) {
        Scene::Node* child = n->query_first([](const Scene::Node* c) {
            const SinkingSceneNodeTag* tag = c->tag_of_type<SinkingSceneNodeTag>();
            return tag != nullptr;
        }, true);
        if (child != nullptr) {
            const SceneNodeTag* tag = n->tag_of_type<SceneNodeTag>();
            SinkingAuxiliaryElementId id = auxiliary_element_id_from_tag(*tag);

            Render::Geometry* geom = nullptr;
            auto it = std::find_if(processed_volumes.begin(), processed_volumes.end(),
                [&](const SinkingAuxiliaryElementId& item) { return item.volume_id == tag->volume_id; });
            if (it == processed_volumes.end()) {
                std::vector<Domain::Vec3f> triangles = generate_triangles(project, *n);
                if (!triangles.empty()) {
                    model_geometry_manager.set(id, Render::geometry_from_triangles(device, triangles, material));
                    geom = model_geometry_manager.get(id);
                    processed_volumes.push_back(id);
                }
            }
            else
                geom = model_geometry_manager.get(*it);

            if (geom != nullptr)
                dynamic_cast<Scene::MeshRenderNodeComponent*>(child->render_component())->set_geometry(geom);
            else {
                model_geometry_manager.release(id);
                scene.remove_child(child);
            }
        }
        else
            ret.push_back(n);
    }

    return ret;
}

static void add_newly_sinking(Render::Device& device, const Domain::Project& project, Scene::Scene& scene, const Scene::Node::NodeList& nodes,
    const Render::Material& material, SinkingContours::ModelGeometryManager& model_geometry_manager, std::vector<SinkingAuxiliaryElementId>& processed_volumes)
{
    for (Scene::Node* n : nodes) {
        const SceneNodeTag* tag = n->tag_of_type<SceneNodeTag>();
        SinkingAuxiliaryElementId id = auxiliary_element_id_from_tag(*tag);

        Render::Geometry* geom = nullptr;
        auto it = std::find_if(processed_volumes.begin(), processed_volumes.end(),
            [&](const SinkingAuxiliaryElementId& item) { return item.volume_id == tag->volume_id; });
        if (it == processed_volumes.end()) {
            std::vector<Domain::Vec3f> triangles = generate_triangles(project, *n);
            if (!triangles.empty()) {
                geom = model_geometry_manager.get_or_create(id,
                    [&]() { return Render::geometry_from_triangles(device, triangles); });
                processed_volumes.push_back(id);
            }
        }
        else
            geom = model_geometry_manager.get(*it);

        if (geom != nullptr) {
            std::string debug_name = fmt::format("sinking_contour_{}_{}", tag->instance_id, tag->volume_id);
            Scene::NodeBuilder builder{ scene };
            builder
                .set_debug_name(debug_name)
                .set_tag(SinkingSceneNodeTag{ tag->object_id, tag->instance_id, tag->volume_id })
                .set_mesh(geom, material, Scene::RenderLayerId(PlaterSceneLayer::ObjectAccessoriesRegular))
                .set_shadows({ false, false });

            Scene::Node* child = builder.build().release();
            // set also the override material to avoid the material being replaced by the parent's one
            child->set_material_override(material);
            scene.add_child(child, n);
        }
    }
}

static void remove_deleted(Scene::Scene& scene, const Domain::ElementRefs& elements, SinkingContours::ModelGeometryManager& model_geometry_manager)
{
    for (const auto& e : elements) {
        model_geometry_manager.release_if([&e](const SinkingAuxiliaryElementId& id, const Render::Geometry& geom) {
            return e.has_volume() ? id.volume_id == e.volume_id : id.instance_id == e.instance_id;
        });
    };
}

void SinkingContours::update_scene(Render::Device& device, const Domain::Project& project, Scene::Scene& scene, const Domain::ElementRefs& elements)
{
    Domain::ElementRefs selected_volumes_refs = collect_selected_volumes_refs(project, elements);
    if (selected_volumes_refs.empty()) {
        // elements are being deleted
        remove_deleted(scene, elements, m_model_geometry_manager);
        return;
    }

    Render::Material material = Render::Material{}
        .set_shader(device.context().shader_manager().shader("flat"))
        .set_uniform("uniform_color", Domain::ColorRGBA::WHITE());

    Scene::Node::NodeList nodes = collect_selected_nodes(scene, selected_volumes_refs);
    DEBUG_ASSERT(nodes.empty() || selected_volumes_refs.size() == nodes.size());

    Domain::ElementRefs sinking_volumes = detect_sinking_volumes(project, selected_volumes_refs);
    nodes = remove_no_more_sinking(scene, nodes, sinking_volumes, m_model_geometry_manager);
    if (sinking_volumes.empty())
        return;

    std::vector<SinkingAuxiliaryElementId> processed_volumes;
    nodes = update_already_sinking(device, project, scene, nodes, material, m_model_geometry_manager, processed_volumes);
    add_newly_sinking(device, project, scene, nodes, material, m_model_geometry_manager, processed_volumes);
}

void SinkingContours::update_visibility(const Platform::MouseEvent& e, const Render::ScreenInfo& screen_info, const Domain::Project& project,
    Scene::Scene& scene)
{
    if (is_empty())
        return;

    if (e.type() == Platform::MouseEvent::Type::Move) {

        Scene::Node::NodeList nodes;
        scene.root().query([&](const Scene::Node* n) {
            return n->tag_of_type<SinkingSceneNodeTag>() != nullptr;
        }, nodes, true);

        for (Scene::Node* n : nodes) {
            dynamic_cast<Scene::MeshRenderNodeComponent*>(n->render_component())->set_layer_index(Scene::RenderLayerId(PlaterSceneLayer::ObjectAccessoriesRegular));
        }

        Scene::Node::NodeList highlight_nodes;

        if (m_selection.empty()) {
            /*          */
            /* hovering */
            /*          */
            Scene::NodePickResults pick_results;
            Scene::Ray pick_ray;
            scene.pick_at(
                screen_info.mouse_to_screen(e.x()),
                screen_info.mouse_to_screen(e.y()),
                pick_results, &pick_ray
            );

            // filters out hits on volumes below the bed if the camera is pointing downward
            if (!scene.camera().pointing_upward()) {
                auto it = std::find_if(pick_results.begin(), pick_results.end(),
                    [](const Scene::NodePickResult& r) {
                        return r.node->tag_of_type<Scene::BedNodeTag>() != nullptr;
                    }
                );
                if (it != pick_results.end()) {
                    pick_results.erase(std::remove_if(it, pick_results.end(),
                        [](const Scene::NodePickResult& r) { return r.node->tag_of_type<SceneNodeTag>() != nullptr; }),
                        pick_results.end());
                }
            }

            if (m_highlight_enabled) {
                for (auto& [n, t] : pick_results) {
                    const SceneNodeTag* tag = n->tag_of_type<SceneNodeTag>();
                    if (tag != nullptr) {
                        Scene::Node* child = n->query_first([](const Scene::Node* c) {
                            const SinkingSceneNodeTag* tag = c->tag_of_type<SinkingSceneNodeTag>();
                            return tag != nullptr;
                        }, true);
                        if (child != nullptr)
                            highlight_nodes.push_back(child);
                        break;
                    }
                }
            }
        }
        else {
            /*              */
            /* transforming */
            /*              */
            if (m_highlight_enabled) {
                Domain::ElementRefs selected_volumes_refs = collect_selected_volumes_refs(project, m_selection);
                Scene::Node::NodeList selected_nodes = collect_selected_nodes(scene, selected_volumes_refs);
                DEBUG_ASSERT(selected_volumes_refs.size() == selected_nodes.size());

                for (Scene::Node* n : selected_nodes) {
                    Scene::Node* child = n->query_first([](const Scene::Node* c) {
                        const SinkingSceneNodeTag* tag = c->tag_of_type<SinkingSceneNodeTag>();
                        return tag != nullptr;
                    }, true);
                    if (child != nullptr)
                        highlight_nodes.push_back(child);
                }
            }

            m_selection.clear();
        }

        for (auto n : highlight_nodes) {
            dynamic_cast<Scene::MeshRenderNodeComponent*>(n->render_component())->set_layer_index(Scene::RenderLayerId(PlaterSceneLayer::ObjectAccessoriesOnTop));
        }
    }
}

} // namespace Slic3r::App::Plater
