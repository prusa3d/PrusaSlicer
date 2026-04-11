///|/ Copyright (c) Prusa Research 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "GLGizmoFaceAlign.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/ImGuiPureWrap.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Selection.hpp"

#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"

#include <imgui/imgui.h>
#include <GL/glew.h>

#include <cassert>
#include <set>

namespace Slic3r {
namespace GUI {

namespace {

static const ColorRGBA PLANE_COLOR_A       = { 0.78f, 0.88f, 1.0f, 0.48f };
static const ColorRGBA PLANE_COLOR_B       = { 1.0f, 0.9f, 0.78f, 0.48f };
static const ColorRGBA HOVER_PLANE_COLOR_A = { 0.85f, 0.93f, 1.0f, 0.72f };
static const ColorRGBA HOVER_PLANE_COLOR_B = { 1.0f, 0.94f, 0.85f, 0.72f };

static bool get_two_selected_instances(const Selection& sel, std::pair<int, int>& out_a, std::pair<int, int>& out_b)
{
    std::set<std::pair<int, int>> insts;
    for (unsigned vid : sel.get_volume_idxs()) {
        const GLVolume* v = sel.get_volume(vid);
        if (v == nullptr || v->volume_idx() < 0 || v->is_wipe_tower() || v->disabled || !v->is_active)
            continue;
        insts.emplace(v->object_idx(), v->instance_idx());
    }
    if (insts.size() != 2)
        return false;
    auto it = insts.begin();
    out_a = *it++;
    out_b = *it;
    return true;
}

static Transform3d instance_raycast_matrix(GLCanvas3D& parent, int object_idx, int instance_idx)
{
    for (const GLVolume* v : parent.get_volumes().volumes) {
        if (v->object_idx() != object_idx || v->instance_idx() != instance_idx)
            continue;
        if (v->volume_idx() < 0 || v->is_wipe_tower())
            continue;
        return Geometry::translation_transform(v->get_sla_shift_z() * Vec3d::UnitZ()) * v->get_instance_transformation().get_matrix();
    }
    return Transform3d::Identity();
}

} // namespace

GLGizmoFaceAlign::GLGizmoFaceAlign(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{}

bool GLGizmoFaceAlign::on_init()
{
    return true;
}

std::string GLGizmoFaceAlign::on_get_name() const
{
    return _u8L("Align faces");
}

bool GLGizmoFaceAlign::on_is_activable() const
{
    std::pair<int, int> a, b;
    return get_two_selected_instances(m_parent.get_selection(), a, b);
}

CommonGizmosDataID GLGizmoFaceAlign::on_get_requirements() const
{
    return CommonGizmosDataID::SelectionInfo;
}

void GLGizmoFaceAlign::on_set_state()
{
    if (get_state() == Off) {
        clear_plane_geometry();
        reset_picks();
        m_cached_inst_a = { -1, -1 };
        m_cached_inst_b = { -1, -1 };
        m_vol_mats_a.clear();
        m_vol_mats_b.clear();
        m_inst_tf_a = Transform3d::Identity();
        m_inst_tf_b = Transform3d::Identity();
    }
}

void GLGizmoFaceAlign::reset_picks()
{
    m_step   = Step::PickSourceFace;
    m_source = FacePick{};
    m_target = FacePick{};
}

void GLGizmoFaceAlign::clear_plane_geometry()
{
    m_planes.clear();
    on_unregister_raycasters_for_picking();
}

void GLGizmoFaceAlign::update_planes(const std::pair<int, int>& ia, const std::pair<int, int>& ib)
{
    const Model* model = m_parent.get_model();
    if (model == nullptr)
        return;

    m_planes.clear();
    on_unregister_raycasters_for_picking();

    const ModelObject* mo_a = model->objects[ia.first];
    const ModelObject* mo_b = model->objects[ib.first];

    const Transform3d mesh_to_world_a = instance_raycast_matrix(m_parent, ia.first, ia.second);
    const Transform3d mesh_to_world_b = instance_raycast_matrix(m_parent, ib.first, ib.second);
    face_align_build_planes_for_instance(mo_a, ia.second, ia.first, mesh_to_world_a, m_planes, 127);
    face_align_build_planes_for_instance(mo_b, ib.second, ib.first, mesh_to_world_b, m_planes, 127);

    m_cached_inst_a = ia;
    m_cached_inst_b = ib;
    m_vol_mats_a.clear();
    for (const ModelVolume* v : mo_a->volumes)
        m_vol_mats_a.push_back(v->get_matrix());
    m_vol_mats_b.clear();
    for (const ModelVolume* v : mo_b->volumes)
        m_vol_mats_b.push_back(v->get_matrix());
    m_inst_tf_a = mo_a->instances[ia.second]->get_matrix_no_offset();
    m_inst_tf_b = mo_b->instances[ib.second]->get_matrix_no_offset();

    on_register_raycasters_for_picking();
}

bool GLGizmoFaceAlign::is_plane_update_necessary(const std::pair<int, int>& ia, const std::pair<int, int>& ib) const
{
    if (get_state() != On)
        return false;

    if (m_planes.empty())
        return true;

    if (ia != m_cached_inst_a || ib != m_cached_inst_b)
        return true;

    const Model* model = m_parent.get_model();
    if (model == nullptr)
        return true;

    const ModelObject* mo_a = model->objects[ia.first];
    const ModelObject* mo_b = model->objects[ib.first];

    if (mo_a->volumes.size() != m_vol_mats_a.size() || mo_b->volumes.size() != m_vol_mats_b.size())
        return true;

    for (size_t i = 0; i < mo_a->volumes.size(); ++i)
        if (!mo_a->volumes[i]->get_matrix().isApprox(m_vol_mats_a[i]))
            return true;
    for (size_t i = 0; i < mo_b->volumes.size(); ++i)
        if (!mo_b->volumes[i]->get_matrix().isApprox(m_vol_mats_b[i]))
            return true;

    if (!mo_a->instances[ia.second]->get_matrix_no_offset().isApprox(m_inst_tf_a) ||
        !mo_b->instances[ib.second]->get_matrix_no_offset().isApprox(m_inst_tf_b))
        return true;

    return false;
}

void GLGizmoFaceAlign::on_set_hover_id()
{
    if (m_hover_id >= 0 && size_t(m_hover_id) >= m_planes.size())
        m_hover_id = -1;
}

void GLGizmoFaceAlign::pick_from_hovered_plane(FacePick& out) const
{
    if (m_hover_id < 0 || size_t(m_hover_id) >= m_planes.size())
        return;
    const FaceAlignPlaneData& p = m_planes[m_hover_id];
    out.object_idx        = p.object_idx;
    out.instance_idx      = p.instance_idx;
    out.face_center_world = p.center_world;
    out.normal_world      = p.normal_world;
    out.valid             = true;
}

bool GLGizmoFaceAlign::compute_alignment_delta(const FacePick& src, const FacePick& tgt, Transform3d& out_delta) const
{
    if (!src.valid || !tgt.valid)
        return false;

    Vec3d n_s = src.normal_world.normalized();
    Vec3d n_t = tgt.normal_world.normalized();
    const Vec3d desired = -n_t;

    Eigen::Quaterniond q;
    q.setFromTwoVectors(n_s, desired);
    const Matrix3d R = q.toRotationMatrix();
    const Vec3d    c_s = src.face_center_world;
    const Vec3d    c_t = tgt.face_center_world;

    // Rotate about source face center so the source outward normal becomes -n_t (mate target face).
    Transform3d r_pivot = Transform3d::Identity();
    r_pivot.linear()        = R;
    r_pivot.translation()   = (Matrix3d::Identity() - R) * c_s;

    // Then translate so the source face center lands on the target face center (full vector, not only along n_t).
    Transform3d t_center = Transform3d::Identity();
    t_center.translate(c_t - c_s);

    out_delta = t_center * r_pivot;
    return true;
}

void GLGizmoFaceAlign::apply_alignment()
{
    if (!m_source.valid || !m_target.valid)
        return;
    if (m_source.object_idx == m_target.object_idx && m_source.instance_idx == m_target.instance_idx) {
        MessageDialog dlg(wxGetApp().plater(), _L("Pick a face on the other instance for the second click."), _L("Align faces"), wxOK | wxICON_WARNING);
        dlg.ShowModal();
        return;
    }

    Transform3d delta;
    if (!compute_alignment_delta(m_source, m_target, delta))
        return;

    wxGetApp().plater()->take_snapshot(_L("Align faces"));
    m_parent.transform_instance_world(m_source.object_idx, m_source.instance_idx, delta);
    reset_picks();
}

void GLGizmoFaceAlign::on_register_raycasters_for_picking()
{
    m_parent.set_raycaster_gizmos_on_top(true);

    assert(m_planes_casters.empty());

    const Transform3d mw_a = instance_raycast_matrix(m_parent, m_cached_inst_a.first, m_cached_inst_a.second);
    const Transform3d mw_b = instance_raycast_matrix(m_parent, m_cached_inst_b.first, m_cached_inst_b.second);

    for (int i = 0; i < (int)m_planes.size(); ++i) {
        const bool is_a = m_planes[i].object_idx == m_cached_inst_a.first && m_planes[i].instance_idx == m_cached_inst_a.second;
        const Transform3d& matrix = is_a ? mw_a : mw_b;
        m_planes_casters.emplace_back(
            m_parent.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, i, *m_planes[i].vbo.mesh_raycaster, matrix));
    }
}

void GLGizmoFaceAlign::on_unregister_raycasters_for_picking()
{
    m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo);
    m_parent.set_raycaster_gizmos_on_top(false);
    m_planes_casters.clear();
}

bool GLGizmoFaceAlign::on_mouse(const wxMouseEvent& mouse_event)
{
    if (mouse_event.LeftDown()) {
        if (m_hover_id >= 0 && size_t(m_hover_id) < m_planes.size()) {
            FacePick pick;
            pick_from_hovered_plane(pick);

            if (m_step == Step::PickSourceFace) {
                m_source = pick;
                m_step   = Step::PickTargetFace;
            } else {
                if (pick.object_idx == m_source.object_idx && pick.instance_idx == m_source.instance_idx) {
                    MessageDialog dlg(wxGetApp().plater(), _L("Pick a face on the other instance for the second click."), _L("Align faces"), wxOK | wxICON_WARNING);
                    dlg.ShowModal();
                    return true;
                }
                m_target = pick;
                apply_alignment();
            }
            m_parent.set_as_dirty();
            return true;
        }
        return false;
    }
    if (mouse_event.LeftUp())
        return m_hover_id != -1;

    return false;
}

void GLGizmoFaceAlign::data_changed([[maybe_unused]] bool is_serializing)
{
    std::pair<int, int> ia, ib;
    if (!get_two_selected_instances(m_parent.get_selection(), ia, ib)) {
        clear_plane_geometry();
        m_cached_inst_a = { -1, -1 };
        m_cached_inst_b = { -1, -1 };
        m_vol_mats_a.clear();
        m_vol_mats_b.clear();
        reset_picks();
        return;
    }

    if (ia != m_cached_inst_a || ib != m_cached_inst_b) {
        clear_plane_geometry();
        reset_picks();
    }
}

void GLGizmoFaceAlign::on_render()
{
    if (get_state() != On)
        return;

    std::pair<int, int> ia, ib;
    if (!get_two_selected_instances(m_parent.get_selection(), ia, ib)) {
        clear_plane_geometry();
        return;
    }

    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    shader->start_using();

    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glEnable(GL_BLEND));

