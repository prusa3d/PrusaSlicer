
#include "Slic3r/App/Plater/CutGizmo.hpp"

#include "Slic3r/App/Plater/CutDialog.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/Domain/CutConnector.hpp"
#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/App/Scene/Plane.hpp"

#include <Slic3r/App/Render/GeometryBuilder.hpp>
#include "Slic3r/App/Scene/Node.hpp"
#include <Slic3r/App/Scene/NodeBuilder.hpp>
#include <Slic3r/App/Scene/NodeVisitor.hpp>
#include "Slic3r/App/Scene/GeometryDataFactory.hpp"
#include "Slic3r/App/Scene/AabbRaycastNodeComponent.hpp"
#include "Slic3r/App/Scene/MeshRenderNodeComponent.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Plater/PlaterSceneLayer.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/ModelVolume.hpp"
#include "Slic3r/Biz/Algorithms/Line.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/Geometry.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ArrangeInteractor.hpp"
#include "Slic3r/Biz/Utils/CutUtils.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp" // ISceneSelectionChangedListener

#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/enum_bitmask.hpp"

#include "Slic3r/Math.hpp"

using namespace Slic3r::App::Yoga;

using Slic3r::App::Scene::SceneNodeTag;
using Slic3r::Domain::ColorRGBA;

static const ColorRGBA UPPER_PART_COLOR    = ColorRGBA(0.0f, 1.0f, 1.0f, 1.0f);
static const ColorRGBA LOWER_PART_COLOR    = ColorRGBA(1.0f, 0.0f, 1.0f, 1.0f);
static const ColorRGBA DEF_PART_COLOR      = ColorRGBA(0.9f, 0.9f, 0.9f, 1.0f);
static const ColorRGBA CUT_PLANE_DEF_COLOR = ColorRGBA(0.9f, 0.9f, 0.9f, 0.5f);
static const ColorRGBA CUT_PLANE_ERR_COLOR = ColorRGBA(1.0f, 0.8f, 0.8f, 0.5f);
static const ColorRGBA CUT_LINE_COLOR      = ColorRGBA::YELLOW();

// connector colors
static const ColorRGBA PLAG_COLOR           = ColorRGBA::YELLOW();
static const ColorRGBA DOWEL_COLOR          = ColorRGBA::DARK_YELLOW();
static const ColorRGBA HOVERED_PLAG_COLOR   = ColorRGBA::CYAN();
static const ColorRGBA HOVERED_DOWEL_COLOR  = ColorRGBA(0.0f, 0.5f, 0.5f, 1.0f);
static const ColorRGBA SELECTED_PLAG_COLOR  = ColorRGBA::GRAY();
static const ColorRGBA SELECTED_DOWEL_COLOR = ColorRGBA::DARK_GRAY();
static const ColorRGBA CONNECTOR_DEF_COLOR  = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f);
static const ColorRGBA CONNECTOR_ERR_COLOR  = ColorRGBA(1.0f, 0.3f, 0.3f, 1.0f);
static const ColorRGBA HOVERED_ERR_COLOR    = ColorRGBA(1.0f, 0.3f, 0.3f, 1.0f);

const unsigned int ScaleStepsCount  = 72;
const unsigned int SnapRegionsCount = 8;

using namespace Slic3r::Domain;
using namespace Slic3r::Biz;
using namespace Slic3r::Biz::Algorithms;
using namespace Slic3r::Biz::Algorithms::Geometry;

namespace Slic3r::App::Plater {

// code is borrowed from:
// #include <arrange-wrapper/SceneBuilder.hpp>
static Domain::BoundingBox3d instance_bounding_box(
    const Domain::ModelInstance& mi,
    const Domain::Transform3d& tr = Domain::Transform3d::Identity(),
    bool dont_translate           = false
)
{
    using Slic3r::Biz::Algorithms::BoundingBox::merge;

    Domain::BoundingBox3d bb;
    const Domain::Transform3d inst_matrix = dont_translate ?
        mi.get_transformation().get_matrix_no_offset() :
        mi.get_transformation().get_matrix();

    for (Domain::ModelVolume* v : mi.get_object()->volumes) {
        if (v->is_model_part()) {
            bb = merge(
                bb,
                Slic3r::Biz::Algorithms::ModelVolume::transformed_bounding_box(
                    *v,
                    tr * inst_matrix * v->get_matrix()
                )
            );
        }
    }

    return bb;
}

static void place_on_bed(ModelInstance* instance)
{
    Domain::BoundingBox3d bb = instance_bounding_box(*instance);

    const Domain::Vec3d min = bb.min;
    if (std::abs(min.z()) > Domain::EPSILON) {
        Vec3d translation = instance->get_offset();
        translation.z() -= min.z();
        instance->set_offset(translation);
    };
}

static double get_handle_mean_size(const BoundingBox3d& bb)
{
    Domain::Vec3d bb_size = Biz::Algorithms::BoundingBox::sizes(bb);
    return (bb_size.x() + bb_size.y() + bb_size.z()) / 30.;
}

static double get_half_size(double size)
{
    return std::max(size * 0.5, 0.05);
}

static indexed_triangle_set its_make_groove_plane(
    Biz::Cut::Groove& groove,
    std::vector<Domain::Vec3d>& groove_vertices,
    const BoundingBox3d& bb,
    double radius
)
{
    // values for calculation

    const float side_width = static_cast<float>(
        is_approx(groove.flaps_angle, 0.) ? groove.depth : (groove.depth / sin(groove.flaps_angle))
    );
    const float flaps_width = 2.f * side_width * static_cast<float>(cos(groove.flaps_angle));

    const float groove_half_width_upper = 0.5f * static_cast<float>(groove.width);
    const float groove_half_width_lower = 0.5f * (static_cast<float>(groove.width) + flaps_width);

    const float cut_plane_radius = 1.5f * static_cast<float>(radius);
    const float cut_plane_length = 1.5f * cut_plane_radius;

    const float groove_half_depth = 0.5f * static_cast<float>(groove.depth);

    const float x = 0.5f * cut_plane_radius;
    const float y = 0.5f * cut_plane_length;
    float z_upper = groove_half_depth;
    float z_lower = -groove_half_depth;

    const float proj = y * static_cast<float>(tan(groove.angle));

    float ext_upper_x = groove_half_width_upper + proj; // upper_x extension
    float ext_lower_x = groove_half_width_lower + proj; // lower_x extension

    float nar_upper_x = groove_half_width_upper - proj; // upper_x narrowing
    float nar_lower_x = groove_half_width_lower - proj; // lower_x narrowing

    const float cut_plane_thiknes = 0.02f * (float) get_handle_mean_size(bb); // cut_plane_thiknes

    // Vertices of the groove used to detection if groove is valid
    // They are written as:
    // {left_ext_lower, left_nar_lower, left_ext_upper, left_nar_upper,
    // right_ext_lower, right_nar_lower, right_ext_upper, right_nar_upper }
    {
        groove_vertices.clear();
        groove_vertices.reserve(8);

        groove_vertices.emplace_back(Vec3f(-ext_lower_x, -y, z_lower).cast<double>());
        groove_vertices.emplace_back(Vec3f(-nar_lower_x, y, z_lower).cast<double>());
        groove_vertices.emplace_back(Vec3f(-ext_upper_x, -y, z_upper).cast<double>());
        groove_vertices.emplace_back(Vec3f(-nar_upper_x, y, z_upper).cast<double>());
        groove_vertices.emplace_back(Vec3f(ext_lower_x, -y, z_lower).cast<double>());
        groove_vertices.emplace_back(Vec3f(nar_lower_x, y, z_lower).cast<double>());
        groove_vertices.emplace_back(Vec3f(ext_upper_x, -y, z_upper).cast<double>());
        groove_vertices.emplace_back(Vec3f(nar_upper_x, y, z_upper).cast<double>());
    }

    // Different cases of groove plane:

    // groove is open

    if (groove_half_width_upper > proj && groove_half_width_lower > proj) {
        indexed_triangle_set mesh;

        auto get_vertices = [x,
                             y](float z_upper,
                                float z_lower,
                                float nar_upper_x,
                                float nar_lower_x,
                                float ext_upper_x,
                                float ext_lower_x)
        {
            return std::vector<stl_vertex>(
                {// upper left part vertices
                 {-x, -y, z_upper},
                 {-x, y, z_upper},
                 {-nar_upper_x, y, z_upper},
                 {-ext_upper_x, -y, z_upper},
                 // lower part vertices
                 {-ext_lower_x, -y, z_lower},
                 {-nar_lower_x, y, z_lower},
                 {nar_lower_x, y, z_lower},
                 {ext_lower_x, -y, z_lower},
                 // upper right part vertices
                 {ext_upper_x, -y, z_upper},
                 {nar_upper_x, y, z_upper},
                 {x, y, z_upper},
                 {x, -y, z_upper}
                }
            );
        };

        mesh.vertices =
            get_vertices(z_upper, z_lower, nar_upper_x, nar_lower_x, ext_upper_x, ext_lower_x);
        mesh.vertices.reserve(2 * mesh.vertices.size());

        z_upper -= cut_plane_thiknes;
        z_lower -= cut_plane_thiknes;

        const float under_x_shift = cut_plane_thiknes / tan(0.5f * groove.flaps_angle);

        nar_upper_x += under_x_shift;
        nar_lower_x += under_x_shift;
        ext_upper_x += under_x_shift;
        ext_lower_x += under_x_shift;

        std::vector<stl_vertex> vertices =
            get_vertices(z_upper, z_lower, nar_upper_x, nar_lower_x, ext_upper_x, ext_lower_x);
        mesh.vertices.insert(mesh.vertices.end(), vertices.begin(), vertices.end());

        mesh.indices = {
            // above view
            {5, 4, 7},
            {5, 7, 6}, // lower part
            {3, 4, 5},
            {3, 5, 2}, // left side
            {9, 6, 8},
            {8, 6, 7}, // right side
            {1, 0, 2},
            {2, 0, 3}, // upper left part
            {9, 8, 10},
            {10, 8, 11}, // upper right part
            // under view
            {20, 21, 22},
            {20, 22, 23}, // upper right part
            {12, 13, 14},
            {12, 14, 15}, // upper left part
            {18, 21, 20},
            {18, 20, 19}, // right side
            {16, 15, 14},
            {16, 14, 17}, // left side
            {16, 17, 18},
            {16, 18, 19}, // lower part
            // left edge
            {1, 13, 0},
            {0, 13, 12},
            // front edge
            {0, 12, 3},
            {3, 12, 15},
            {3, 15, 4},
            {4, 15, 16},
            {4, 16, 7},
            {7, 16, 19},
            {7, 19, 20},
            {7, 20, 8},
            {8, 20, 11},
            {11, 20, 23},
            // right edge
            {11, 23, 10},
            {10, 23, 22},
            // back edge
            {1, 13, 2},
            {2, 13, 14},
            {2, 14, 17},
            {2, 17, 5},
            {5, 17, 6},
            {6, 17, 18},
            {6, 18, 9},
            {9, 18, 21},
            {9, 21, 10},
            {10, 21, 22}
        };
        return mesh;
    }

    const float tan_groove_angle = static_cast<float>(tan(groove.angle));
    float cross_pt_upper_y       = groove_half_width_upper / tan_groove_angle;

    // groove is closed

    const float tan_half_flaps_angle = static_cast<float>(tan(0.5f * groove.flaps_angle));
    if (groove_half_width_upper < proj && groove_half_width_lower < proj) {
        float cross_pt_lower_y = groove_half_width_lower / tan_groove_angle;

        indexed_triangle_set mesh;

        auto get_vertices = [x,
                             y](float z_upper,
                                float z_lower,
                                float cross_pt_upper_y,
                                float cross_pt_lower_y,
                                float ext_upper_x,
                                float ext_lower_x)
        {
            return std::vector<stl_vertex>(
                {// upper part vertices
                 {-x, -y, z_upper},
                 {-x, y, z_upper},
                 {x, y, z_upper},
                 {x, -y, z_upper},
                 {ext_upper_x, -y, z_upper},
                 {0.f, cross_pt_upper_y, z_upper},
                 {-ext_upper_x, -y, z_upper},
                 // lower part vertices
                 {-ext_lower_x, -y, z_lower},
                 {0.f, cross_pt_lower_y, z_lower},
                 {ext_lower_x, -y, z_lower}
                }
            );
        };

        mesh.vertices = get_vertices(
            z_upper,
            z_lower,
            cross_pt_upper_y,
            cross_pt_lower_y,
            ext_upper_x,
            ext_lower_x
        );
        mesh.vertices.reserve(2 * mesh.vertices.size());

        z_upper -= cut_plane_thiknes;
        z_lower -= cut_plane_thiknes;

        const float under_x_shift = cut_plane_thiknes / tan_half_flaps_angle;

        cross_pt_upper_y += cut_plane_thiknes;
        cross_pt_lower_y += cut_plane_thiknes;
        ext_upper_x += under_x_shift;
        ext_lower_x += under_x_shift;

        std::vector<stl_vertex> vertices = get_vertices(
            z_upper,
            z_lower,
            cross_pt_upper_y,
            cross_pt_lower_y,
            ext_upper_x,
            ext_lower_x
        );
        mesh.vertices.insert(mesh.vertices.end(), vertices.begin(), vertices.end());

        mesh.indices = {
            // above view
            {8, 7, 9}, // lower part
            {5, 8, 6},
            {6, 8, 7}, // left side
            {4, 9, 8},
            {4, 8, 5}, // right side
            {1, 0, 6},
            {1, 6, 5},
            {1, 5, 2},
            {2, 5, 4},
            {2, 4, 3}, // upper part
            // under view
            {10, 11, 16},
            {16, 11, 15},
            {15, 11, 12},
            {15, 12, 14},
            {14, 12, 13}, // upper part
            {18, 15, 14},
            {14, 18, 19}, // right side
            {17, 16, 15},
            {17, 15, 18}, // left side
            {17, 18, 19}, // lower part
            // left edge
            {1, 11, 0},
            {0, 11, 10},
            // front edge
            {0, 10, 6},
            {6, 10, 16},
            {6, 17, 16},
            {6, 7, 17},
            {7, 17, 19},
            {7, 19, 9},
            {4, 14, 19},
            {4, 19, 9},
            {4, 14, 13},
            {4, 13, 3},
            // right edge
            {3, 13, 12},
            {3, 12, 2},
            // back edge
            {2, 12, 11},
            {2, 11, 1}
        };

        return mesh;
    }

    // groove is closed from the roof

    indexed_triangle_set mesh;
    mesh.vertices = {
        // upper part vertices
        {-x, -y, z_upper},
        {-x, y, z_upper},
        {x, y, z_upper},
        {x, -y, z_upper},
        {ext_upper_x, -y, z_upper},
        {0.f, cross_pt_upper_y, z_upper},
        {-ext_upper_x, -y, z_upper},
        // lower part vertices
        {-ext_lower_x, -y, z_lower},
        {-nar_lower_x, y, z_lower},
        {nar_lower_x, y, z_lower},
        {ext_lower_x, -y, z_lower}
    };

    mesh.vertices.reserve(2 * mesh.vertices.size() + 1);

    z_upper -= cut_plane_thiknes;
    z_lower -= cut_plane_thiknes;

    const float under_x_shift = cut_plane_thiknes / tan_half_flaps_angle;

    nar_lower_x += under_x_shift;
    ext_upper_x += under_x_shift;
    ext_lower_x += under_x_shift;

    std::vector<stl_vertex> vertices = {
        // upper part vertices
        {-x, -y, z_upper},
        {-x, y, z_upper},
        {x, y, z_upper},
        {x, -y, z_upper},
        {ext_upper_x, -y, z_upper},
        {under_x_shift, cross_pt_upper_y, z_upper},
        {-under_x_shift, cross_pt_upper_y, z_upper},
        {-ext_upper_x, -y, z_upper},
        // lower part vertices
        {-ext_lower_x, -y, z_lower},
        {-nar_lower_x, y, z_lower},
        {nar_lower_x, y, z_lower},
        {ext_lower_x, -y, z_lower}
    };
    mesh.vertices.insert(mesh.vertices.end(), vertices.begin(), vertices.end());

    mesh.indices = {
        // above view
        {8, 7, 10},
        {8, 10, 9}, // lower part
        {5, 8, 7},
        {5, 7, 6}, // left side
        {4, 10, 9},
        {4, 9, 5}, // right side
        {1, 0, 6},
        {1, 6, 5},
        {1, 5, 2},
        {2, 5, 4},
        {2, 4, 3}, // upper part
        // under view
        {11, 12, 18},
        {18, 12, 17},
        {17, 12, 16},
        {16, 12, 13},
        {16, 13, 15},
        {15, 13, 14}, // upper part
        {21, 16, 15},
        {21, 15, 22}, // right side
        {19, 18, 17},
        {19, 17, 20}, // left side
        {19, 20, 21},
        {19, 21, 22}, // lower part
        // left edge
        {1, 12, 11},
        {1, 11, 0},
        // front edge
        {0, 11, 18},
        {0, 18, 6},
        {7, 19, 18},
        {7, 18, 6},
        {7, 19, 22},
        {7, 22, 10},
        {10, 22, 15},
        {10, 15, 4},
        {4, 15, 14},
        {4, 14, 3},
        // right edge
        {3, 14, 13},
        {3, 14, 2},
        // back edge
        {2, 13, 12},
        {2, 12, 1},
        {5, 16, 21},
        {5, 21, 9},
        {9, 21, 20},
        {9, 20, 8},
        {5, 17, 20},
        {5, 20, 8}
    };

    return mesh;
}

static Vec3d extract_position(const App::Scene::Transform& xform)
{
    return xform.matrix().block<3, 1>(0, 3);
}

bool CutGizmo::SolidAABBMesh::intersects_line(
    Domain::Vec3d point,
    Domain::Vec3d direction,
    const Domain::Transform3d& inst_trafo
) const
{
    Transform3d trafo_inv = (inst_trafo * trafo).inverse();
    Vec3d to              = trafo_inv * (point + direction);
    point                 = trafo_inv * point;
    direction             = (to - point).normalized();

    std::vector<AABBMesh::hit_result> hits     = aabb_mesh.get()->query_ray_hits(point, direction);
    std::vector<AABBMesh::hit_result> neg_hits = aabb_mesh.get()->query_ray_hits(point, -direction);

    return !hits.empty() || !neg_hits.empty();
}

CutGizmo::CutGizmo(
    Render::Device& device,
    Scene::GeometryDataFactory& data_factory,
    PlaterScenePresenter& scene_presenter,
    Biz::ProjectInteractor* project_interactor
) :
    m_device(device),
    m_data_factory(data_factory),
    m_scene_presenter(scene_presenter),
    m_project_interactor(project_interactor)
{
    m_project_contexts = std::make_unique<ProjectContexts>(*project_interactor);

    m_dialog                      = std::make_unique<CutDialog>();
    m_dialog->callbacks().perform = [this]() { perform_cut(); };

    m_dialog->callbacks().z_changed = [this](double new_Z)
    {
        if (!Domain::fuzzy_compare(m_plane_center.z(), new_Z)) {
            m_plane_center.z() = new_Z;
            update_cut_plane_trafo();
            if (is_planar_mode()) {
                update_clipper_presenter();
            }
            preprocess_cut();
        }
    };

    m_dialog->callbacks().mode_changed = [this]()
    {
        update_cut_plane_mesh();
        update_cut_plane_trafo();
        update_nodes_on_mode_changed();
        context().state.is_planar_mode = is_planar_mode();
        take_snapshot(UndoSnapshotType::CutChangeMode);
    };

    m_dialog->callbacks().groove_depth_value_changed = [this](double value)
    {
        context().state.groove.depth = value;
        update_cut_plane_mesh();
    };
    m_dialog->callbacks().groove_depth_tolerance_changed = [this](double value)
    {
        context().state.groove.depth_tolerance = value;
        update_cut_plane_mesh();
    };
    m_dialog->callbacks().groove_width_value_changed = [this](double value)
    {
        context().state.groove.width = value;
        update_cut_plane_mesh();
    };
    m_dialog->callbacks().groove_width_tolerance_changed = [this](double value)
    {
        context().state.groove.width_tolerance = value;
        update_cut_plane_mesh();
    };
    m_dialog->callbacks().flap_angle_changed = [this](double value)
    {
        // Convert the degree value to an angle in radians.
        context().state.groove.flaps_angle = deg2rad(value);
        update_cut_plane_mesh();
    };
    m_dialog->callbacks().groove_angle_changed = [this](double value)
    {
        // Convert the degree value to an angle in radians.
        context().state.groove.angle = deg2rad(value);
        update_cut_plane_mesh();
    };

    m_dialog->callbacks().reset_connectors = [this]()
    {
        reset_connectors();
        take_snapshot(UndoSnapshotType::CutResetConnectors);
    };

    m_dialog->callbacks().connector_attributes_changed = [this](UndoSnapshotType undo_snapshot_type)
    {
        update_selected_connectors(true);
        take_snapshot(undo_snapshot_type);
    };

    m_dialog->callbacks().connector_transformations_changed = [this]()
    { update_selected_connectors(false); };

    m_dialog->callbacks().snap_settings_changed = [this]() { update_snap_nodes(); };

    m_dialog->callbacks().reset_cut_plane = [this]()
    {
        set_plane_center(m_bb_center);
        m_start_dragging_m = context().state.rotation_m = Transform3d::Identity();

        update_cut_plane_trafo();
        update_cut_plane_mesh();
    };

    m_dialog->callbacks().flip_cut_plane = [this]() { flip_cut_plane(); };

    m_dialog->callbacks().connectors_editing_changed = [this](bool connectors_editing)
    {
        update_nodes_on_mode_changed();
        update_cut_plane_color();
        context().state.connectors_editing = connectors_editing;
    };

    m_dialog->callbacks().cut_settings_changed = [this](bool force_take_snapshot)
    {
        context().state.keep_as_parts    = keep_as_parts();
        context().state.part_state_upper = m_dialog->part_state_upper();
        context().state.part_state_lower = m_dialog->part_state_lower();
        if (force_take_snapshot) {
            take_snapshot(UndoSnapshotType::CutChangeMainSettings);
        }
    };
}

void CutGizmo::take_snapshot(UndoSnapshotType undo_snapshot_type)
{
    if (!m_ignore_dialog_events) {
        m_project_interactor->undo_provider().take_snapshot(undo_snapshot_type);
    }
}

void CutGizmo::on_thumbnail_render_begin()
{
    // Before rendering a thumbnail, hide gizmo nodes and show original model nodes.
    if (m_main_node != nullptr) {
        m_main_node->set_enabled(false);
    }

    m_clipper_presenter.set_enable_plane(false);
    m_clipper_presenter.set_enable_contour(false);
    this->set_enabled_scene_nodes(true);
}

void CutGizmo::on_thumbnail_render_end()
{
    // After rendering a thumbnail, restore gizmo nodes and hide original model nodes.
    if (m_main_node != nullptr) {
        m_main_node->set_enabled(true);
    }

    m_clipper_presenter.set_enable_contour(this->is_planar_mode());
    m_clipper_presenter.set_enable_plane(this->is_planar_mode());
    this->set_enabled_scene_nodes(false);
}

void CutGizmo::on_activated()
{
    init_scene_nodes();

    update_scene_nodes();
    set_enabled_scene_nodes(false);

    m_scene_presenter.scene().add_listener<IThumbnailRenderListener>(this);
}

void CutGizmo::on_deactivated()
{
    reset_cut_part_meshes();
    reset_connectors_nodes();
    set_enabled_scene_nodes(true);
    m_clipper_presenter.set_enable_mesh(true);
    m_clipper_presenter.deactivate(m_main_node);

    // Clear solid meshes to force their recreation when the selected object is changed
    m_solid_meshes.clear();

    m_scene_presenter.scene().remove_listener<IThumbnailRenderListener>(this);
}

void CutGizmo::on_project_activated(size_t new_project_id)
{
    on_activated();
}

void CutGizmo::on_project_deactivated(size_t old_project_id)
{
    on_deactivated();
}

void CutGizmo::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
)
{
    // the gizmo is active
    if (m_dialog->is_visible() && enabled()) {
        update_scene_nodes();
    }
}

