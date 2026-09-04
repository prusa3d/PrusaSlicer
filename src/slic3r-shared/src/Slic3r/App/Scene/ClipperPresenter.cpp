#include "Slic3r/App/Scene/ClipperPresenter.hpp"

#include "Slic3r/App/Scene/Clipper.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Scene/Node.hpp"
#include <Slic3r/App/Scene/NodeBuilder.hpp>
#include <Slic3r/App/Scene/NodeVisitor.hpp>
#include "Slic3r/App/Scene/MeshRenderNodeComponent.hpp"

#include "Slic3r/Domain/ModelVolume.hpp"

#include "Slic3r/App/Render/Device.hpp"
#include <Slic3r/App/Render/GeometryBuilder.hpp>

#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include <fmt/format.h>

using Slic3r::Domain::ColorRGBA;

namespace Slic3r::App::Scene {

ClipperPresenter::ClipperPresenter(Clipper* clipper,
                                   Render::Device* device,
                                   ISceneProvider* scene_provider) :
    m_clipper(clipper),
    m_device(device),
    m_scene_provider{scene_provider}
{}

static void set_enabled_scene_nodes(
    Scene* scene,
    bool enabled_scene_nodes,
    Node* main_presenter_node,
    bool force_enabled_scene_nodes = true
)
{
    if (force_enabled_scene_nodes) {
        visit(
            scene->root(),
            [&](Node& node)
            {
                SceneNodeTag* tag = node.tag_of_type<SceneNodeTag>();
                if (tag != nullptr) {
                    node.set_enabled(enabled_scene_nodes);
                }
            },
            true
        );
    }
    if (main_presenter_node) {
        main_presenter_node->set_enabled(!enabled_scene_nodes);
    }
}

static Node* get_main_clipper_node(Node* parent_node)
{
    for (const auto& node : parent_node->children()) {
        const ClipperElement* tag = node->tag_of_type<ClipperElement>();
        if (tag && tag->type == ClipperElementType::Undef) {
            return node.get();
        }
    };
    return nullptr;
}

void ClipperPresenter::activate(
    const Domain::ModelObject* selected_object,
    const Domain::ModelInstance* selected_instance,
    Node* parent_node,
    double sla_shift,
    BuildMeshesNodes should_build_meshes_nodes
)
{
    m_main_node = get_main_clipper_node(parent_node);
    if (m_main_node && m_main_node->children().size() > 0) {
        deactivate();
    }

    if (m_clipper) {
        m_clipper->set_camera(&m_scene_provider->scene().camera());
        m_clipper->update(selected_object, selected_instance, sla_shift, true);
    }

    if (!m_main_node) {
        init_main_node(parent_node);
    }

    if (should_build_meshes_nodes == BuildMeshesNodes::Yes) {
        build_meshes_nodes(selected_instance->get_matrix());
    }

    set_enabled_scene_nodes(&m_scene_provider->scene(), false, m_main_node);
}

void ClipperPresenter::deactivate(bool force_enabled_scene_nodes /*= true*/)
{
    reset();
    set_enabled_scene_nodes(&m_scene_provider->scene(), true, m_main_node, force_enabled_scene_nodes);
}

void ClipperPresenter::reset()
{
    if (m_main_node) {
        m_scene_provider->scene().remove_children(
            [&](const Node* node)
            {
                const ClipperElement* tag = node->tag_of_type<ClipperElement>();
                return tag->type != ClipperElementType::Undef;
            },
            m_main_node
        );

        m_model_geometry_manager.release_all();
        m_model_triangle_mesh_manager.release_all();
    }
    m_contour_enabled = m_mesh_enabled = m_plane_enabled = true;
}

void ClipperPresenter::init_main_node(Node* parent_node)
{
    NodeBuilder builder{m_scene_provider->scene()};

    builder.set_debug_name(fmt::format("Clipper main {}", inst_counter)).set_tag(ClipperElement());
    inst_counter++;

    m_scene_provider->scene().add_child(builder.build().release(), parent_node);
    m_main_node = parent_node->children().back().get();
}

void ClipperPresenter::build_meshes_nodes(const Domain::Transform3d& inst_trafo)
{
    const size_t volumes_cnt = m_clipper->volumes().size();
    for (size_t volume_id = 0; volume_id < volumes_cnt; volume_id++) {
        const Domain::ModelVolume* volume = m_clipper->volumes()[volume_id];

        ClipperElement id{ClipperElementType::Mesh, volume_id};

        const auto& trimesh = m_model_triangle_mesh_manager.get_or_create(
            id,
            [&]() -> std::unique_ptr<TriangleMesh>
            { return std::make_unique<TriangleMesh>(volume->mesh_ptr()); }
        );
        const auto* geom = m_model_geometry_manager.get_or_create(
            id,
            [&]() { return Render::geometry_from_triangle_mesh(*m_device, trimesh->triangles()); }
        );

        auto trafo = inst_trafo * volume->get_matrix();

        ColorRGBA color = m_mesh_color;
        if (!volume->is_model_part())
            color.set(3, 0.75f);
        auto material =
            Render::Material{}
                .set_shader(m_device->context().shader_manager().shader("gouraud_light_clip"))
                .set_uniform("uniform_color", color)
                .set_transparent(color.is_transparent());

        NodeBuilder builder{m_scene_provider->scene()};
        builder.set_debug_name(fmt::format("Clipped model:vol {}", volume_id))
            .set_tag(id)
            .set_mesh(geom, material, int(0))
            .transform([trafo, volume](auto& xform) { xform = trafo; });

        m_scene_provider->scene().add_child(builder.build().release(), m_main_node);
    }
}

Domain::Vec4f get_clipping_plane_data(const Clipper* clipper)
{
    Domain::Vec4f clp_data_out(0.f, 0.f, 1.f, FLT_MAX);
    // Take care of the clipping plane. The normal of the clipping plane is
    // saved with opposite sign than we need to pass to OpenGL (FIXME)
    if (bool clipping_plane_active = clipper->get_position() != 0.; clipping_plane_active) {
        const Biz::ClippingPlane& clp = clipper->get_clipping_plane();
        for (size_t i = 0; i < 3; ++i)
            clp_data_out[i] = -1.f * float(clp.get_data()[i]);
        clp_data_out[3] = float(clp.get_data()[3]);
    }

    return clp_data_out;
}

void ClipperPresenter::build_non_mesh_node(
    ClipperElementType type,
    const indexed_triangle_set& its,
    size_t clipper_id,
    size_t island_id
)
{
    if (its.empty()) {
        // Skip render node creation for Clipper islands without a contour or plane.
        return;
    }

    ASSERT(type == ClipperElementType::Plane || type == ClipperElementType::Contour);
    ClipperElement id{type, clipper_id, island_id};

    indexed_triangle_set mesh_its = its;
    const auto& trimesh           = m_model_triangle_mesh_manager.get_or_create(
        id,
        [&]() -> std::unique_ptr<TriangleMesh>
        { return std::make_unique<TriangleMesh>(std::move(mesh_its)); }
    );
    const auto* geom = m_model_geometry_manager.get_or_create(
        id,
        [&]() { return Render::geometry_from_triangle_mesh(*m_device, trimesh->triangles()); }
    );

    const bool is_plane = type == ClipperElementType::Plane;
    auto material =
        Render::Material{}
            .set_shader(m_device->context().shader_manager().shader(/*"gouraud_light"*/ "flat"))
            .set_uniform("uniform_color", is_plane ? m_plane_color : m_contour_color);

    NodeBuilder bldr(m_scene_provider->scene());
    bldr.set_debug_name(
            fmt::format(
                "Clipped {}:id {}, island {}",
                is_plane ? "plane" : "contour",
                clipper_id,
                island_id
            )
    )
        .set_tag(id)
        .set_mesh(geom, material, int(0));

    if (is_plane) {
        bldr.set_aabb(trimesh->aabb_mesh());
    }

    m_scene_provider->scene().add_child(bldr.build().release(), m_main_node);
}

void ClipperPresenter::update_nodes()
{
    // update clipping plane data for meshes
    visit(
        *m_main_node,
        [&](Node& node)
        {
            ClipperElement* tag   = node.tag_of_type<ClipperElement>();
            ASSERT(tag);
            auto render_component = static_cast<MeshRenderNodeComponent*>(node.render_component());

            if (tag->type == ClipperElementType::Mesh) {
                Render::Material material = render_component->material();
                material.set_uniform("clipping_plane", get_clipping_plane_data(m_clipper));
                node.set_material_override(material);
                return;
            }
        },
        true
    );

    // remove Plane and Contour nodes

    m_scene_provider->scene().remove_children(
        [&](const Node* node)
        {
            const ClipperElement* tag = node->tag_of_type<ClipperElement>();
            if (tag->type == ClipperElementType::Plane || tag->type == ClipperElementType::Contour)
            {
                m_model_triangle_mesh_manager.release(*tag);
                m_model_geometry_manager.release(*tag);
                return true;
            }
            return false;
        },
        m_main_node
    );

    // Build new Plane and Contour nodes

    size_t clipper_id = 0;
    for (const auto& [mesh_clipper, trafo] : m_clipper->object_clippers) {
        if (mesh_clipper->result) {
            size_t island_id = 0;
            for (const Biz::MeshClipper::CutIsland& island :
                 mesh_clipper->result.value().cut_islands)
            {
                if (std::find(
                        m_ignored_ids.begin(),
                        m_ignored_ids.end(),
                        MeshClipperContourId{clipper_id, island_id}
                    )
                    == m_ignored_ids.end())
                {
                    // Add Plane
                    build_non_mesh_node(
                        ClipperElementType::Plane,
                        island.model,
                        clipper_id,
                        island_id
                    );

                    // Add Contour
                    build_non_mesh_node(
                        ClipperElementType::Contour,
                        island.model_expanded,
                        clipper_id,
                        island_id
                    );
                }

                island_id++;
            }
            clipper_id++;
        }
    }
}

void ClipperPresenter::set_clickable_plane(bool clickable) {}

static void set_node_enabled(Node* parent, ClipperElementType node_type, bool enabled)
{
    visit(
        *parent,
        [&](Node& node)
        {
            ClipperElement* tag = node.tag_of_type<ClipperElement>();
            if (tag->type == node_type) {
                node.set_enabled(enabled);
                return;
            }
        },
        true
    );
}

static void set_node_color(Node* parent, ClipperElementType node_type, ColorRGBA color)
{
    visit(
        *parent,
        [&](Node& node)
        {
            ClipperElement* tag = node.tag_of_type<ClipperElement>();
            if (tag->type == node_type) {
                Render::Material material = node.render_component()->material();
                material.set_uniform("uniform_color", color);
                node.set_material_override(material);
                return;
            }
        },
        true
    );
}

void ClipperPresenter::set_enable_mesh(bool enable)
{
    if (m_mesh_enabled != enable) {
        m_mesh_enabled = enable;
        set_node_enabled(m_main_node, ClipperElementType::Mesh, m_mesh_enabled);
    }
}

void ClipperPresenter::set_enable_plane(bool enable)
{
    if (m_plane_enabled != enable) {
        m_plane_enabled = enable;
        set_node_enabled(m_main_node, ClipperElementType::Plane, m_plane_enabled);
    }
}

void ClipperPresenter::set_enable_contour(bool enable)
{
    if (m_contour_enabled != enable) {
        m_contour_enabled = enable;
        set_node_enabled(m_main_node, ClipperElementType::Contour, m_contour_enabled);
    }
}

void ClipperPresenter::set_color_mesh(Slic3r::Domain::ColorRGBA color)
{
    if (m_mesh_color != color) {
        m_mesh_color = color;
        set_node_color(m_main_node, ClipperElementType::Mesh, m_mesh_color);
    }
}

void ClipperPresenter::set_color_plane(Slic3r::Domain::ColorRGBA color)
{
    if (m_plane_color != color) {
        m_plane_color = color;
        set_node_color(m_main_node, ClipperElementType::Plane, m_plane_color);
    }
}

void ClipperPresenter::set_color_contour(Slic3r::Domain::ColorRGBA color)
{
    if (m_contour_color != color) {
        m_contour_color = color;
        set_node_color(m_main_node, ClipperElementType::Contour, m_contour_color);
    }
}

void ClipperPresenter::reset_ignored()
{
    m_ignored_ids.clear();
}

void ClipperPresenter::set_ignored(const std::vector<MeshClipperContourId>& ids)
{
    m_ignored_ids = ids;
    update_nodes();
}

bool ClipperPresenter::has_ignored() const
{
    return !m_ignored_ids.empty();
}

bool ClipperPresenter::has_valid_contour() const
{
    return m_clipper && m_clipper->has_valid_contour();
}

void ClipperPresenter::set_position_by_ratio(double pos, bool keep_normal)
{
    if (m_clipper) {
        m_clipper->set_position_by_ratio(pos, keep_normal);
        // reset_ignored();
        update_nodes();
    }
}

void ClipperPresenter::update_clipper(
    const Domain::Vec3d& clp_normal,
    double clp_offset,
    double pos,
    bool force_reset_ignored
)
{
    if (m_clipper) {
        if (force_reset_ignored) {
            reset_ignored();
        }
        m_clipper->set_range_and_pos(clp_normal, clp_offset, pos);
        update_nodes();
    }
}

void
ClipperPresenter::set_limiting_plane(const Domain::Vec3d& plane_normal, const double plane_offset)
{
    if (m_clipper) {
        m_clipper->set_limiting_plane(plane_normal, plane_offset);
        update_nodes();
    }
}

void ClipperPresenter::set_behavior(bool hide_clipped, bool fill_cut, double contour_width)
{
    if (m_clipper) {
        m_clipper->set_behavior(hide_clipped, fill_cut, contour_width);
        update_nodes();
    }
}

bool ClipperPresenter::is_outside_of_cut_contour(const Domain::Vec3d& point) const
{
    if (m_clipper) {
        // get id of clipper contour possible contains a point
        MeshClipperContourId id = m_clipper->get_mesh_clipper_contour_id_from_projection(point);
        if (id.is_valid()) {
            // check if point is inside the ignored contours
            return std::find(m_ignored_ids.begin(), m_ignored_ids.end(), id) != m_ignored_ids.end();
        }
        return true;
    }
    return true;
}

// Unprojects the mouse ray on the mesh and saves hit point and normal of the facet into pos_and_normal
// Return false if no intersection was found, true otherwise.
bool ClipperPresenter::unproject_on_cut_plane(
    const Ray& ray,
    Domain::Vec3d& pos_world,
    bool respect_contours
)
{
    MeshClipperContourId contour_id;
    bool can_unproject = m_clipper->unproject_on_cut_plane(ray, pos_world, contour_id);
    if (respect_contours && can_unproject) {
        // Do not react to clicks outside a contour (or inside a contour that is ignored)
        // check if point is inside the ignored contours
        return std::find(m_ignored_ids.begin(), m_ignored_ids.end(), contour_id)
            == m_ignored_ids.end();
    }
    return false;
}

} // namespace Slic3r::App::Scene
