#include "Slic3r/App/libvgcode/SlaViewer.hpp"

#include <Slic3r/Biz/libpgcode/Utils.hpp>
#include <Slic3r/Biz/Algorithms/Tesselate.hpp>
#include <Slic3r/App/Render/GL/commonGL.hpp>
#include <Slic3r/App/Render/Device.hpp>
#include <Slic3r/App/Render/Context.hpp>
#include <Slic3r/App/Render/TextureManager.hpp>
#include <Slic3r/App/Render/TextureBufferManager.hpp>
#include <Slic3r/App/Render/Material.hpp>
#include <Slic3r/App/Render/GeometryBuilder.hpp>
#include <Slic3r/App/Scene/NodeBuilder.hpp>
#include <Slic3r/App/Scene/Scene.hpp>
#include <Slic3r/App/Scene/NodeVisitor.hpp>
#include "Slic3r/App/Scene/InstancedMeshRenderNodeComponent.hpp"
#include "Slic3r/App/Scene/MeshRenderNodeComponent.hpp"
#include "Slic3r/App/Scene/ScenePresenterProjectContext.hpp"

#include "Slic3r/Domain/ObjectID.hpp"
#include "libslic3r/SLAResult.hpp"
#include "Slic3r/LegacyFormat.hpp"
#include "Slic3r/App/libvgcode/GCodeNodeTag.hpp"

#include "Slic3r/Log.hpp"

#include <map>
#include <assert.h>
#include <stdexcept>
#include <cstdio>
#include <string>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <memory>

using namespace Slic3r::Biz::libpgcode;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::ExPolygons;
using Slic3r::Domain::EPSILON;
using Slic3r::Domain::Vec2f;