void CutGizmo::on_scene_selection_transformed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
)
{
    // the gizmo is active
    if (m_dialog->is_visible()) {
        update_scene_nodes();
    }
}

std::optional<App::Undo::ToolState> CutGizmo::get_tool_state() const
{
    return context().state;
}

void CutGizmo::set_tool_state(const App::Undo::ToolState& tool_state)
{
    ASSERT(std::holds_alternative<Undo::CutGizmoState>(tool_state));
    const Undo::CutGizmoState& state = std::get<Undo::CutGizmoState>(tool_state);

    context().state = state;
    if (m_dialog->is_visible()) {
        update_scene_nodes();
    }
}

Scene::ToolType CutGizmo::type() const
{
    return Scene::ToolType::CutGizmo;
}

bool CutGizmo::enabled() const
{
    const Biz::Scene::ObjectSelection& selection =
        m_project_interactor->scene_interactor().object_selection();
    return selection.state() == Biz::Scene::SelectionState::WholeInstance;
}

GizmoWindowPtr CutGizmo::release_ui_window()
{
    return m_dialog.release();
}

Vec3d CutGizmo::mouse_position_in_local_plane(AxisType axis, const Domain::Line3d& mouse_ray) const
{
    double half_pi = 0.5 * PI;

    Transform3d m = Transform3d::Identity();

    switch (axis) {
    case AxisType::XAxis: {
        m.rotate(Eigen::AngleAxisd(half_pi, Vec3d::UnitZ()));
        m.rotate(Eigen::AngleAxisd(-half_pi, Vec3d::UnitY()));
        break;
    }
    case AxisType::YAxis: {
        m.rotate(Eigen::AngleAxisd(half_pi, Vec3d::UnitY()));
        m.rotate(Eigen::AngleAxisd(half_pi, Vec3d::UnitZ()));
        break;
    }
    case AxisType::ZAxis:
    default: {
        // no rotation applied
        break;
    }
    }

    m = m * m_start_dragging_m.inverse();
    m.translate(-m_plane_center);

    return Biz::Algorithms::Line::intersect_plane(
        Biz::Algorithms::Line::transformed(mouse_ray, m),
        0.
    );
}

void CutGizmo::dragging_handle_rotation(const Domain::Line3d& mouse_ray)
{
    const AxisType axis = m_hovered_handle.axis;
    const Vec2d mouse_pos =
        Biz::Algorithms::Point::to_2d(mouse_position_in_local_plane(axis, mouse_ray));

    const Vec2d orig_dir = Vec2d::UnitX();
    const Vec2d new_dir  = mouse_pos.normalized();

    const double two_pi = 2.0 * PI;

    double theta = ::acos(std::clamp(new_dir.dot(orig_dir), -1.0, 1.0));
    if (cross2(orig_dir, new_dir) < 0.0)
        theta = two_pi - theta;

    const double len = mouse_pos.norm();
    // snap to coarse snap region
    if (m_snap_coarse_in_radius <= len && len <= m_snap_coarse_out_radius) {
        const double step = two_pi / double(SnapRegionsCount);
        theta             = step * std::round(theta / step);
    }
    // snap to fine snap region (scale)
    else if (m_snap_fine_in_radius <= len && len <= m_snap_fine_out_radius)
    {
        const double step = two_pi / double(ScaleStepsCount);
        theta             = step * std::round(theta / step);
    }

    if (is_approx(theta, two_pi))
        theta = 0.0;
    if (axis != AxisType::YAxis)
        theta += 0.5 * PI;

    if (!is_approx(theta, 0.0))
        reset_preprocess_cut();

    Vec3d rotation                       = Vec3d::Zero();
    rotation[static_cast<int>(axis) - 1] = theta;

    const Transform3d rotation_tmp = m_start_dragging_m * rotation_transform(rotation);
    context().state.rotation_m     = rotation_tmp;

    if (is_planar_mode()) {
        update_clipper_presenter();
    }
    update_cut_plane_trafo();
}

static Vec3d ray_origin_on_near_z_plane(const Scene::Camera& camera, Scene::Ray pick_ray)
{
    Scene::Plane near_z_plane = Scene::Plane::from_point_and_normal(
        camera.position() + (camera.cam_projection().z_near() + 1) * camera.forward(),
        camera.forward()
    );
    double t = 0.0;
    near_z_plane.intersects(pick_ray, t);
    return pick_ray.point_at(t);
}