    if (is_plane_update_necessary(ia, ib))
        update_planes(ia, ib);

    const Camera&     camera = wxGetApp().plater()->get_camera();
    const Transform3d mw_a   = instance_raycast_matrix(m_parent, ia.first, ia.second);
    const Transform3d mw_b   = instance_raycast_matrix(m_parent, ib.first, ib.second);

    for (int i = 0; i < (int)m_planes.size(); ++i) {
        const bool is_a = m_planes[i].object_idx == m_cached_inst_a.first && m_planes[i].instance_idx == m_cached_inst_a.second;
        const ColorRGBA base  = is_a ? PLANE_COLOR_A : PLANE_COLOR_B;
        const ColorRGBA hover = is_a ? HOVER_PLANE_COLOR_A : HOVER_PLANE_COLOR_B;
        m_planes[i].vbo.model.set_color(i == m_hover_id ? hover : base);

        const Transform3d& inst_matrix      = is_a ? mw_a : mw_b;
        const Transform3d  view_model_matrix = camera.get_view_matrix() * inst_matrix;
        shader->set_uniform("view_model_matrix", view_model_matrix);
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        m_planes[i].vbo.model.render();
    }

    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glDisable(GL_BLEND));

    shader->stop_using();
}

std::string GLGizmoFaceAlign::get_tooltip() const
{
    if (m_step == Step::PickSourceFace)
        return _u8L("Click a highlighted face on the object that should move");
    return _u8L("Click a highlighted face on the other object to align to");
}

void GLGizmoFaceAlign::on_render_input_window(float x, float y, float bottom_limit)
{
    const float h = m_imgui->scaled(10.f);
    y               = std::min(y, bottom_limit - h);
    ImGuiPureWrap::set_next_window_pos(x, y, ImGuiCond_Always);
    ImGuiPureWrap::begin(get_name(false), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    const float wrap = ImGui::GetFontSize() * 22.f;
    ImGuiPureWrap::text_wrapped(
        _u8L("Select exactly two object instances, then activate this tool. Convex hull face patches are shown on both — same idea as "
             "\"Place on face\". First click: face on the part that moves. Second click: face on the other instance. The first instance is "
             "rotated so the faces mate (outward normals opposed), then shifted so the first face center coincides with the second face center."),
        wrap);
    ImGui::Separator();
    if (m_step == Step::PickSourceFace)
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, _u8L("Step 1/2: click a highlighted face on the object to move."));
    else
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, _u8L("Step 2/2: click a highlighted face on the other instance."));

    if (ImGuiPureWrap::button(_u8L("Restart selection"))) {
        reset_picks();
        m_parent.set_as_dirty();
    }

    ImGuiPureWrap::end();
}

} // namespace GUI
} // namespace Slic3r