namespace Slic3r::App::libvgcode {

SlaViewer::SlaViewer() :
    m_model_geometry_manager{"sla_viewer_geometry"},
    m_model_triangle_mesh_manager{"sla_viewer_mesh"}
{}

void SlaViewer::init(Render::Device& device, Scene::Scene& scene, Scene::GeometryDataFactory& data_factory)
{
    AbstractViewer::init(device, scene, data_factory);

    /* Create a tree
     * -> "main_sla"             {SlaObjectNodeTag{ object_id = 0, instance_id = 0, SlaMeshType::Undefined }}
     *       -> "top_clip_mesh"      {SlaObjectNodeTag{ object_id = 0, instance_id=0, SlaMeshType::TopClip }}
     *       -> "bottom_clip_mesh"   {SlaObjectNodeTag{ object_id = 0, instance_id=0, SlaMeshType::BottomClip }}
     *       -> "sla_object_node"    {SlaObjectNodeTag{ object_id, instance_id = 0, SlaMeshType::Undefined }}
     *           -> "sla_instanceN_node" {SlaObjectNodeTag{ object_id, instance_id, SlaMeshType::Undefined }}
     *               -> "object_mesh"        {SlaObjectNodeTag{ object_id, instance_id, SlaMeshType::Object }}
     *               -> "supports_mesh"      {SlaObjectNodeTag{ object_id, instance_id, SlaMeshType::Supports }}
     *               -> "pad_mesh"           {SlaObjectNodeTag{ object_id, instance_id, SlaMeshType::Pad }}
     * -> "segment node"
     */
}

void SlaViewer::set_scene(Scene::Scene& scene)
{
    AbstractViewer::set_scene(scene);

    Scene::Node* node = m_scene->root().query_first([](const Scene::Node* n) -> bool {
        const SlaObjectNodeTag* tag = n->tag_of_type<SlaObjectNodeTag>();
        return tag != nullptr && tag->type == SlaMeshType::Undefined;
    }, true);

    if (node != nullptr) {
        m_main_node = node;
        return;
    }

    Scene::NodeBuilder builder{ *m_scene };
    builder.set_debug_name("sla_main");
    builder.set_tag(SlaObjectNodeTag());

    builder.child([&](Scene::NodeBuilder& bldr) {
        build_clipping_plane_node(SlaMeshType::TopClip, bldr);
    });

    builder.child([&](Scene::NodeBuilder& bldr) {
        build_clipping_plane_node(SlaMeshType::BottomClip, bldr);
    });

    m_scene->add_child(builder.build().release(), &m_scene->root());
    m_main_node = m_scene->root().children().back().get();
}

void SlaViewer::clear_scene()
{
    if (m_scene != nullptr && m_main_node != nullptr) {
        m_scene->remove_children([&](const Scene::Node* node) { return true; }, m_main_node);
        m_scene->remove_child(m_main_node);
        m_main_node = nullptr;
    }
}

void SlaViewer::reset()
{
    reset_layers();

    // Remove all object Scene::Nodes
    std::vector<Scene::AuxiliaryElementId> geometry_ids;
    m_scene->remove_children([&](const Scene::Node* node) {
        const SlaObjectNodeTag* t = node->tag_of_type<SlaObjectNodeTag>();
        bool ret                  = t && !t->is_clip();
        if (ret) {
            Scene::AuxiliaryElementId id;
            id.id = t->object_id;
            switch (t->type) {
            case SlaMeshType::Object: {
                id.type = Scene::AuxiliaryElementId::Type::SlaMesh;
                break;
            }
            case SlaMeshType::Pad: {
                id.type = Scene::AuxiliaryElementId::Type::SlaPad;
                break;
            }
            case SlaMeshType::Supports: {
                id.type = Scene::AuxiliaryElementId::Type::SlaSupports;
                break;
            }
            case SlaMeshType::Undefined:
            case SlaMeshType::TopClip:
            case SlaMeshType::BottomClip:
                // Clip types are excluded above by !t->is_clip(); Undefined is not expected here.
                break;
            }

            if (node->has_render_component())
                geometry_ids.push_back(id);
        }
        return ret;
    }, m_main_node);

    for (const Scene::AuxiliaryElementId& id : geometry_ids) {
        m_model_geometry_manager.release(id);
        m_model_triangle_mesh_manager.release(id);
    }
}

void SlaViewer::reset_layers()
{
    AbstractViewer::reset();

    // reset clipping planes
    if (m_main_node == nullptr)
       return;

    // Make default mesh as small as possible.
    // Its geometry will be updated on layers move
    indexed_triangle_set mesh_its = Biz::Algorithms::TriangleMesh::its_make_cube(0.1f, 0.1f, 0.1f);
    update_clipping_plane(SlaMeshType::TopClip, mesh_its);
    update_clipping_plane(SlaMeshType::BottomClip, mesh_its);

    m_result = nullptr;
}

void SlaViewer::reset_object(const Domain::ObjectID object_id)
{
    // remove object from the  Scene::Nodes 
    m_scene->remove_children([object_id](const Scene::Node* node) { 
        const SlaObjectNodeTag* t = node->tag_of_type<SlaObjectNodeTag>();
        return t && t->object_id == object_id.id;
    }, m_main_node);

    for (const Scene::AuxiliaryElementId::Type& aei_type : { Scene::AuxiliaryElementId::Type::SlaMesh,
        Scene::AuxiliaryElementId::Type::SlaSupports,
        Scene::AuxiliaryElementId::Type::SlaPad }) {

        Scene::AuxiliaryElementId id{ aei_type, object_id.id };

        m_model_geometry_manager.release(id);
        m_model_triangle_mesh_manager.release(id);
    }
}

void SlaViewer::load(const Biz::Slicing::SLAResult& result, const Scene::Transform& bed_transform)
{
    m_bed_instance_transform = bed_transform;
    if (result.export_data->print_statistics.has_value()) {
        load_layers(result.heights, result.export_data->print_statistics.value().layers_times_running_total);
        m_result = &result;
    }
}

void SlaViewer::load_layers(const std::vector<float>& layers_zs, const std::vector<double>& layers_times)
{
    if (layers_zs.empty() || layers_times.empty())
        return;

    AbstractViewer::reset();

    assert(layers_zs.size() == layers_times.size());

    for (size_t i = 0; i < layers_zs.size(); ++i) {
        m_layers.update_as_sla(layers_zs[i], layers_times[i]);
    }
}

static const std::unordered_map<SlaMeshType, ColorRGBA> SLA_MESH_COLORS = {
    {SlaMeshType::Object, {1, 0.5f, 0, 1}},
    {SlaMeshType::Supports, {0.5f, 0.5f, 0.5f, 1}},
    {SlaMeshType::Pad, {0.6f, 0.2f, 1.0f, 1}},
};

void SlaViewer::build_sla_object_mesh(
    size_t object_id,
    size_t instance_id,
    SlaMeshType type, 
    std::shared_ptr<const Slic3r::Domain::TriangleMesh> mesh,
    const Transform3d& trafo,
    Scene::NodeBuilder& builder)
{
    const std::string type_str = type == SlaMeshType::Object   ? "object" :
                                 type == SlaMeshType::Supports ? "support" :
                                 type == SlaMeshType::Pad      ? "pad" : "UNDEF";
    SPDLOG_DEBUG("build_volume obj:{} inst:{} type:{}", object_id, instance_id, type_str);

    auto& geom_mgr = m_model_geometry_manager;
    auto& trimesh_mgr = m_model_triangle_mesh_manager;

    Scene::AuxiliaryElementId::Type aei_type =  type == SlaMeshType::Object   ? Scene::AuxiliaryElementId::Type::SlaMesh :
                                                type == SlaMeshType::Supports ? Scene::AuxiliaryElementId::Type::SlaSupports :
                                                type == SlaMeshType::Pad      ? Scene::AuxiliaryElementId::Type::SlaPad : 
                                                                                Scene::AuxiliaryElementId::Type::Volume;

    Scene::AuxiliaryElementId id{ aei_type, object_id};
    const auto& trimesh =
        trimesh_mgr.get_or_create(id, [&]() {
        return std::make_unique<Scene::TriangleMesh>(mesh);
            });
    const auto* geom = geom_mgr.get_or_create(id, [&]() {
        return Render::geometry_from_triangle_mesh(*m_device, trimesh->triangles());
        });
    ColorRGBA color = ColorRGBA{ 1.0f, 1.0f, 1.0f, 1.0f };

    auto color_it = SLA_MESH_COLORS.find(type);
    if (color_it != SLA_MESH_COLORS.end())
        color = color_it->second;

    auto material = Render::Material{}
        .set_shader(m_device->context().shader_manager().shader("gouraud_light"))
        .set_uniform("uniform_color", color)
        .set_transparent(color.is_transparent());

    Scene::PrintVolumeData print_volume;
    print_volume.type = Domain::BedType::Invalid;
    set_uniforms(print_volume, material);

    Scene::PBRParams pbr_params;
    switch (type)
    {
    default:
    case SlaMeshType::Object:
    {
        pbr_params = Scene::DEFAULT_VOLUME_PBRPARAMS;
        break;
    }
    case SlaMeshType::Supports:
    {
        pbr_params = Scene::DEFAULT_SLA_SUPPORTS_PBRPARAMS;
        break;
    }
    case SlaMeshType::Pad:
    {
        pbr_params = Scene::DEFAULT_SLA_PAD_PBRPARAMS;
        break;
    }
    }

    builder
        .set_debug_name(Slic3r::format("sla_obj: %1%, inst: %2%, %3%", object_id, instance_id, type_str))
        .set_tag(SlaObjectNodeTag{ object_id, instance_id, type })
        .set_mesh(geom, material, 0)
        .transform([trafo](auto& xform) { xform = trafo; })
        .set_aabb(trimesh->aabb_mesh())
        .set_shadows(Render::Shadows{ true, true })
        .set_pbr(pbr_params);
}

void SlaViewer::build_clipping_plane_node(SlaMeshType plane_type, Scene::NodeBuilder& builder)
{
    ASSERT(plane_type == SlaMeshType::TopClip || plane_type == SlaMeshType::BottomClip);

    const std::string type_str = plane_type == SlaMeshType::TopClip ? "Clip top" : "Clip bottom";
    SPDLOG_DEBUG("build_volume type:{}", type_str);

    auto& geom_mgr = m_model_geometry_manager;
    auto& trimesh_mgr = m_model_triangle_mesh_manager;

    // Make default mesh as small as possible.
    // Its geometry will be updated on layers move
    indexed_triangle_set mesh_its = Biz::Algorithms::TriangleMesh::its_make_cube(0.1f, 0.1f, 0.1f);

    Scene::AuxiliaryElementId::Type aei_type = plane_type == SlaMeshType::TopClip ? 
        Scene::AuxiliaryElementId::Type::SlaTopClip : 
        Scene::AuxiliaryElementId::Type::SlaBottomClip;

    Scene::AuxiliaryElementId id{ aei_type, 0 };

    const auto& trimesh =
        trimesh_mgr.get_or_create(id, [&]() {
        return std::make_unique<Scene::TriangleMesh>(std::move(mesh_its));
            });
    const auto* geom = geom_mgr.get_or_create(id, [&]() {
        return Render::geometry_from_triangle_mesh(*m_device, trimesh->triangles());
        });

    ColorRGBA color = ColorRGBA{ 1.0f, 0.0f, 0.37f, 1.0f };
    auto material = Render::Material{}
        .set_shader(m_device->context().shader_manager().shader("gouraud_light"))
        .set_uniform("uniform_color", color)
        .set_transparent(color.is_transparent());

    Scene::PrintVolumeData print_volume;
    print_volume.type = Domain::BedType::Invalid;
    set_uniforms(print_volume, material);

    builder.set_debug_name(Slic3r::format("sla_obj: clipping plane: %1%", type_str))
        .set_tag(SlaObjectNodeTag{0, 0, plane_type})
        .set_mesh(geom, material, 0)
        .set_shadows(Render::Shadows{true, true});
}

void SlaViewer::build_if_needed(
    size_t object_id,
    size_t instance_id,
    SlaMeshType type,
    std::shared_ptr<const Slic3r::Domain::TriangleMesh> mesh,
    const Transform3d& trafo, 
    Scene::Node* parent_node)
{
    if (!mesh || mesh->empty()) {
        return;
    }

    for (auto& node : parent_node->children()) {
        const SlaObjectNodeTag* t = node->tag_of_type<SlaObjectNodeTag>();
        if (t->type == type) {
            // node for this mesh type already exists
            return;
        }
    }

    Scene::NodeBuilder builder(*m_scene);

    build_sla_object_mesh(object_id, instance_id, type, mesh, trafo, builder);

    m_scene->add_child(builder.build().release(), parent_node);
}

void SlaViewer::build_instance_node(
    const Biz::Slicing::Sla::Object& sla_object,
    size_t instance_id,
    const Transform3d& trafo,
    Scene::Node* parent_node)
{
    // add Scene::Nodes for each mesh from object
    Scene::NodeBuilder builder(*m_scene);

    size_t object_id = sla_object.object_id.id;

    build_if_needed(object_id, instance_id, SlaMeshType::Object,   sla_object.mesh,              trafo, parent_node);
    build_if_needed(object_id, instance_id, SlaMeshType::Supports, sla_object.support_structure, trafo, parent_node);
    build_if_needed(object_id, instance_id, SlaMeshType::Pad,      sla_object.pad,               trafo, parent_node);

    for (const auto& node : parent_node->children()) {
        const SlaObjectNodeTag* t = node->tag_of_type<SlaObjectNodeTag>();
        if (t != nullptr && t->instance_id == instance_id) {
            node->set_local_transform(trafo);
        }
    }
}

void SlaViewer::load_object(const Biz::Slicing::Sla::Object& sla_object, const Scene::Transform& bed_transform)
{
    size_t object_id = sla_object.object_id.id;

    m_bed_instance_transform = bed_transform;

    Scene::Node* object_node{ nullptr };
    for (auto& node : m_main_node->children()) {
        const SlaObjectNodeTag* t = node->tag_of_type<SlaObjectNodeTag>();
        if (t != nullptr && t->object_id == object_id) {
            // "sla_object" node already exists 
            ASSERT(t->type == SlaMeshType::Undefined);
            object_node = node.get();
            break;
        }
    }

    if (!object_node) {
        // Create object node if it wasn't found
        Scene::NodeBuilder builder{ *m_scene };
        builder.set_debug_name(Slic3r::format("sla_object: %1%", object_id));
        builder.set_tag(SlaObjectNodeTag({ object_id }));
        m_scene->add_child(builder.build().release(), m_main_node);
        object_node = m_main_node->children().back().get();
    }
    ASSERT(object_node);

    // add instances nodes
    for (const auto& [id, trafo] : sla_object.instance_trafos) {
        size_t instance_id = id.id;
        Scene::Node* inst_node{ nullptr };
        for (auto& node : object_node->children()) {
            const SlaObjectNodeTag* t = node->tag_of_type<SlaObjectNodeTag>();
            ASSERT(t->object_id == object_id && t->type == SlaMeshType::Undefined);
            if (t != nullptr && t->instance_id == instance_id) {
                // "instance" node already exists
                inst_node = node.get();
                break;
            }
        }

        if (!inst_node) {
            // Create instance node if it wasn't found
            Scene::NodeBuilder builder{ *m_scene };
            builder.set_debug_name(Slic3r::format("sla_object: %1%, instance %2%", object_id, instance_id));
            builder.set_tag(SlaObjectNodeTag({ object_id, instance_id }));
            m_scene->add_child(builder.build().release(), object_node);
            inst_node = object_node->children().back().get();
        }
        ASSERT(inst_node);

        // add mesh node
        build_instance_node(sla_object, instance_id, m_bed_instance_transform * trafo, inst_node);
    }
}

void SlaViewer::render()
{
}

void SlaViewer::update_clipping_plane(SlaMeshType plane_type, indexed_triangle_set& its)
{
    if (m_main_node == nullptr)
        return;

    Scene::visit(*m_main_node, [&](Scene::Node& n) {
        SlaObjectNodeTag* tag = n.tag_of_type<SlaObjectNodeTag>();
        if (tag != nullptr && tag->type == plane_type) {
            Scene::AuxiliaryElementId::Type aei_type = plane_type == SlaMeshType::TopClip ?
                Scene::AuxiliaryElementId::Type::SlaTopClip :
                Scene::AuxiliaryElementId::Type::SlaBottomClip;

            Scene::AuxiliaryElementId id{ aei_type, 0 };

            m_model_triangle_mesh_manager.release(id);
            m_model_geometry_manager.release(id);

            const auto& trimesh =
                m_model_triangle_mesh_manager.get_or_create(id, [&]() {
                return std::make_unique<Scene::TriangleMesh>(std::move(its));
                    });
            const auto* geom = m_model_geometry_manager.get_or_create(id, [&]() {
                return Render::geometry_from_triangle_mesh(*m_device, trimesh->triangles());
                });

            static_cast<Scene::MeshRenderNodeComponent*>(n.render_component())->set_geometry(geom);
            n.set_local_transform(m_bed_instance_transform);
        }
    });
}

static indexed_triangle_set create_clipping_plane_its(const ExPolygons& polygons, float z, bool flip)
{
    using Slic3r::Biz::Algorithms::Tesselate::triangulate_expolygons_3d;
    auto triangles = triangulate_expolygons_3d(polygons, z, flip);

    indexed_triangle_set its;
    its.vertices.reserve(triangles.size());
    its.indices.reserve(triangles.size());

    for (size_t i = 0; i < triangles.size(); i += 3) {
        its.vertices.emplace_back(triangles[i].cast<float>());
        its.vertices.emplace_back(triangles[i + 1].cast<float>());
        its.vertices.emplace_back(triangles[i + 2].cast<float>());

        its.indices.emplace_back(Domain::Index3{
            static_cast<int>(i),
            static_cast<int>(i + 1),
            static_cast<int>(i + 2)
            });
    }
    return its;
}

void SlaViewer::update_preview_range(size_t min_layer_id, size_t max_layer_id)
{
    if (m_layers.empty() || !m_result) {
        reset_layers();
        return;
    }

    float min_layer_z = min_layer_id == 0 ? 0.f :m_result->heights[min_layer_id - 1];
    float max_layer_z = m_result->heights[max_layer_id];

    // Add a small Z shifts to prevent top/bottom face flickering.
    min_layer_z -= float(EPSILON);
    max_layer_z += float(EPSILON);

    // update Z clipping

    Vec2f z_range = { min_layer_z, max_layer_z };

    Scene::visit(*m_main_node, [&](Scene::Node& n) {
        SlaObjectNodeTag* tag = n.tag_of_type<SlaObjectNodeTag>();
        if (tag != nullptr) {
            if (tag->type != SlaMeshType::Undefined && !tag->is_clip()) {
                ColorRGBA color = SLA_MESH_COLORS.find(tag->type)->second;

                auto material = Render::Material{}
                    .set_shader(m_device->context().shader_manager().shader("gouraud_light_double_z_clip"))
                    .set_uniform("uniform_color", color)
                    .set_uniform("z_range", z_range)
                    .set_transparent(color.is_transparent());
                n.render_component()->replace_material(material);
            }
        }
    });

    // update clipping planes

    for (const SlaMeshType& type : {SlaMeshType::TopClip, SlaMeshType::BottomClip})
    {
        const size_t layer_id = type == SlaMeshType::TopClip ? max_layer_id : min_layer_id == 0 ? 0 : min_layer_id-1;
        const float mesh_z = type == SlaMeshType::TopClip ? max_layer_z : min_layer_z;

        indexed_triangle_set its = create_clipping_plane_its(m_result->slices[layer_id], mesh_z, type == SlaMeshType::BottomClip);

        update_clipping_plane(type, its);
    }
}

void SlaViewer::set_layers_range(Interval::value_type min, Interval::value_type max)
{
    AbstractViewer::set_layers_range(min, max);
    update_preview_range(min, max);
}

void SlaViewer::set_view_visible_range(Interval::value_type min, Interval::value_type max)
{
    AbstractViewer::set_view_visible_range(min, max);
    update_preview_range(min, max);
}

void SlaViewer::update_view_full_range()
{

}

float SlaViewer::estimated_time() const
{
    return 0.f;
}

float SlaViewer::estimated_time_at(size_t id) const
{
    return 0.f;
}

std::vector<float> SlaViewer::layers_estimated_times() const
{
    return m_layers.times(Biz::libpgcode::TimeMode::Normal);
}

} // namespace Slic3r::App::libvgcode