Scene::GizmoActivationState CutGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    const auto event_type          = ctx.mouse_event().type();
    const auto event_key_modifiers = ctx.mouse_event().key_modifiers();
    const auto event_button        = ctx.mouse_event().button();
    if (event_type != Platform::MouseEvent::Type::ButtonDown
        && event_type != Platform::MouseEvent::Type::Move
        && event_type != Platform::MouseEvent::Type::ButtonUp)
    {
        on_stop_dragging();
        return Scene::GizmoActivationState::Inactive;
    }

    if (const bool is_looking_forward =
            m_scene_presenter.scene().camera().forward().dot(m_cut_normal) < 0.05;
        m_is_looking_forward_on_cut_plane != is_looking_forward)
    {
        m_is_looking_forward_on_cut_plane = is_looking_forward;
        update_clipper_presenter(false);
        update_parts_nodes_enabled();
    }

    if (m_dialog->connectors_editing) {
        return on_mouse_for_connectors(ctx, only_active);
    }

    if (m_is_cut_line_processing) {
        return on_mouse_for_cut_line(ctx, only_active);
    }

    Handle hovered_handle;
    if (const Scene::Node* handle_node = ctx.pick_result_node_with_tag_of_type<CutHandleNodeTag>())
    {
        const CutHandleNodeTag& tag = *handle_node->tag_of_type<CutHandleNodeTag>();
        ASSERT(tag.type == CutHandleNodeTag::Type::Handle);
        hovered_handle = tag.handle();
    }
    update_handles_nodes(hovered_handle);

    const auto& pick_ray = ctx.pick_ray();
    if (event_type == Platform::MouseEvent::Type::ButtonDown) {
        Scene::Node* part_node = ctx.pick_result_node_with_tag_of_type<CutPartNodeTag>();
        if (is_planar_mode() && part_node && event_button == Platform::MouseButton::Right) {
            // this part is clicked
            m_part_selection.toggle_part(part_node->tag_of_type<CutPartNodeTag>()->id);
            update_parts_nodes_colors_from_selection();
            return Scene::GizmoActivationState::Done;
        }

        if (const Scene::Node* handle_node =
                ctx.pick_result_node_with_tag_of_type<CutHandleNodeTag>())
        {
            const CutHandleNodeTag& tag = *handle_node->tag_of_type<CutHandleNodeTag>();
            ASSERT(tag.type == CutHandleNodeTag::Type::Handle);
            m_hovered_handle   = Handle(tag.handle_type, tag.primary_axis);
            m_is_plane_hovered = false;
            m_translation_ray.direction =
                context().state.rotation_m * axis_type_dir(tag.primary_axis);
        } else if (ctx.pick_result_node_with_tag_of_type<CutPlaneNodeTag>()) {
            m_is_plane_hovered          = true;
            m_translation_ray.direction = context().state.rotation_m * Vec3d::UnitZ();
        } else if (
            event_button == Platform::MouseButton::Left
            && event_key_modifiers & Platform::KeyModifiers(Platform::KeyModifier::Shift)
            && !m_is_cut_line_processing
        )
        {
            m_is_cut_line_processing = true;
            m_line_beg = ray_origin_on_near_z_plane(m_scene_presenter.scene().camera(), pick_ray);
            m_line_end = m_line_beg;
            return Scene::GizmoActivationState::Active;
        } else {
            on_stop_dragging();
            return Scene::GizmoActivationState::Inactive;
        }

        m_translation_ray.origin =
            extract_position(m_scene_presenter.selection_root().world_transform());
    }

    double t;
    if (!m_translation_ray.closest_point_from_ray(pick_ray, t) && !m_is_connector_handled) {
        m_dragging = false;
        return Scene::GizmoActivationState::Inactive;
    }

    if (event_type == Platform::MouseEvent::Type::ButtonDown
        && (m_is_plane_hovered || !m_hovered_handle.is_undef()))
    {
        m_dragging         = true;
        m_start_t          = t;
        m_start_dragging_m = context().state.rotation_m;
        m_can_flip_plane   = m_is_plane_hovered;
        return Scene::GizmoActivationState::Active;
    }

    if (event_type == Platform::MouseEvent::Type::ButtonDown && m_is_cut_line_processing) {
        return Scene::GizmoActivationState::Active;
    }

    if (!m_dragging)
        return Scene::GizmoActivationState::Inactive;

    if (m_is_plane_hovered || m_hovered_handle.is_move()) {
        if (!Domain::fuzzy_compare(m_start_t, t)) {
            Vec3d delta = m_translation_ray.point_at(t) - m_translation_ray.point_at(m_start_t);
            m_start_t   = t;
            if (set_plane_center(m_plane_center + delta)) {
                if (is_planar_mode()) {
                    update_clipper_presenter();
                }
                update_cut_plane_trafo();
            }
            m_can_flip_plane = false;
        }
    } else if (m_hovered_handle.is_rotation()) {
        dragging_handle_rotation(Domain::Line3d(pick_ray.origin, pick_ray.point_at(10.0)));
    }

    if (event_type == Platform::MouseEvent::Type::ButtonUp) {
        m_dragging         = false;
        m_is_plane_hovered = false;
        m_hovered_handle   = Handle();

        if (m_can_flip_plane) {
            flip_cut_plane();
        } else {
            preprocess_cut();
            take_snapshot(UndoSnapshotType::CutPlaneMove);
        }

        return Scene::GizmoActivationState::Done;
    }

    return Scene::GizmoActivationState();
}

Scene::GizmoActivationState
CutGizmo::on_mouse_for_connectors(Scene::GizmoEventContext& ctx, bool only_active)
{
    const auto event_type          = ctx.mouse_event().type();
    const auto event_button        = ctx.mouse_event().button();
    const auto event_key_modifiers = ctx.mouse_event().key_modifiers();
    const auto& pick_ray           = ctx.pick_ray();

    if (event_type == Platform::MouseEvent::Type::ButtonDown) {
        if (ctx.pick_result_node_with_tag_of_type<CutConnectorNodeTag>() != nullptr) {
            if (event_button == Platform::MouseButton::Right) {
                select_hovered_connector(true);
                remove_selected_connectors();

                return Scene::GizmoActivationState::Done;
            }

            m_is_connector_handled = m_dragging = true;
            m_clipper_presenter.unproject_on_cut_plane(pick_ray, m_btn_down_pos);

            return Scene::GizmoActivationState::Active;
        }
        const Scene::Node* clipper_plane_node =
            ctx.pick_result_node_with_tag_of_type<Scene::ClipperElement>();
        if (clipper_plane_node && event_button == Platform::MouseButton::Left) {
            if (clipper_plane_node->tag_of_type<Scene::ClipperElement>()->type
                == Scene::ClipperElementType::Plane)
            {
                Domain::Vec3d pos_world;
                if (m_clipper_presenter.unproject_on_cut_plane(pick_ray, pos_world)) {
                    add_connector(pos_world);
                }

                return Scene::GizmoActivationState::Done;
            }
        }
        unselect_all_connectors();
        update_connectors_nodes_colors();
    }

    if (!m_dragging) {
        const Scene::Node* connector_node =
            ctx.pick_result_node_with_tag_of_type<CutConnectorNodeTag>();
        std::optional<size_t> hovered_connector_id = connector_node ?
            std::optional<size_t>(connector_node->tag_of_type<CutConnectorNodeTag>()->id) :
            std::nullopt;

        if (m_hovered_connector_id != hovered_connector_id) {
            m_hovered_connector_id = hovered_connector_id;
            update_connectors_nodes_colors();
        }

        return Scene::GizmoActivationState::Inactive;
    }

    if (m_is_connector_handled) {
        Domain::Vec3d pos_world;
        const bool can_move = m_clipper_presenter.unproject_on_cut_plane(pick_ray, pos_world);

        if (can_move && m_btn_down_pos != pos_world) {
            // move connector on new position
            const size_t id                                   = m_hovered_connector_id.value();
            context().selected_object->cut_connectors[id].pos = get_local_pos(pos_world);
            check_and_update_connectors_state();
            update_connector_node(id);
            if (event_type == Platform::MouseEvent::Type::ButtonUp) {
                take_snapshot(UndoSnapshotType::CutMoveConnector);
            }
        } else if (event_type == Platform::MouseEvent::Type::ButtonUp) {
            if (event_key_modifiers & Platform::KeyModifiers(Platform::KeyModifier::Alt)) {
                unselect_hovered_connector();
            } else {
                bool add_to_selection =
                    event_key_modifiers & Platform::KeyModifiers(Platform::KeyModifier::Shift);
                select_hovered_connector(!add_to_selection);
            }
            update_connectors_nodes_colors();
            update_dialog_on_selection_changed();
        }
    }

    if (event_type == Platform::MouseEvent::Type::ButtonUp && m_is_connector_handled) {
        m_is_connector_handled = m_dragging = false;
        m_btn_down_pos                      = Vec3d::Zero();
        return Scene::GizmoActivationState::Done;
    }

    return Scene::GizmoActivationState();
}

void CutGizmo::on_cycle_prepare() {}

bool CutGizmo::disable_object_selection() const
{
    return true;
}

void CutGizmo::provide_clipper(Scene::Clipper& clipper)
{
    m_clipper_presenter = Scene::ClipperPresenter(&clipper, &m_device, &m_scene_presenter);
}

void CutGizmo::provide_gizmo_controller(Scene::IGizmoController& controller)
{
    m_controller = &controller;
}

void CutGizmo::on_stop_dragging()
{
    m_dragging = false;
}

bool CutGizmo::set_plane_center(const Vec3d& center_pos)
{
    if (!is_planar_mode()) {
        // Non-planar mode allows translation of the cut plane while keeping its normal fixed.
        // Ensure the new plane center stays inside the bounding sphere of the bounding box.
        const double dist   = (m_bb_center - center_pos).norm();
        const double radius = (m_bb_center - context().state.bounding_box.min).norm();
        if (dist > radius) {
            return false;
        }
    }

    // Compute the projection radius of the bounding box onto the plane normal.
    // Absolute values are used because extents contribute regardless of direction.
    Vec3d bb_half_extents =
        (context().state.bounding_box.max - context().state.bounding_box.min) * 0.5;
    Vec3d abs_plane_normal =
        Vec3d(std::abs(m_cut_normal.x()), std::abs(m_cut_normal.y()), std::abs(m_cut_normal.z()));
    double bb_radius = abs_plane_normal.dot(bb_half_extents);

    // Compute the distance from the bounding box center to the newly positioned cut plane
    double bb_center_dist = std::abs(m_cut_normal.dot(m_bb_center) - m_cut_normal.dot(center_pos));

    if (bb_center_dist <= bb_radius) {
        // The center position is updated only if the cut plane actually cuts the object bounding box
        m_plane_center                = center_pos;
        context().state.center_offset = m_plane_center - m_bb_center;
        m_dialog->set_cut_z_position(m_plane_center.z());
        return true;
    }

    return false;
}

void CutGizmo::update_scene_nodes()
{
    static const double in_to_mm = 25.4;
    static const double mm_to_in = 1 / in_to_mm;

    const Biz::Scene::ObjectSelection& selection =
        m_project_interactor->scene_interactor().object_selection();

    if (selection.elements.size() != 1
        || selection.mode != Slic3r::Biz::Scene::SelectionMode::Instance)
    {
        // We can't perform a cut for multiple objects simultaneously.
        // So, just deactivate CutGizmo
        if (m_controller) {
            m_controller->deactivate_current_tool();
        }
        return;
    }

    Domain::Project& project          = m_project_interactor->selected_project();
    const Domain::ElementRef& element = selection.elements.front();
    if (element.is_wipe_tower()) {
        return;
    }

    ASSERT(element.volume_id == 0); // Whole object is selected

    Domain::ModelObject* new_object = project.find_object_by_id(element.object_id);
    if (context().selected_object != new_object) {
        context().selected_object = new_object;
    }

    if (m_solid_meshes.empty()) {
        for (const ModelVolume* volume : context().selected_object->volumes) {
            if (volume->is_model_part()) {
                m_solid_meshes.emplace_back(
                    SolidAABBMesh{
                        std::make_shared<AABBMesh>(volume->mesh().its),
                        volume->get_matrix()
                    }
                );
            }
        }
    }

    const Domain::ModelInstance* new_inst =
        project.find_instance_by_id(element.object_id, element.instance_id);

    const Domain::ModelInstancePtrs& instances = context().selected_object->instances;

    if (context().selected_instance != new_inst
        || context().instance_idx >= instances.size()
        || instances[context().instance_idx] != new_inst)
    {
        context().selected_instance = new_inst;

        // get instance index
        context().instance_idx = 0;
        for (const auto* inst : instances) {
            if (inst == context().selected_instance)
                break;
            context().instance_idx++;
        }
        ASSERT(context().instance_idx < instances.size());
    }
    update_connectors_nodes();
    unselect_all_connectors();

    // update ui values
    using namespace Slic3r::Biz::Algorithms::BoundingBox;

    if (BoundingBoxf3 inst_bb = instance_bounding_box(*context().selected_instance);
        !context().state.bounding_box.defined
        || !approx_equals(context().state.bounding_box, inst_bb))
    {
        context().state = {};

        context().state.bounding_box = inst_bb;
        context().state.rotation_m   = Transform3d::Identity();

        context().state.groove.depth = context().state.groove.depth_init =
            std::max(1., 0.5 * get_handle_mean_size(context().state.bounding_box));
        context().state.groove.width = context().state.groove.width_init =
            4.0 * context().state.groove.depth;
        context().state.groove.flaps_angle = context().state.groove.flaps_angle_init =
            Biz::Algorithms::Geometry::PI / 3.;
        context().state.groove.angle = context().state.groove.angle_init = 0.;
    }

    m_bb_center = center(context().state.bounding_box);

    Domain::Vec3d bb_size = sizes(context().state.bounding_box);
    m_mean_size =
        (bb_size.x() + bb_size.y() + bb_size.z()) / 9.0 * (m_imperial_units ? mm_to_in : 1.);
    m_radius = 0.5 * bb_size.norm();

    m_contour_width = is_planar_mode() ? 0.4f : 0.f;

    m_handle_connection_len = 0.5 * m_radius; // std::min<double>(0.75 * m_radius, 35.0);
    m_handle_radius         = m_handle_connection_len * 0.85;

    m_snap_coarse_in_radius  = m_handle_radius / 3.0;
    m_snap_coarse_out_radius = m_snap_coarse_in_radius * 2.;
    m_snap_fine_in_radius    = m_handle_connection_len * 0.85;
    m_snap_fine_out_radius   = m_handle_connection_len * 1.15;
    m_start_dragging_m       = context().state.rotation_m;

    m_ignore_dialog_events = true;
    {
        ScopeGuard update_quard([this]() { m_ignore_dialog_events = false; });

        m_dialog->set_build_size(bb_size, m_bb_center.z());
        m_dialog->set_groove_values(context().state.groove, m_mean_size);
        m_dialog->set_connector_defaults(m_mean_size);
        m_dialog->update_from_gizmo_state(context().state);
        set_plane_center(m_bb_center + context().state.center_offset);
    }

    update_cut_normal();

    m_clipper_presenter.activate(context().selected_object, context().selected_instance, m_main_node);
    m_clipper_presenter.set_behavior(true, true, 0.4);

    update_cut_plane_mesh();
    update_cut_plane_trafo();

    m_clipper_presenter.set_enable_mesh(false);

    update_nodes_on_mode_changed();
}

void CutGizmo::build_cut_part_mesh(
    CutPartNodeTag::Type type,
    size_t part_id,
    std::shared_ptr<const Slic3r::Domain::TriangleMesh> mesh,
    const Transform3d& trafo,
    Scene::NodeBuilder& builder
)
{
    const std::string type_str = type == CutPartNodeTag::Type::Upper ? "UpperPart" :
        type == CutPartNodeTag::Type::Lower                          ? "LowerPart" :
                                                                       "UNDEF";
    SPDLOG_DEBUG("build_volume type:{}", type_str);

    auto& geom_mgr    = m_model_geometry_manager;
    auto& trimesh_mgr = m_model_triangle_mesh_manager;

    CutAuxiliaryElementId id{CutAuxiliaryElementId::Type::CutPart, part_id};
    const auto& trimesh = trimesh_mgr.get_or_create(
        id,
        [&]() -> std::unique_ptr<Scene::TriangleMesh>
        { return std::make_unique<Scene::TriangleMesh>(mesh); }
    );
    const auto* geom = geom_mgr.get_or_create(
        id,
        [&]() { return Render::geometry_from_triangle_mesh(m_device, trimesh->triangles()); }
    );
    ColorRGBA color = type == CutPartNodeTag::Type::Upper ? UPPER_PART_COLOR :
        type == CutPartNodeTag::Type::Lower               ? LOWER_PART_COLOR :
                                                            ColorRGBA{1.0f, 1.0f, 1.0f, 1.0f};

    auto material = Render::Material{}
                        .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
                        .set_uniform("uniform_color", color)
                        .set_transparent(color.is_transparent());

    builder.set_debug_name(fmt::format("cut type: {}", type_str))
        .set_tag(CutPartNodeTag(type, part_id))
        .set_mesh(geom, material, int(0))
        .transform([trafo](auto& xform) { xform = trafo; })
        .set_aabb(trimesh->aabb_mesh())
        .set_shadows(Render::Shadows{true, true})
        .set_pbr(Scene::DEFAULT_VOLUME_PBRPARAMS);
}

void CutGizmo::reset_cut_part_meshes()
{
    // Remove all cut parts Scene::Nodes
    m_scene_presenter.scene().remove_children(
        [this](const Scene::Node* node)
        {
            const CutPartNodeTag* tag = node->tag_of_type<CutPartNodeTag>();
            if (tag) {
                CutAuxiliaryElementId id(CutAuxiliaryElementId::Type::CutPart, tag->id);
                m_model_geometry_manager.release(id);
                m_model_triangle_mesh_manager.release(id);
            }
            return tag != nullptr;
        },
        m_main_node
    );
}

void CutGizmo::reset_connectors_nodes()
{
    m_scene_presenter.scene().remove_children(
        [&](const Scene::Node* node) { return true; },
        m_connectors_node
    );
}

void CutGizmo::set_enabled_scene_nodes(bool enabled)
{
    Scene::visit(
        m_scene_presenter.scene().root(),
        [&](Scene::Node& node)
        {
            SceneNodeTag* tag = node.tag_of_type<SceneNodeTag>();
            if (tag != nullptr) {
                node.set_enabled(enabled);
            }
        },
        true
    );
    if (m_main_node) {
        m_main_node->set_enabled(!enabled);
    }
}

void CutGizmo::build_cut_plane_node(Scene::NodeBuilder& builder)
{
    SPDLOG_DEBUG("Cut element type:Cut plane");

    // Make default mesh as small as possible.
    // Its geometry will be updated on plane mode change
    indexed_triangle_set mesh_its = Biz::Algorithms::TriangleMesh::its_make_cube(0.1f, 0.1f, 0.1f);

    CutAuxiliaryElementId id{CutAuxiliaryElementId::Type::CutPlane};

    const auto& trimesh = m_model_triangle_mesh_manager.get_or_create(
        id,
        [&]() -> std::unique_ptr<Scene::TriangleMesh>
        { return std::make_unique<Scene::TriangleMesh>(std::move(mesh_its)); }
    );
    const auto* geom = m_model_geometry_manager.get_or_create(
        id,
        [&]() { return Render::geometry_from_triangle_mesh(m_device, trimesh->triangles()); }
    );

    ColorRGBA color = CUT_PLANE_DEF_COLOR;
    auto material   = Render::Material{}
                          .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
                          .set_uniform("uniform_color", color);

    builder.set_debug_name("cut: cut plane:")
        .set_tag(CutPlaneNodeTag())
        .set_mesh(geom, material, int(0));
}

void CutGizmo::build_cut_line_node(Scene::NodeBuilder& builder)
{
    SPDLOG_DEBUG("Cut element type:Cut line");

    auto material = Render::Material{}
                        .set_shader(m_device.context().shader_manager().shader("flat"))
                        .set_uniform("uniform_color", CUT_LINE_COLOR);

    builder.set_debug_name("cut: cut line:")
        .set_tag(CutLineNodeTag())
        .set_mesh(m_data_factory.geometry(Scene::GeometryDataId::Segment), material, int(0));
}

void CutGizmo::update_cut_line_node()
{
    Vec3d line      = m_line_end - m_line_beg;
    double line_len = line.norm();

    bool is_enabled = m_is_cut_line_processing && line_len > 3.;
    m_cut_line_node->set_enabled(is_enabled);

    if (!is_enabled)
        return;

    Eigen::Quaterniond q;
    q.setFromTwoVectors(Vec3d::UnitX(), line);

    Scene::Transform trafo = Scene::Transform::Identity();
    trafo.translate(m_line_beg);
    trafo.rotate(Eigen::AngleAxisd(q));
    trafo.scale(line_len);
    m_cut_line_node->set_local_transform(trafo);
}

void CutGizmo::build_handles_nodes(Scene::NodeBuilder& builder)
{
    SPDLOG_DEBUG("build_volume type:Cut plane handles");

    static constexpr double SCALE_FACTOR = 0.075;

    builder.set_debug_name("Handles");
    builder.set_tag(CutHandleNodeTag());

    // Add GradedCircle as a helper for rotation handles

    builder.child(
        [&](Scene::NodeBuilder& child_bldr)
        {
            Render::Material material =
                Render::Material{}
                    .set_shader(m_device.context().shader_manager().shader("flat"))
                    .set_uniform("uniform_color", ColorRGBA::WHITE());

            child_bldr.set_debug_name("GradedCircle")
                .set_tag(
                    CutHandleNodeTag(CutHandleNodeTag::Type::GradedCircle, Handle::Type::Rotation)
                )
                .set_mesh(
                    m_data_factory.geometry(Scene::GeometryDataId::GradedCircle),
                    material,
                    Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles)
                );
        }
    );

    // Add Stems as a helpers for Z/X move and Z rotation handles

    auto create_stem_node =
        [&](Scene::NodeBuilder& child_bldr, Handle::Type handle_type, AxisType axis)
    {
        Render::Material material =
            Render::Material{}
                .set_shader(m_device.context().shader_manager().shader("flat"))
                .set_uniform("uniform_color", ColorRGBA::YELLOW());

        child_bldr.set_debug_name("stem")
            .set_tag(CutHandleNodeTag(CutHandleNodeTag::Type::Stem, handle_type, axis))
            .set_mesh(
                m_data_factory.geometry(Scene::GeometryDataId::Segment),
                material,
                Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles)
            );
    };

    builder.child([&](Scene::NodeBuilder& child_bldr)
                  { create_stem_node(child_bldr, Handle::Type::Move, AxisType::ZAxis); });

    builder.child([&](Scene::NodeBuilder& child_bldr)
                  { create_stem_node(child_bldr, Handle::Type::Move, AxisType::XAxis); });

    builder.child([&](Scene::NodeBuilder& child_bldr)
                  { create_stem_node(child_bldr, Handle::Type::Rotation, AxisType::ZAxis); });

    auto create_sphere_node = [&](Scene::NodeBuilder& child_bldr,
                                  Handle::Type handle_type,
                                  AxisType axis,
                                  const std::string& debug_name)
    {
        Render::Material material =
            Render::Material{}
                .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
                .set_uniform("uniform_color", ColorRGBA::WHITE());

        child_bldr.set_debug_name(debug_name)
            .set_tag(CutHandleNodeTag(CutHandleNodeTag::Type::Handle, handle_type, axis))
            .set_mesh(
                m_data_factory.geometry(Scene::GeometryDataId::Sphere),
                material,
                Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles)
            )
            .set_screen_space_sized_modifier(SCALE_FACTOR)
            .set_aabb(m_data_factory.triangle_mesh(Scene::GeometryDataId::Sphere)->aabb_mesh());
    };

    // Add Handle for is_move_z

    builder.child(
        [&](Scene::NodeBuilder& child_bldr)
        { create_sphere_node(child_bldr, Handle::Type::Move, AxisType::ZAxis, "HandleZMove"); }
    );

    // Add Handle for is_move_x

    builder.child(
        [&](Scene::NodeBuilder& child_bldr)
        { create_sphere_node(child_bldr, Handle::Type::Move, AxisType::XAxis, "HandleXMove"); }
    );

    // Add handles for rotation

    auto create_cone_node = [&](Scene::NodeBuilder& internal_bldr, AxisType axis, bool is_cw)
    {
        Render::Material material =
            Render::Material{}
                .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
                .set_uniform("uniform_color", axis_color(axis));

        internal_bldr.set_debug_name("Cone")
            .set_tag(CutHandleNodeTag(
                CutHandleNodeTag::Type::Handle,
                Handle::Type::Rotation,
                axis,
                is_cw
            ))
            .set_mesh(
                m_data_factory.geometry(Scene::GeometryDataId::Cone),
                material,
                Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles)
            )
            .set_screen_space_sized_modifier(SCALE_FACTOR)
            .set_aabb(m_data_factory.triangle_mesh(Scene::GeometryDataId::Cone)->aabb_mesh());
    };

    auto create_rotation_handle_nodes =
        [&](Scene::NodeBuilder& child_bldr, AxisType axis, const std::string& debug_name)
    {
        child_bldr.set_debug_name(debug_name);
        child_bldr.set_tag(
            CutHandleNodeTag(CutHandleNodeTag::Type::Handle, Handle::Type::Rotation, axis)
        );

        child_bldr.child([&](Scene::NodeBuilder& internal_bldr)
                         { create_cone_node(internal_bldr, axis, true); });

        child_bldr.child([&](Scene::NodeBuilder& internal_bldr)
                         { create_cone_node(internal_bldr, axis, false); });
    };

    builder.child(
        [&](Scene::NodeBuilder& child_bldr)
        { create_rotation_handle_nodes(child_bldr, AxisType::XAxis, "HandleXRotation"); }
    );

    builder.child(
        [&](Scene::NodeBuilder& child_bldr)
        { create_rotation_handle_nodes(child_bldr, AxisType::YAxis, "HandleYRotation"); }
    );

    builder.child(
        [&](Scene::NodeBuilder& child_bldr)
        {
            create_rotation_handle_nodes(child_bldr, AxisType::ZAxis, "HandleZRotation");

            child_bldr.child(
                [&](Scene::NodeBuilder& internal_bldr)
                {
                    create_sphere_node(
                        internal_bldr,
                        Handle::Type::Rotation,
                        AxisType::ZAxis,
                        "HandleZRotation"
                    );
                }
            );
        }
    );
}

void CutGizmo::update_cut_plane_color()
{
    ColorRGBA cp_clr          = can_perform_cut() ? CUT_PLANE_DEF_COLOR : CUT_PLANE_ERR_COLOR;
    Render::Material material = m_plane_node->render_component()->material();
    material.set_uniform("uniform_color", cp_clr).set_transparent(cp_clr.is_transparent());
    m_plane_node->set_material_override(material);
}

void CutGizmo::update_cut_plane_mesh()
{
    if (m_ignore_dialog_events)
        return;

    const double cp_width    = 0.02 * m_mean_size;
    indexed_triangle_set its = is_planar_mode() ?
        Biz::Algorithms::TriangleMesh::its_make_frustum_dowel(1.2 * m_radius, cp_width, 4) :
        its_make_groove_plane(
            context().state.groove,
            m_groove_vertices,
            context().state.bounding_box,
            m_radius
        );

    CutAuxiliaryElementId id{CutAuxiliaryElementId::Type::CutPlane};
    m_model_triangle_mesh_manager.release(id);
    m_model_geometry_manager.release(id);

    const auto& trimesh = m_model_triangle_mesh_manager.get_or_create(
        id,
        [&, this]() -> std::unique_ptr<Scene::TriangleMesh>
        { return std::make_unique<Scene::TriangleMesh>(std::move(its)); }
    );
    const auto* geom = m_model_geometry_manager.get_or_create(
        id,
        [&]() { return Render::geometry_from_triangle_mesh(m_device, trimesh->triangles()); }
    );

    static_cast<Scene::MeshRenderNodeComponent*>(m_plane_node->render_component())
        ->set_geometry(geom);

    m_plane_node->set_raycast_component(new Scene::AabbRaycastNodeComponent(&trimesh->aabb_mesh()));

    if (is_planar_mode()) {
        update_clipper_presenter();
    }

    update_cut_plane_color();

    preprocess_cut();
}

void CutGizmo::update_cut_plane_trafo()
{
    update_cut_normal();

    ColorRGBA cp_clr = can_perform_cut() ? CUT_PLANE_DEF_COLOR : CUT_PLANE_ERR_COLOR;

    Render::Material material = m_plane_node->render_component()->material();
    material.set_uniform("uniform_color", cp_clr).set_transparent(cp_clr.is_transparent());
    m_plane_node->set_material_override(material);

    m_plane_node->set_local_transform(
        Domain::translation_transform(m_plane_center) * context().state.rotation_m
    );

    update_handles_nodes();

    update_dialog_state();
}

void CutGizmo::update_handles_material_and_enability(Handle hovered_handle)
{
    const bool disabled_handles =
        (m_dragging && m_is_plane_hovered) || m_dialog->connectors_editing;
    m_handles_node->set_enabled(!disabled_handles);
    if (disabled_handles)
        return;

    bool is_groove_mode          = !is_planar_mode();
    bool is_undef_hovered_handle = hovered_handle.is_undef();

    Scene::visit(
        *m_handles_node,
        [&](Scene::Node& node)
        {
            if (&node == m_handles_node)
                return;
            CutHandleNodeTag* tag = node.tag_of_type<CutHandleNodeTag>();
            if (!tag)
                return;
            const Handle tag_handle = tag->handle();

            if (tag->type == CutHandleNodeTag::Type::Stem) {
                if (tag_handle.is_move_z()) {
                    node.set_enabled(
                        !hovered_handle.is_move_x() && !hovered_handle.is_rotation_z()
                    );
                } else if (tag_handle.is_move_x()) {
                    node.set_enabled(
                        is_groove_mode && (hovered_handle.is_move_x() || is_undef_hovered_handle)
                    );
                } else if (tag_handle.is_rotation_z()) {
                    node.set_enabled(is_groove_mode && hovered_handle.is_rotation_z());
                }
            } else if (tag->type == CutHandleNodeTag::Type::GradedCircle) {
                node.set_enabled(hovered_handle.is_rotation());
            } else if (tag_handle.is_move()) {
                Render::Material material = node.render_component()->material();
                bool is_enabled{false};

                if (tag_handle.is_z_axis()) {
                    is_enabled = !hovered_handle.is_move_x() && !hovered_handle.is_rotation_z();

                    ColorRGBA color = hovered_handle.is_move_z() ? ColorRGBA::YELLOW() :
                        hovered_handle.is_rotation_x()           ? ColorRGBA::CYAN() :
                        hovered_handle.is_rotation_y()           ? ColorRGBA::MAGENTA() :
                                                                   ColorRGBA::GRAY();
                    material.set_uniform("uniform_color", color);
                } else if (tag_handle.is_x_axis()) {
                    is_enabled =
                        is_groove_mode && (hovered_handle.is_move_x() || is_undef_hovered_handle);

                    material.set_uniform(
                        "uniform_color",
                        hovered_handle.is_move_z() ? ColorRGBA::YELLOW() : ColorRGBA::GRAY()
                    );
                }

                node.set_enabled(is_enabled);
                node.set_material_override(material);
            } else if (tag_handle.is_rotation_z()) {
                for (const auto& node_child : node.children()) {
                    CutHandleNodeTag* tag = node_child.get()->tag_of_type<CutHandleNodeTag>();
                    assert(tag);
                    if (tag->is_cw) {
                        // cones for is_rotation_z don't need a changed color, just set_enabled
                        node_child->set_enabled(is_groove_mode && hovered_handle.is_rotation_z());
                    } else {
                        node_child->set_enabled(
                            is_groove_mode
                            && (hovered_handle.is_rotation_z() || is_undef_hovered_handle)
                        );
                        Render::Material material = node_child->render_component()->material();
                        material.set_uniform(
                            "uniform_color",
                            hovered_handle.is_rotation_z() ? ColorRGBA::BLUE() : ColorRGBA::GRAY()
                        );
                        node_child->set_material_override(material);
                    }
                }
            } else {
                for (const auto& node_child : node.children()) {
                    CutHandleNodeTag* tag = node_child.get()->tag_of_type<CutHandleNodeTag>();
                    assert(tag && tag->is_cw);
                    const bool hovered_axis =
                        hovered_handle.is_rotation() && hovered_handle.axis == tag->primary_axis;
                    node_child->set_enabled(is_undef_hovered_handle || hovered_axis);
                    Render::Material material = node_child->render_component()->material();

                    ColorRGBA color = !hovered_axis          ? axis_color(tag->primary_axis) :
                        tag->primary_axis == AxisType::XAxis ? ColorRGBA::CYAN() :
                                                               ColorRGBA::MAGENTA();

                    material.set_uniform("uniform_color", color);
                    node_child->set_material_override(material);
                }
            }
        },
        true
    );
}

void CutGizmo::update_handles_local_fransform(Handle hovered_handle)
{
    const Transform3d trafo = translation_transform(m_plane_center) * context().state.rotation_m;

    const double size = get_half_size(get_handle_mean_size(context().state.bounding_box));

    Scene::visit(
        *m_handles_node,
        [&](Scene::Node& node)
        {
            CutHandleNodeTag* tag = node.tag_of_type<CutHandleNodeTag>();
            if (!tag)
                return;
            Handle tag_h = tag->handle();

            if (tag->type == CutHandleNodeTag::Type::Stem) {
                Vec3d rotate       = Vec3d::Zero();
                double length_koef = 1.;
                if (tag_h.is_move_z()) {
                    rotate = -0.5 * PI * Vec3d::UnitY();
                } else if (tag_h.is_move_x()) {
                    // length_koef = 0.75;
                } else if (tag_h.is_rotation_z()) {
                    rotate      = -0.5 * PI * Vec3d::UnitZ();
                    length_koef = 1.75;
                }
                node.set_local_transform(
                    trafo
                    * rotation_transform(rotate)
                    * scale_transform(length_koef * m_handle_connection_len * Vec3d::UnitX())
                );
            } else if (tag->type == CutHandleNodeTag::Type::GradedCircle) {
                Transform3d rotate_trafo = hovered_handle.is_x_axis() ?
                    rotation_transform(0.5 * PI * Vec3d::UnitY())
                        * rotation_transform(-PI * Vec3d::UnitZ()) :
                    hovered_handle.is_y_axis() ? rotation_transform(-0.5 * PI * Vec3d::UnitZ())
                        * rotation_transform(-0.5 * PI * Vec3d::UnitY()) :
                    hovered_handle.is_y_axis() ? rotation_transform(-0.5 * PI * Vec3d::UnitZ()) :
                                                 Transform3d::Identity();

                node.set_local_transform(
                    translation_transform(m_plane_center)
                    * (m_dragging ? m_start_dragging_m : context().state.rotation_m)
                    * rotate_trafo
                    * scale_transform(2.0 * m_handle_radius * Vec3d::Ones())
                );
            } else if (tag_h.is_move()) {
                node.set_local_transform(
                    trafo
                    * translation_transform(
                        m_handle_connection_len * axis_type_dir(tag->primary_axis)
                    )
                    * scale_transform(size)
                );
            } else if (tag_h.is_rotation()) {
                if (tag_h.is_x_axis()) {
                    for (const auto& node_child : node.children()) {
                        CutHandleNodeTag* tag = node_child.get()->tag_of_type<CutHandleNodeTag>();
                        assert(tag && tag->is_cw);
                        Vec3d offset = Vec3d(
                            0.0,
                            (tag->is_cw.value() ? 1. : -1.) * size,
                            m_handle_connection_len
                        );
                        Vec3d rotation =
                            0.5 * (tag->is_cw.value() ? -1. : 1.) * PI * Vec3d::UnitX();
                        node_child->set_local_transform(
                            trafo * translation_transform(offset) * rotation_transform(rotation)
                            // * scale_transform(scale)
                        );
                    }
                } else if (tag_h.is_y_axis()) {
                    for (const auto& node_child : node.children()) {
                        CutHandleNodeTag* tag = node_child.get()->tag_of_type<CutHandleNodeTag>();
                        assert(tag && tag->is_cw);
                        Vec3d offset = Vec3d(
                            (tag->is_cw.value() ? 1. : -1.) * size,
                            0.0,
                            m_handle_connection_len
                        );
                        Vec3d rotation =
                            0.5 * (tag->is_cw.value() ? 1. : -1.) * PI * Vec3d::UnitY();
                        node_child->set_local_transform(
                            trafo * translation_transform(offset) * rotation_transform(rotation)
                            // * scale_transform(scale)
                        );
                    }
                } else if (tag_h.is_z_axis()) {
                    double handle_y_shift = -1.75 * m_handle_connection_len;
                    for (const auto& node_child : node.children()) {
                        CutHandleNodeTag* tag = node_child.get()->tag_of_type<CutHandleNodeTag>();
                        assert(tag);
                        if (tag->is_cw) {
                            Vec3d offset =
                                Vec3d((tag->is_cw.value() ? 1. : -1.) * size, handle_y_shift, 0.);
                            Vec3d rotation =
                                0.5 * (tag->is_cw.value() ? 1. : -1.) * PI * Vec3d::UnitY();
                            node_child->set_local_transform(
                                trafo * translation_transform(offset) * rotation_transform(rotation)
                                // * scale_transform(scale)
                            );
                        } else {
                            node_child->set_local_transform(
                                trafo * translation_transform(handle_y_shift * Vec3d::UnitY())
                                // * scale_transform(size)
                            );
                        }
                    }
                }
            }
        },
        false // process trafos just for enabled nodes
    );
}

void CutGizmo::update_handles_nodes(Handle hovered_handle)
{
    if (hovered_handle.is_undef()) {
        hovered_handle = m_hovered_handle;
    }

    update_handles_material_and_enability(hovered_handle);
    update_handles_local_fransform(hovered_handle);
}

void CutGizmo::update_nodes_on_mode_changed()
{
    bool connectors_editing = m_dialog->connectors_editing;
    m_handles_node->set_enabled(!connectors_editing);
    m_plane_node->set_enabled(!connectors_editing);
    update_parts_nodes_enabled();

    m_connectors_node->set_enabled(is_planar_mode());
    if (connectors_editing) {
        check_and_update_connectors_state();
    } else {
        update_connectors_nodes_colors();
    }

    m_clipper_presenter.set_enable_contour(is_planar_mode());
    m_clipper_presenter.set_enable_plane(is_planar_mode());
}

void CutGizmo::put_connectors_on_cut_plane(const Vec3d& cp_normal, double cp_offset)
{
    CutConnectors& connectors = context().selected_object->cut_connectors;
    if (connectors.empty())
        return;

    for (size_t id = 0; id < connectors.size(); id++) {
        auto& connector = connectors[id];
        // convert connetor pos to the world coordinates
        Vec3d pos_world = get_world_pos(connector.pos);

        // scalar distance from point to plane along the normal
        double distance = -cp_normal.dot(pos_world) + cp_offset;
        // move connector
        connector.pos += distance * cp_normal;
        update_connector_node(id);
    }
}

void CutGizmo::update_clipper_presenter(bool force_reset_ignored)
{
    if (!is_planar_mode() || m_ignore_dialog_events)
        return;

    auto rotate_vec3d_around_plane_center = [&](Vec3d& vec) -> void
    {
        vec = Transformation(
                  translation_transform(m_plane_center)
                  * context().state.rotation_m
                  * translation_transform(-m_plane_center)
              )
                  .get_matrix()
            * vec;
    };

    // calculate normal and offset for clipping plane
    Vec3d beg = m_bb_center;
    beg[Z] -= m_radius;
    rotate_vec3d_around_plane_center(beg);

    Vec3d normal  = m_cut_normal;
    double offset = normal.dot(m_plane_center);
    double dist   = normal.dot(beg);

    if (!m_is_looking_forward_on_cut_plane) {
        // recalculate normal and offset for clipping plane, if camera is looking downward to cut plane
        normal = context().state.rotation_m * (-1. * Vec3d::UnitZ());
        normal.normalize();

        beg = m_bb_center;
        beg[Z] += m_radius;
        rotate_vec3d_around_plane_center(beg);

        offset = normal.dot(m_plane_center);
        dist   = normal.dot(beg);
    }

    m_clp_normal = normal;
    m_clipper_presenter.update_clipper(m_clp_normal, offset, dist, force_reset_ignored);

    put_connectors_on_cut_plane(m_clp_normal, offset);

    check_and_update_connectors_state();
}

void CutGizmo::init_scene_nodes()
{
    ProjectContext& ctxt = context();
    if (ctxt.main_node) {
        ASSERT(ctxt.handles_node, ctxt.plane_node, ctxt.connectors_node, ctxt.cut_line_node);

        m_main_node       = ctxt.main_node;
        m_handles_node    = ctxt.handles_node;
        m_plane_node      = ctxt.plane_node;
        m_cut_line_node   = ctxt.cut_line_node;
        m_connectors_node = ctxt.connectors_node;
        return;
    }

    Scene::Scene& scene = m_scene_presenter.scene();

    Scene::NodeBuilder builder{scene};
    builder.set_debug_name("cut_main");
    builder.set_tag(CutNodeTag());

    builder.child([&](Scene::NodeBuilder& bldr) { build_cut_plane_node(bldr); });
    builder.child([&](Scene::NodeBuilder& bldr) { build_handles_nodes(bldr); });
    builder.child([&](Scene::NodeBuilder& bldr) { build_cut_line_node(bldr); });

    scene.add_child(builder.build().release(), &scene.root());
    m_main_node = ctxt.main_node = scene.root().children().back().get();
    m_handles_node = ctxt.handles_node = m_main_node->query_first(
        [](const Scene::Node* n) -> bool
        {
            const CutHandleNodeTag* tag = n->tag_of_type<CutHandleNodeTag>();
            return tag != nullptr && tag->type == CutHandleNodeTag::Type::Undef;
        },
        true
    );

    m_plane_node = ctxt.plane_node = m_main_node->query_first(
        [](const Scene::Node* n) -> bool { return n->tag_of_type<CutPlaneNodeTag>() != nullptr; },
        true
    );

    m_cut_line_node = ctxt.cut_line_node = m_main_node->query_first(
        [](const Scene::Node* n) -> bool { return n->tag_of_type<CutLineNodeTag>() != nullptr; },
        true
    );

    Scene::NodeBuilder connectors_builder{scene};
    connectors_builder.set_debug_name("cut_connectors");
    connectors_builder.set_tag(CutConnectorNodeTag());

    scene.add_child(connectors_builder.build().release(), &scene.root());
    m_connectors_node = ctxt.connectors_node = scene.root().children().back().get();
}

Domain::Transform3d CutGizmo::get_cut_matrix()
{
    if (!context().selected_instance)
        return Domain::Transform3d::Identity();

    // m_cut_z is the distance from the bed. Subtract possible SLA elevation.
    const double sla_shift_z = 0.; // selection.get_first_volume()->get_sla_shift_z();

    const Domain::Vec3d instance_offset = context().selected_instance->get_offset();
    Domain::Vec3d cut_center_offset     = m_plane_center - instance_offset;
    cut_center_offset.z() -= sla_shift_z;

    return Domain::translation_transform(cut_center_offset) * context().state.rotation_m;
}

void CutGizmo::flip_cut_plane()
{
    context().state.rotation_m =
        context().state.rotation_m * rotation_transform(PI * Vec3d::UnitX());

    m_start_dragging_m = context().state.rotation_m;

    update_cut_normal();

    if (is_planar_mode()) {
        m_part_selection.turn_over_selection();
        update_parts_nodes_colors_from_selection();
        m_is_looking_forward_on_cut_plane =
            m_scene_presenter.scene().camera().forward().dot(m_cut_normal) < 0.05;
        update_clipper_presenter(false);
    } else {
        update_cut_plane_mesh();
    }
    update_parts_nodes_enabled();

    if (!m_dialog->connectors_editing) {
        update_cut_plane_trafo();
    }

    take_snapshot(UndoSnapshotType::CutFlipPlane);
}

void CutGizmo::update_cut_normal()
{
    Vec3d normal = context().state.rotation_m * Vec3d::UnitZ();
    normal.normalize();
    m_cut_normal = normal;
}

bool CutGizmo::can_perform_cut() const
{
    if (!m_invalid_connectors_idxs.empty() || m_dialog->connectors_editing)
        return false;

    return is_planar_mode() ? m_clipper_presenter.has_valid_contour() : is_valid_groove();

    if (!is_planar_mode())
        return is_valid_groove();

    return !m_part_selection.is_one_object() && m_clipper_presenter.has_valid_contour();

    return true;
}

bool CutGizmo::is_valid_groove() const
{
    if (is_planar_mode())
        return true;

    const float flaps_width =
        -2.f * context().state.groove.depth / tan(context().state.groove.flaps_angle);
    if (flaps_width > context().state.groove.width)
        return false;

    const Transform3d trafo = translation_transform(m_plane_center) * context().state.rotation_m;
    const Transform3d inst_trafo = context().selected_instance->get_matrix();

    for (size_t id = 0; id < m_groove_vertices.size(); id += 2) {
        const Vec3d beg = trafo * m_groove_vertices[id];
        const Vec3d end = trafo * m_groove_vertices[id + 1];

        bool intersection = false;
        for (const auto& solid_mesh : m_solid_meshes) {
            if ((intersection = solid_mesh.intersects_line(beg, end - beg, inst_trafo))) {
                break;
            }
        }
        if (!intersection)
            return false;
    }

    return true;
}

void CutGizmo::apply_connectors_in_model(ModelObject* mo, int& dowels_count)
{
    if (is_planar_mode()) {
        for (CutConnector& connector : mo->cut_connectors) {
            connector.rotation_m = context().state.rotation_m;

            if (connector.attribs.type == CutConnectorType::Dowel) {
                if (connector.attribs.style == CutConnectorStyle::Prism) {
                    connector.height *= 2;
                    // calculate shift of the connector center regarding to the position on the cut plane
                    connector.pos -= m_cut_normal * 0.5 * connector.height;
                }

                dowels_count++;
            }
        }
        apply_cut_connectors(mo, _u8L("Connector"));
    }
}

static indexed_triangle_set get_connector_mesh(
    CutConnectorAttributes connector_attributes,
    double snap_bulge_proportion,
    double snap_space_proportion
)
{
    indexed_triangle_set connector_mesh;

    int sectorCount{1};
    switch (CutConnectorShape(connector_attributes.shape)) {
    case CutConnectorShape::Triangle:
        sectorCount = 3;
        break;
    case CutConnectorShape::Square:
        sectorCount = 4;
        break;
    case CutConnectorShape::Circle:
        sectorCount = 360;
        break;
    case CutConnectorShape::Hexagon:
        sectorCount = 6;
        break;
    default:
        break;
    }

    if (connector_attributes.type == CutConnectorType::Snap)
        connector_mesh = Biz::Algorithms::TriangleMesh::its_make_snap(
            1.0,
            1.0,
            static_cast<float>(snap_space_proportion),
            static_cast<float>(snap_bulge_proportion)
        );
    else if (connector_attributes.style == CutConnectorStyle::Prism)
        connector_mesh =
            Biz::Algorithms::TriangleMesh::its_make_cylinder(1.0, 1.0, (2 * PI / sectorCount));
    else if (connector_attributes.type == CutConnectorType::Plug)
        connector_mesh =
            Biz::Algorithms::TriangleMesh::its_make_frustum(1.0, 1.0, (2 * PI / sectorCount));
    else
        connector_mesh =
            Biz::Algorithms::TriangleMesh::its_make_frustum_dowel(1.0, 1.0, sectorCount);

    return connector_mesh;
}

void CutGizmo::get_connector_geometry(
    const Domain::CutConnectorAttributes& connector_attributes,
    Scene::TriangleMesh** trimesh,
    Render::Geometry** geom
)
{
    ConnectorAuxiliaryElementId id{connector_attributes};

    (*trimesh) = m_connector_triangle_mesh_manager.get_or_create(
        id,
        [&]() -> std::unique_ptr<Scene::TriangleMesh>
        {
            indexed_triangle_set mesh_its = get_connector_mesh(
                connector_attributes,
                m_snap_bulge_proportion,
                m_snap_space_proportion
            );
            return std::make_unique<Scene::TriangleMesh>(std::move(mesh_its));
        }
    );

    (*geom) = m_connector_geometry_manager.get_or_create(
        id,
        [&]() { return Render::geometry_from_triangle_mesh(m_device, (*trimesh)->triangles()); }
    );
}

void CutGizmo::build_connector_node(const CutConnector& connector)
{
    SPDLOG_DEBUG("Cut element type:Connector");

    Scene::TriangleMesh* trimesh{nullptr};
    Render::Geometry* geom{nullptr};
    get_connector_geometry(connector.attribs, &trimesh, &geom);
    ASSERT(trimesh && geom);

    ColorRGBA color = CONNECTOR_DEF_COLOR;
    auto material   = Render::Material{}
                          .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
                          .set_uniform("uniform_color", color);

    double height   = connector.height;
    Vec3d pos_world = get_world_pos(connector.pos);
    if (connector.attribs.type == CutConnectorType::Dowel
        && connector.attribs.style == CutConnectorStyle::Prism)
    {
        height = 0.05f;
    }
    if (!m_is_looking_forward_on_cut_plane) {
        pos_world += 0.05 * m_clp_normal;
    }
    pos_world[Z] += 0.; // sla_shift;
    double xy_scale = connector.radius;

    const Transform3d scale_trafo = scale_transform(Vec3d(xy_scale, xy_scale, height));

    const Transform3d trafo = Domain::translation_transform(pos_world)
        * context().state.rotation_m
        * rotation_transform(-connector.z_angle * Vec3d::UnitZ())
        * scale_trafo;

    size_t connector_id = m_connectors_node->children().size();

    Scene::Scene& scene = m_scene_presenter.scene();
    Scene::NodeBuilder builder(scene);
    builder.set_debug_name("cut: connector:")
        .set_tag(
            CutConnectorNodeTag(connector_id, connector.attribs.type == CutConnectorType::Snap)
        )
        .set_mesh(geom, material, int(0))
        .transform([trafo](auto& xform) { xform = trafo; })
        .set_aabb(trimesh->aabb_mesh());

    scene.add_child(builder.build().release(), m_connectors_node);
}

void CutGizmo::update_connectors_nodes()
{
    if (!m_connectors_node)
        return;
    reset_connectors_nodes();

    // load all connectors form object
    for (const CutConnector& connector : context().selected_object->cut_connectors) {
        build_connector_node(connector);
    }
}

void CutGizmo::update_connectors_nodes_colors()
{
    const CutConnectors& connectors = context().selected_object->cut_connectors;
    // Update geometry for existed snap nodes
    for (const auto& node : m_connectors_node->children()) {
        CutConnectorNodeTag* tag = node.get()->tag_of_type<CutConnectorNodeTag>();

        const CutConnector& connector = connectors[tag->id];

        ColorRGBA color               = CONNECTOR_DEF_COLOR;
        const bool conflict_connector = std::binary_search(
            m_invalid_connectors_idxs.begin(),
            m_invalid_connectors_idxs.end(),
            tag->id
        );
        if (conflict_connector)
            color = CONNECTOR_ERR_COLOR;
        else // default connector color
            color = connector.attribs.type == CutConnectorType::Dowel ? DOWEL_COLOR : PLAG_COLOR;

        if (!m_dialog->connectors_editing)
            color = CONNECTOR_ERR_COLOR;
        else if (m_hovered_connector_id && m_hovered_connector_id.value() == tag->id)
            color = conflict_connector                            ? HOVERED_ERR_COLOR :
                connector.attribs.type == CutConnectorType::Dowel ? HOVERED_DOWEL_COLOR :
                                                                    HOVERED_PLAG_COLOR;
        else if (tag->is_selected)
            color = connector.attribs.type == CutConnectorType::Dowel ? SELECTED_DOWEL_COLOR :
                                                                        SELECTED_PLAG_COLOR;

        Render::Material material = node.get()->render_component()->material();
        material.set_uniform("uniform_color", color).set_transparent(color.is_transparent());
        node.get()->set_material_override(material);
    }
}

void CutGizmo::update_parts_nodes_colors_from_selection()
{
    bool invalid_color = m_dragging || !is_valid_groove();

    Scene::visit(
        *m_main_node,
        [&](Scene::Node& node)
        {
            CutPartNodeTag* tag = node.tag_of_type<CutPartNodeTag>();
            if (!tag)
                return;

            tag->type = m_part_selection.parts()[tag->id].selected ? CutPartNodeTag::Type::Upper :
                                                                     CutPartNodeTag::Type::Lower;

            ColorRGBA color           = invalid_color    ? DEF_PART_COLOR :
                tag->type == CutPartNodeTag::Type::Upper ? UPPER_PART_COLOR :
                                                           LOWER_PART_COLOR;
            Render::Material material = node.render_component()->material();
            material.set_uniform("uniform_color", color).set_transparent(color.is_transparent());
            node.set_material_override(material);
        },
        true
    );
}

void CutGizmo::update_parts_nodes_enabled()
{
    const bool connectors_editing     = m_dialog->connectors_editing;
    CutPartNodeTag::Type enabled_type = m_is_looking_forward_on_cut_plane ?
        CutPartNodeTag::Type::Lower :
        CutPartNodeTag::Type::Upper;

    Scene::visit(
        *m_main_node,
        [&](Scene::Node& node)
        {
            if (CutPartNodeTag* tag = node.tag_of_type<CutPartNodeTag>()) {
                node.set_enabled(!connectors_editing || tag->type == enabled_type);
            }
        },
        true
    );
}

void CutGizmo::update_connector_node(size_t id, bool force_geometry_update)
{
    const CutConnector& connector = context().selected_object->cut_connectors[id];
    double height                 = connector.height;
    Domain::Vec3d pos_world       = get_world_pos(connector.pos);

    Scene::Node* connector_node = m_connectors_node->children()[id].get();

    if (force_geometry_update) {
        Scene::TriangleMesh* trimesh{nullptr};
        Render::Geometry* geom{nullptr};
        get_connector_geometry(connector.attribs, &trimesh, &geom);
        ASSERT(trimesh && geom);

        // Update geometry for non-snap node
        static_cast<Scene::MeshRenderNodeComponent*>(connector_node->render_component())
            ->set_geometry(geom);
        connector_node->set_raycast_component(
            new Scene::AabbRaycastNodeComponent(&trimesh->aabb_mesh())
        );

        connector_node->tag_of_type<CutConnectorNodeTag>()->is_snap =
            connector.attribs.type == CutConnectorType::Snap;
    }

    if (connector.attribs.type == CutConnectorType::Dowel
        && connector.attribs.style == CutConnectorStyle::Prism)
    {
        height = 0.05f;
    }
    if (!m_is_looking_forward_on_cut_plane)
        pos_world += 0.05 * m_clp_normal;
    pos_world[Z] += 0.; // sla_shift;
    double xy_scale = connector.radius;

    const Transform3d scale_trafo = scale_transform(Vec3d(xy_scale, xy_scale, height));

    const Transform3d trafo = Domain::translation_transform(pos_world)
        * context().state.rotation_m
        * rotation_transform(-connector.z_angle * Vec3d::UnitZ())
        * scale_trafo;

    connector_node->set_local_transform(trafo);
}

void CutGizmo::update_snap_nodes()
{
    bool recreate_snaps{false};

    if (!Domain::fuzzy_compare(m_snap_bulge_proportion, snap_bulge_proportion())) {
        m_snap_bulge_proportion = snap_bulge_proportion();
        recreate_snaps          = true;
    }
    if (!Domain::fuzzy_compare(m_snap_space_proportion, snap_space_proportion())) {
        m_snap_space_proportion = snap_space_proportion();
        recreate_snaps          = true;
    }

    if (!recreate_snaps)
        return;

    CutConnectorAttributes attribs(
        CutConnectorType::Snap,
        CutConnectorStyle::Undef,
        CutConnectorShape::Undef
    );

    ConnectorAuxiliaryElementId id{attribs};

    // For snap connectors mesh have to be recreated, so remove it from connector managers
    m_connector_triangle_mesh_manager.release(id);
    m_connector_geometry_manager.release(id);

    Scene::TriangleMesh* trimesh = m_connector_triangle_mesh_manager.get_or_create(
        id,
        [&]() -> std::unique_ptr<Scene::TriangleMesh>
        {
            indexed_triangle_set mesh_its =
                get_connector_mesh(attribs, m_snap_bulge_proportion, m_snap_space_proportion);
            return std::make_unique<Scene::TriangleMesh>(std::move(mesh_its));
        }
    );

    Render::Geometry* geom = m_connector_geometry_manager.get_or_create(
        id,
        [&]() { return Render::geometry_from_triangle_mesh(m_device, trimesh->triangles()); }
    );

    // Update geometry for existed snap nodes
    Scene::visit(
        *m_connectors_node,
        [&](Scene::Node& n)
        {
            if (n.tag_of_type<CutConnectorNodeTag>()->is_snap) {
                static_cast<Scene::MeshRenderNodeComponent*>(n.render_component())
                    ->set_geometry(geom);
                n.set_raycast_component(new Scene::AabbRaycastNodeComponent(&trimesh->aabb_mesh()));
            }
        }
    );
}

Vec3d CutGizmo::get_local_pos(Domain::Vec3d pos_world)
{
    const float sla_shift = 0.; // m_c->selection_info()->get_sla_shift();
    Vec3d inst_offset     = context().selected_instance->get_transformation().get_offset();

    // recalculate hit to object's local position
    Vec3d pos = pos_world;
    pos -= inst_offset;
    pos[Z] -= sla_shift;

    return pos;
}

Vec3d CutGizmo::get_world_pos(Domain::Vec3d pos)
{
    const float sla_shift = 0.; // m_c->selection_info()->get_sla_shift();
    Vec3d inst_offset     = context().selected_instance->get_transformation().get_offset();

    // recalculate hit to object's local position
    pos += inst_offset;
    pos[Z] += sla_shift;

    return pos;
}

void CutGizmo::update_dialog_on_selection_changed()
{
    bool is_first_selected{true};

    std::optional<double> radius;
    std::optional<double> height;
    std::optional<double> radius_tolerance;
    std::optional<double> height_tolerance;
    std::optional<double> z_angle;

    CutConnectorAttributes attribs;

    const auto& children = m_connectors_node->children();
    for (size_t id = 0; id < children.size(); id++) {
        if (children[id].get()->tag_of_type<CutConnectorNodeTag>()->is_selected) {
            CutConnector& connector = context().selected_object->cut_connectors[id];

            if (is_first_selected) {
                radius           = connector.radius;
                height           = connector.height;
                radius_tolerance = connector.radius_tolerance;
                height_tolerance = connector.height_tolerance;
                z_angle          = connector.z_angle;
                attribs          = connector.attribs;

                is_first_selected = false;
            } else {
                if (radius && radius.value() != connector.radius)
                    radius = std::nullopt;
                if (height && height.value() != connector.height)
                    height = std::nullopt;
                if (radius_tolerance && radius_tolerance.value() != connector.radius_tolerance)
                    radius_tolerance = std::nullopt;
                if (height_tolerance && height_tolerance.value() != connector.height_tolerance)
                    height_tolerance = std::nullopt;
                if (z_angle && z_angle.value() != connector.z_angle)
                    z_angle = std::nullopt;

                if (attribs.type != connector.attribs.type)
                    attribs.type = CutConnectorType::Undef;
                if (attribs.style != connector.attribs.style)
                    attribs.style = CutConnectorStyle::Undef;
                if (attribs.shape != connector.attribs.shape)
                    attribs.shape = CutConnectorShape::Undef;
            }
        }
    }

    // set values in dialog from tmp
    m_dialog->set_connector_type(attribs.type);
    m_dialog->set_connector_style(attribs.style);
    m_dialog->set_connector_shape(attribs.shape);
    m_dialog->set_connector_values(height, height_tolerance, radius, radius_tolerance, z_angle);
}

void CutGizmo::update_dialog_state()
{
    bool is_planar = is_planar_mode();
    m_dialog->update_state(
        CutDialog::OutState{
            .connectors_outside_cut_contour = is_planar ? m_info_stats.outside_cut_contour : 0,
            .connectors_outside_object      = is_planar ? m_info_stats.outside_bb : 0,
            .connectors_overlap             = is_planar ? m_info_stats.is_overlap : false,
            .plane_outside_object = is_planar ? !m_clipper_presenter.has_valid_contour() : false,
            .invalid_groove       = is_planar ? false : !is_valid_groove(),
            .has_connectors       = !context().selected_object->cut_connectors.empty()
        }
    );
}

void CutGizmo::apply_cut_connectors(ModelObject* mo, const std::string& connector_name)
{
    if (mo->cut_connectors.empty())
        return;

    using namespace Biz::Algorithms::Geometry;

    size_t connector_id = mo->cut_id.connectors_cnt();
    for (const CutConnector& connector : mo->cut_connectors) {
        Domain::TriangleMesh mesh = Domain::TriangleMesh(
            get_connector_mesh(connector.attribs, m_snap_space_proportion, m_snap_bulge_proportion)
        );
        ModelVolume* new_volume = Biz::Algorithms::ModelObject::add_volume(
            mo,
            std::move(mesh),
            ModelVolumeType::NEGATIVE_VOLUME
        );

        // Transform the new modifier to be aligned inside the instance
        new_volume->set_transformation(
            translation_transform(connector.pos)
            * connector.rotation_m
            * rotation_transform(-connector.z_angle * Vec3d::UnitZ())
            * scale_transform(Vec3d(connector.radius, connector.radius, connector.height))
        );

        new_volume->cut_info =
            {connector.attribs.type, connector.radius_tolerance, connector.height_tolerance};
        new_volume->name = connector_name + "-" + std::to_string(++connector_id);
    }
    mo->cut_id.increase_connectors_cnt(mo->cut_connectors.size());

    // delete all connectors
    mo->cut_connectors.clear();
}

static void update_object_cut_id(
    CutId& cut_id,
    Biz::ModelObjectCutAttributes attributes,
    const int dowels_count
)
{
    // we don't save cut information, if result will not contains all parts of initial object
    if (!attributes.keep_upper || !attributes.keep_lower || attributes.invalidate_cut_info)
        return;

    if (!cut_id.valid())
        cut_id.init();
    // increase check sum, if it's needed
    {
        int cut_obj_cnt = -1;
        if (attributes.keep_upper)
            cut_obj_cnt++;
        if (attributes.keep_lower)
            cut_obj_cnt++;
        if (attributes.create_dowels)
            cut_obj_cnt += dowels_count;
        if (cut_obj_cnt > 0)
            cut_id.increase_check_sum(size_t(cut_obj_cnt));
    }
}

static void check_objects_after_cut(const ModelObjectPtrs& objects)
{
    std::vector<std::string> err_objects_names;
    std::vector<int> err_objects_idxs;
    int obj_idx{0};
    for (const ModelObject* object : objects) {
        std::vector<std::string> connectors_names;
        connectors_names.reserve(object->volumes.size());
        for (const ModelVolume* vol : object->volumes)
            if (vol->cut_info.is_connector)
                connectors_names.push_back(vol->name);
        const size_t connectors_count = connectors_names.size();

        // remove duplicates
        std::sort(connectors_names.begin(), connectors_names.end());
        connectors_names.erase(
            std::unique(connectors_names.begin(), connectors_names.end()),
            connectors_names.end()
        );

        if (connectors_count != connectors_names.size())
            err_objects_names.push_back(object->name);

        // check manifold/repairs
        // auto stats = ModelProcessing::get_object_mesh_stats(object);
        // if (!stats.manifold() || stats.repaired())
        // err_objects_idxs.push_back(obj_idx);
        obj_idx++;
    }
    /*
        if (!err_objects_names.empty()) {
            wxString names = from_u8(err_objects_names[0]);
            for (size_t i = 1; i < err_objects_names.size(); i++)
                names += ", " + from_u8(err_objects_names[i]);
            WarningDialog(plater, format_wxstr("Objects(%1%) have duplicated connectors. "
                "Some connectors may be missing in slicing result.\n"
                "Please report to PrusaSlicer team in which scenario this issue happened.\n"
                "Thank you.", names)).ShowModal();
        }

        if (is_windows10() && !err_objects_idxs.empty()) {
            auto dlg = WarningDialog(plater, _L("Open edges or errors were detected after the cut.\n"
                "Do you want to fix them by Windows repair algorithm?"),
                _L("Errors detected after cut operation"), wxYES_NO);
            if (dlg.ShowModal() == wxID_YES) {
                //          model_name
                std::vector<std::string>                           succes_models;
                //                    model_name   failing reason
                std::vector<std::pair<std::string, std::string>>   failed_models;

                std::vector<std::string> model_names;

                for (int obj_idx : err_objects_idxs)
                    model_names.push_back(objects[obj_idx]->name);

                auto fix_and_update_progress = [model_names, &objects](const int obj_idx, int model_idx,
                    wxProgressDialog& progress_dlg,
                    std::vector<std::string>& succes_models,
                    std::vector<std::pair<std::string, std::string>>& failed_models) -> bool
                    {
                        const std::string& model_name = model_names[model_idx];
                        wxString msg;
                        if (model_names.size() == 1)
                            msg = GUI::format(_L("Repairing object %1%"), model_name) + "\n";
                        else {
                            // TRN: This is followed by a list of object which are to be repaired.
                            msg = _L("Repairing objects:") + "\n";
                            for (int i = 0; i < int(model_names.size()); ++i)
                                msg += (i == model_idx ? " > " : "   ") + from_u8(model_names[i]) + "\n";
                            msg += "\n";
                        }

                        std::string res;
                        if (!fix_model_by_win10_sdk_gui(*objects[obj_idx], -1, progress_dlg, msg, res))
                            return false;

                        if (res.empty())
                            succes_models.push_back(model_name);
                        else
                            failed_models.push_back({ model_name, res });
                        return true;
                    };

                // Open a progress dialog.
                // TRN: This shows in a progress dialog while the operation is in progress.
                wxProgressDialog progress_dlg(_L("Fixing by Windows repair algorithm"), "", 100, find_toplevel_parent(plater),
                    wxPD_AUTO_HIDE | wxPD_APP_MODAL | wxPD_CAN_ABORT);
                int model_idx{ 0 };
                for (int obj_idx : err_objects_idxs) {
                    if (!fix_and_update_progress(obj_idx, model_idx, progress_dlg, succes_models, failed_models))
                        break;
                    model_idx++;
                }

                // Close the progress dialog
                progress_dlg.Update(100, "");

                // Show info dialog
                wxString msg = MenuFactory::get_repaire_result_message(succes_models, failed_models);
                // TRN: Title of a dialog informing the user about the result of the model repair operation.
                InfoDialog(plater, _L("Repair operation finished"), msg).ShowModal();
            }
        }
        */

    // Put all instances on bed
    for (auto* object : objects) {
        for (auto instance : object->instances) {
            place_on_bed(instance);
        }
    }
}

void synchronize_model_after_cut(Model& model, const CutId& cut_id)
{
    for (ModelObject* obj : model.objects)
        if (obj->is_cut() && obj->cut_id.has_same_id(cut_id) && !obj->cut_id.is_equal(cut_id))
            obj->cut_id = cut_id;
}

static std::vector<size_t> get_unique_object_ids(const ElementRefs& instance_refs)
{
    auto object_ids =
        instance_refs | std::views::transform([](const auto& ref) { return ref.object_id; });
    std::vector<size_t> unique_object_ids(object_ids.begin(), object_ids.end());
    std::ranges::sort(unique_object_ids);
    auto [first, last] = std::ranges::unique(unique_object_ids);
    unique_object_ids.erase(first, last);
    return unique_object_ids;
}

void CutGizmo::perform_cut()
{
    if (!can_perform_cut() || !context().selected_object)
        return;

    // deactivate CutGizmo and then perform a cut
    if (m_controller) {
        m_controller->deactivate_current_tool();
    }

    reset_connectors_nodes();

    // perform cut
    {
        // This shall delete the part selection class and deallocate the memory.
        ScopeGuard part_selection_killer([this]() { m_part_selection = CutPartSelection(); });

        const bool cut_with_groove = !is_planar_mode();

        const bool cut_by_contour = m_clipper_presenter.has_ignored();

        Domain::ModelObject* cut_mo;
        if (cut_by_contour) {
            m_part_selection.model_object()->cut_connectors =
                context().selected_object->cut_connectors;
            cut_mo = m_part_selection.model_object();
        } else
            cut_mo = context().selected_object;

        int dowels_count          = 0;
        const bool has_connectors = !context().selected_object->cut_connectors.empty();
        // update connectors pos as offset of its center before cut performing
        apply_connectors_in_model(cut_mo, dowels_count);

        // ys_FIXME:: set wxBusyCursor ;

        Biz::ModelObjectCutAttributes attributes = {
            .keep_upper          = has_connectors ? true : keep_upper(),
            .keep_lower          = has_connectors ? true : keep_lower(),
            .keep_as_parts       = has_connectors ? false : keep_as_parts(),
            .flip_upper          = flip_upper(),
            .flip_lower          = flip_lower(),
            .place_on_cut_upper  = place_on_cut_upper(),
            .place_on_cut_lower  = place_on_cut_lower(),
            .create_dowels       = dowels_count > 0,
            .invalidate_cut_info = !has_connectors && !cut_with_groove && !cut_mo->cut_id.valid()
        };

        // update cut_id for the cut object in respect to the attributes
        update_object_cut_id(cut_mo->cut_id, attributes, dowels_count);

        Biz::Cut cut(cut_mo, context().instance_idx, get_cut_matrix(), attributes);
        const ModelObjectPtrs& new_objects = cut_by_contour ?
            cut.perform_by_contour(m_part_selection.get_cut_parts(), dowels_count) :
            cut_with_groove ?
            cut.perform_with_groove(context().state.groove, context().state.rotation_m) :
            cut.perform_with_plane();

        check_objects_after_cut(new_objects);
        // save cut_id to post update synchronization
        const CutId cut_id = cut_mo->cut_id;

        // Update cut results in the model:
        // 1. Remove the old object
        m_project_interactor->scene_interactor().delete_object(context().selected_object);
        // 2. Add new objects.
        const ElementRefs instance_refs{
            m_project_interactor->scene_interactor().add_new_objects(new_objects)
        };
        // Note: After that, process only ModelInstances, which will be obtained with ids from instance_refs

        // arrange result objects
        Biz::Arrange::Settings settings;
        settings.scaled_offset = Biz::Algorithms::Scaling::scaled(3.0);

        const Biz::Scene::BedSelection selection{
            m_project_interactor->scene_interactor().bed_selection()
        };
        ASSERT(!selection.empty());
        const BedRef& bed_ref{selection.selected_beds().front()};
        const Domain::BedInstance* bed_instance{
            m_project_interactor->selected_project().find_bed_instance_by_id(bed_ref.instance_id)
        };
        ASSERT(bed_instance);

        std::vector<size_t> unique_object_ids = get_unique_object_ids(instance_refs);
        std::vector<BedToArrange> beds_to_arrange;
        std::size_t inst_cnt{};
        for (std::size_t object_id : unique_object_ids) {
            const ModelObject* object{
                m_project_interactor->selected_project().find_object_by_id(object_id)
            };
            ASSERT(object);
            if (beds_to_arrange.empty()) {
                inst_cnt = object->instances.size();
                beds_to_arrange.reserve(inst_cnt);
                for (const ModelInstance* instance : object->instances) {
                    beds_to_arrange.emplace_back(
                        BedToArrange{bed_ref, bed_instance->index(), {}, {instance}, true}
                    );
                }
            } else {
                ASSERT(object->instances.size() == inst_cnt);
                for (std::size_t inst_idx{}; inst_idx < inst_cnt; inst_idx++) {
                    beds_to_arrange.at(inst_idx).arrangeable.emplace_back(
                        object->instances.at(inst_idx)
                    );
                }
            }
        }

        m_project_interactor->arrange_interactor().arrange(
            m_project_interactor->selected_project_id(),
            beds_to_arrange,
            std::nullopt,
            {},
            settings,
            [this]() { take_snapshot(Biz::UndoSnapshotType::Cut); });
        synchronize_model_after_cut(m_project_interactor->selected_project().model(), cut_id);
    }

    context().invalidate();
}

void CutGizmo::reset_preprocess_cut()
{
    m_part_selection = CutPartSelection();

    if (!is_planar_mode()) {
        if (m_dragging || m_groove_editing || !is_valid_groove()) {
            update_parts_nodes_colors_from_selection();
            return;
        }
        preprocess_cut();
    }
}

void CutGizmo::preprocess_cut()
{
    if (!context().selected_instance || m_dragging || m_ignore_dialog_events)
        return;

    reset_cut_part_meshes();

    //! wxBusyCursor wait;

    if (is_planar_mode()) {
        reset_preprocess_cut();
        m_part_selection = CutPartSelection(
            context().selected_object,
            get_cut_matrix(),
            context().instance_idx,
            m_plane_center,
            m_cut_normal,
            &m_clipper_presenter
        );
    } else {
        if (is_valid_groove()) {
            Biz::Cut cut(context().selected_object, context().instance_idx, get_cut_matrix());
            const ModelObjectPtrs& new_objects =
                cut.perform_with_groove(context().state.groove, context().state.rotation_m, true);
            if (!new_objects.empty())
                m_part_selection = CutPartSelection(new_objects.front(), context().instance_idx);
        } else {
            // reset cut parts with default object volums
            m_part_selection = CutPartSelection(context().selected_object, context().instance_idx);
        }
    }
    Scene::Scene& scene = m_scene_presenter.scene();
    size_t part_id{0};
    for (const auto& part : m_part_selection.parts()) {
        Scene::NodeBuilder builder(scene);
        build_cut_part_mesh(
            part.selected ? CutPartNodeTag::Type::Upper : CutPartNodeTag::Type::Lower,
            part_id++,
            part.mesh,
            part.trafo,
            builder
        );
        scene.add_child(builder.build().release(), m_main_node);
    }

    if (!is_planar_mode() && !is_valid_groove()) {
        update_parts_nodes_colors_from_selection();
    }
}

bool CutGizmo::keep_upper() const
{
    return context().state.part_state_upper.keep;
}

bool CutGizmo::keep_lower() const
{
    return context().state.part_state_lower.keep;
}

bool CutGizmo::place_on_cut_upper() const
{
    return context().state.part_state_upper.place_on_cut;
}

bool CutGizmo::place_on_cut_lower() const
{
    return context().state.part_state_lower.place_on_cut;
}

bool CutGizmo::flip_upper() const
{
    return context().state.part_state_upper.flip;
}

bool CutGizmo::flip_lower() const
{
    return context().state.part_state_lower.flip;
}

bool CutGizmo::is_planar_mode() const
{
    return m_dialog->is_planar_cut_mode;
}

bool CutGizmo::keep_as_parts() const
{
    return m_dialog->keep_as_parts;
}

bool CutGizmo::add_connector(Domain::Vec3d pos_world)
{
    if (!m_dialog->connectors_editing)
        return false;

    unselect_all_connectors();

    CutConnectors& connectors = context().selected_object->cut_connectors;
    connectors.emplace_back(
        get_local_pos(pos_world),
        context().state.rotation_m,
        connector_size() * 0.5f,
        connector_depth(),
        connector_size_tolerance() * 0.5f,
        connector_depth_tolerance(),
        connector_angle(),
        connector_attributes()
    );

    build_connector_node(connectors.back());

    check_and_update_connectors_state();
    update_dialog_on_selection_changed();

    take_snapshot(UndoSnapshotType::CutAddConnector);

    return true;
}

bool CutGizmo::remove_selected_connectors()
{
    CutConnectors& connectors = context().selected_object->cut_connectors;
    if (connectors.empty())
        return false;

    const Scene::Node::NodeOwningList& connectors_nodes = m_connectors_node->children();
    ASSERT(connectors.size() == connectors_nodes.size());

    Scene::Scene& scene = m_scene_presenter.scene();

    for (int i = int(connectors.size()) - 1; i >= 0; i--) {
        Scene::Node* node = connectors_nodes[i].get();
        if (node->tag_of_type<CutConnectorNodeTag>()->is_selected) {
            // remove connector from the object
            connectors.erase(connectors.begin() + i);
            // remove connector from connector_nodes
            scene.remove_child(node);
        }
    }

    ASSERT(connectors.size() == m_connectors_node->children().size());

    // update ids for connector nodes

    size_t id = 0;
    for (const auto& node : m_connectors_node->children()) {
        node.get()->tag_of_type<CutConnectorNodeTag>()->id = id++;
    }
    check_and_update_connectors_state();

    take_snapshot(UndoSnapshotType::CutRemoveConnector);

    return true;
}

void CutGizmo::select_hovered_connector(bool force_unique_selection)
{
    ASSERT(
        m_hovered_connector_id
        && m_connectors_node->children().size() > m_hovered_connector_id.value()
    );

    if (force_unique_selection) {
        // unselect all nodes except of hovered one
        for (const auto& node : m_connectors_node->children()) {
            CutConnectorNodeTag* tag = node.get()->tag_of_type<CutConnectorNodeTag>();
            tag->is_selected         = tag->id == m_hovered_connector_id.value();
        }
    } else {
        // just select hovered node
        const auto& node = m_connectors_node->children()[m_hovered_connector_id.value()];
        node.get()->tag_of_type<CutConnectorNodeTag>()->is_selected = true;
    }
}

void CutGizmo::unselect_hovered_connector()
{
    ASSERT(
        m_hovered_connector_id
        && m_connectors_node->children().size() > m_hovered_connector_id.value()
    );

    // just unselect hovered node

    const auto& node = m_connectors_node->children()[m_hovered_connector_id.value()];
    node.get()->tag_of_type<CutConnectorNodeTag>()->is_selected = false;
}

void CutGizmo::unselect_all_connectors()
{
    for (const auto& node : m_connectors_node->children()) {
        node.get()->tag_of_type<CutConnectorNodeTag>()->is_selected = false;
    }
}

void CutGizmo::select_all_connectors()
{
    for (const auto& node : m_connectors_node->children()) {
        node.get()->tag_of_type<CutConnectorNodeTag>()->is_selected = true;
    }
}

void CutGizmo::update_selected_connectors(bool force_geometry_update)
{
    const auto& children = m_connectors_node->children();
    for (size_t id = 0; id < children.size(); id++) {
        if (children[id].get()->tag_of_type<CutConnectorNodeTag>()->is_selected) {
            CutConnector& connector    = context().selected_object->cut_connectors[id];
            connector.radius           = connector_size() * 0.5f;
            connector.height           = connector_depth();
            connector.radius_tolerance = connector_size_tolerance() * 0.5f;
            connector.height_tolerance = connector_depth_tolerance();
            connector.z_angle          = connector_angle();
            connector.attribs          = connector_attributes();

            update_connector_node(id, force_geometry_update);
        }
    }
    check_and_update_connectors_state();
}

void CutGizmo::reset_connectors()
{
    context().selected_object->cut_connectors.clear();
    update_connectors_nodes();
    check_and_update_connectors_state();
}

bool CutGizmo::is_outside_of_cut_contour(
    size_t idx,
    const CutConnectors& connectors,
    const Vec3d cur_pos
)
{
    // check if connector pos is out of clipping plane
    if (m_clipper_presenter.is_outside_of_cut_contour(cur_pos)) {
        m_info_stats.outside_cut_contour++;
        return true;
    }

    // check if connector bottom contour is out of clipping plane
    const CutConnector& cur_connector = connectors[idx];
    const CutConnectorShape shape     = CutConnectorShape(cur_connector.attribs.shape);
    const int   sectorCount = shape == CutConnectorShape::Triangle ? 3 :
        shape == CutConnectorShape::Square ? 4 :
        shape == CutConnectorShape::Circle ? 60 : // supposably, 60 points are enough for conflict detection
        shape == CutConnectorShape::Hexagon ? 6 :
        60; // supposably, 60 points are enough for conflict detection for snap connectors

    indexed_triangle_set mesh;
    auto& vertices = mesh.vertices;
    vertices.reserve(sectorCount + 1);

    float fa = 2 * PI / sectorCount;
    auto vec = Eigen::Vector2f(0, cur_connector.radius);
    for (float angle = 0; angle < 2.f * PI; angle += fa) {
        Vec2f p = Eigen::Rotation2Df(angle) * vec;
        vertices.emplace_back(Vec3f(p(0), p(1), 0.f));
    }
    its_transform(mesh, translation_transform(cur_pos) * context().state.rotation_m);

    for (const Vec3f& vertex : vertices) {
        if (m_clipper_presenter.is_outside_of_cut_contour(vertex.cast<double>())) {
            m_info_stats.outside_cut_contour++;
            return true;
        }
    }

    return false;
}

bool CutGizmo::is_conflict_for_connector(
    size_t idx,
    const CutConnectors& connectors,
    const Vec3d cur_pos
)
{
    if (is_outside_of_cut_contour(idx, connectors, cur_pos))
        return true;

    const CutConnector& cur_connector = connectors[idx];

    const Transform3d matrix = translation_transform(cur_pos)
        * context().state.rotation_m
        * scale_transform(Vec3d(cur_connector.radius, cur_connector.radius, cur_connector.height));
    // get tbb from cur_connector.attribs mesh
    /*const*/ BoundingBoxf3 cur_tbb;
    cur_tbb = Algorithms::BoundingBox::transformed(cur_tbb, matrix);

    // check if connector's bounding box is inside the object's bounding box
    if (!context().state.bounding_box.contains(cur_tbb)) {
        m_info_stats.outside_bb++;
        return true;
    }

    // check if connectors are overlapping
    for (size_t i = 0; i < connectors.size(); ++i) {
        if (i == idx)
            continue;
        const CutConnector& connector = connectors[i];

        if ((connector.pos - cur_connector.pos).norm()
            < double(connector.radius + cur_connector.radius))
        {
            m_info_stats.is_overlap = true;
            return true;
        }
    }

    return false;
}

void CutGizmo::check_and_update_connectors_state()
{
    m_info_stats.invalidate();
    m_invalid_connectors_idxs.clear();
    if (!is_planar_mode())
        return;
    const CutConnectors& connectors = context().selected_object->cut_connectors;
    const Vec3d& instance_offset    = context().selected_instance->get_offset();
    const double sla_shift          = 0; // double(m_c->selection_info()->get_sla_shift());

    for (size_t i = 0; i < connectors.size(); ++i) {
        const CutConnector& connector = connectors[i];
        Vec3d pos                     = connector.pos
            + instance_offset
            + sla_shift * Vec3d::UnitZ(); // recalculate connector position to world position
        if (is_conflict_for_connector(i, connectors, pos))
            m_invalid_connectors_idxs.emplace_back(i);
    }
    update_connectors_nodes_colors();

    update_dialog_state();
}

CutConnectorAttributes CutGizmo::connector_attributes() const
{
    if (m_dialog->connector_type == CutConnectorType::Snap)
        return CutConnectorAttributes(
            CutConnectorType::Snap,
            CutConnectorStyle::Undef,
            CutConnectorShape::Undef
        );

    return CutConnectorAttributes(
        m_dialog->connector_type,
        m_dialog->connector_style,
        m_dialog->connector_shape
    );
}

double CutGizmo::connector_depth() const
{
    return m_dialog->connector_depth;
}

double CutGizmo::connector_size() const
{
    return m_dialog->connector_size;
}

double CutGizmo::connector_angle() const
{
    return deg2rad(m_dialog->connector_angle);
}

double CutGizmo::snap_bulge_proportion() const
{
    return 0.01 * m_dialog->snap_bulge_proportion;
}

double CutGizmo::snap_space_proportion() const
{
    return 0.01 * m_dialog->snap_space_proportion;
}

double CutGizmo::connector_depth_tolerance() const
{
    return m_dialog->connector_depth_tolerance;
}

double CutGizmo::connector_size_tolerance() const
{
    return m_dialog->connector_size_tolerance;
}

Scene::GizmoActivationState
CutGizmo::on_mouse_for_cut_line(Scene::GizmoEventContext& ctx, bool only_active)
{
    ASSERT(m_is_cut_line_processing);

    const auto event_type   = ctx.mouse_event().type();
    const auto event_button = ctx.mouse_event().button();

    bool shift_down =
        (ctx.mouse_event().key_modifiers() & Platform::KeyModifiers(Platform::KeyModifier::Shift))
        != 0;

    if (!shift_down) {
        m_is_cut_line_processing = false;
        return Scene::GizmoActivationState::Inactive;
    }

    m_line_end = ray_origin_on_near_z_plane(m_scene_presenter.scene().camera(), ctx.pick_ray());
    update_cut_line_node();

    Scene::Ray pick_ray = ctx.pick_ray();
    if (event_button == Platform::MouseButton::Left
        && event_type == Platform::MouseEvent::Type::ButtonUp)
    {
        Vec3d line_dir = m_line_end - m_line_beg;
        if (line_dir.norm() < 3.0) {
            m_is_cut_line_processing = false;
            return Scene::GizmoActivationState::Done;
        }

        Vec3d dir = pick_ray.direction;
        Vec3d pt  = m_line_end + dir; // Move the pt along dir so it is not clipped.

        Vec3d cross_dir = line_dir.cross(dir).normalized();
        Eigen::Quaterniond q;
        Transform3d m = Transform3d::Identity();
        m.matrix().block(0, 0, 3, 3) =
            q.setFromTwoVectors(Vec3d::UnitZ(), cross_dir).toRotationMatrix();

        const Vec3d new_plane_center = m_bb_center + cross_dir * cross_dir.dot(pt - m_bb_center);

        if (context().state.bounding_box.contains(new_plane_center)) {
            set_plane_center(new_plane_center);
            m_start_dragging_m = context().state.rotation_m = m;
            update_cut_plane_trafo();
            update_cut_plane_mesh();
            take_snapshot(UndoSnapshotType::CutPlaneMove);
        }

        m_is_cut_line_processing = false;
        update_cut_line_node();

        if (!is_planar_mode()) {
            m_groove_editing = false;
            reset_preprocess_cut();
        }
        return Scene::GizmoActivationState::Done;
    }

    return Scene::GizmoActivationState();
}

CutGizmo::ProjectContext& CutGizmo::context()
{
    return m_project_contexts->selected();
}

const CutGizmo::ProjectContext& CutGizmo::context() const
{
    return m_project_contexts->selected();
}

} // namespace Slic3r::App::Plater
