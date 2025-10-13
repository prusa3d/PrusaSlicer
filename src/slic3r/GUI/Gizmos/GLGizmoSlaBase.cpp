///|/ Copyright (c) Prusa Research 2022 - 2023 Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "libslic3r/libslic3r.h"
#include "GLGizmoSlaBase.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/MainFrame.hpp"

#include "libslic3r/MultipleBeds.hpp"

namespace Slic3r {
namespace GUI {

static const ColorRGBA DISABLED_COLOR = ColorRGBA::DARK_GRAY();
static const int VOLUME_RAYCASTERS_BASE_ID = (int)SceneRaycaster::EIdBase::Gizmo;

GLGizmoSlaBase::GLGizmoSlaBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, SLAPrintObjectStep min_step)
: GLGizmoBase(parent, icon_filename, sprite_id)
, m_min_sla_print_object_step((int)min_step)
{}

/*static*/ bool GLGizmoSlaBase::selected_print_object_exists(const GLCanvas3D& canvas, const wxString& text)
{
    if (const Selection& sel = canvas.get_selection(); !sel.is_single_full_instance() || !sel.get_model()->objects[sel.get_object_idx()]
        || ! canvas.sla_print()->get_print_object_by_model_object_id(sel.get_model()->objects[sel.get_object_idx()]->id()))
    {
        if (! text.IsEmpty())
            wxGetApp().CallAfter([text]() {
                MessageDialog dlg(GUI::wxGetApp().mainframe, text,
                    _L("Bed selection mismatch"), wxICON_INFORMATION | wxOK);
                dlg.ShowModal();
            });
        return false;
    }
    return true;
}

void GLGizmoSlaBase::reslice_until_step(SLAPrintObjectStep step, bool postpone_error_messages)
{
    wxGetApp().CallAfter([this, step, postpone_error_messages]() {
        if (m_c->selection_info())
            wxGetApp().plater()->reslice_SLA_until_step(step, *m_c->selection_info()->model_object(), postpone_error_messages);
        else {
            const Selection& selection = m_parent.get_selection();
            const int object_idx = selection.get_object_idx();
            if (object_idx >= 0 && !selection.is_wipe_tower())
                wxGetApp().plater()->reslice_SLA_until_step(step, *wxGetApp().plater()->model().objects[object_idx], postpone_error_messages);
        }
    });
}

CommonGizmosDataID GLGizmoSlaBase::on_get_requirements() const
{
    return CommonGizmosDataID(
                int(CommonGizmosDataID::SelectionInfo)
              | int(CommonGizmosDataID::InstancesHider)
              | int(CommonGizmosDataID::Raycaster)
              | int(CommonGizmosDataID::ObjectClipper)
              | int(CommonGizmosDataID::SupportsClipper));
}

void GLGizmoSlaBase::update_volumes()
{
    m_volumes.clear();
    unregister_volume_raycasters_for_picking();

    const ModelObject* mo = m_c->selection_info()->model_object();
    if (mo == nullptr)
        return;

    const SLAPrintObject* po = m_c->selection_info()->print_object();
    if (po == nullptr)
        return;

    m_input_enabled = false;

    TriangleMesh backend_mesh;
    std::shared_ptr<const indexed_triangle_set> preview_mesh_ptr = po->get_mesh_to_print();
    if (preview_mesh_ptr != nullptr)
        backend_mesh = TriangleMesh(*preview_mesh_ptr);

    if (!backend_mesh.empty()) {
        auto last_comp_step = static_cast<int>(po->last_completed_step());
        if (last_comp_step == slaposCount)
            last_comp_step = -1;

        m_input_enabled = last_comp_step >= m_min_sla_print_object_step || po->model_object()->sla_points_status == sla::PointsStatus::UserModified;

        const int object_idx   = m_parent.get_selection().get_object_idx();
        const int instance_idx = m_parent.get_selection().get_instance_idx();

        const Geometry::Transformation& inst_trafo = po->model_object()->instances[instance_idx]->get_transformation();
        const double current_elevation = po->get_current_elevation();

        auto add_volume = [this, object_idx, instance_idx, &inst_trafo, current_elevation](const TriangleMesh& mesh, int volume_id, bool add_mesh_raycaster = false) {
            GLVolume* volume = m_volumes.volumes.emplace_back(new GLVolume());
            volume->model.init_from(mesh);
            volume->set_instance_transformation(inst_trafo);
            volume->set_sla_shift_z(current_elevation);
            if (add_mesh_raycaster)
                volume->mesh_raycaster = std::make_unique<GUI::MeshRaycaster>(mesh);
            if (m_input_enabled)
                volume->selected = true; // to set the proper color
            else
                volume->set_color(DISABLED_COLOR);
            volume->composite_id = GLVolume::CompositeID(object_idx, volume_id, instance_idx);
        };

        const Transform3d po_trafo_inverse = po->trafo().inverse();

        // main mesh
        backend_mesh.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()).cast<float>());
        backend_mesh.transform(po_trafo_inverse);
        add_volume(backend_mesh, 0, true);

        // supports mesh
        TriangleMesh supports_mesh = po->support_mesh();
        if (!supports_mesh.empty()) {
            supports_mesh.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()).cast<float>());
            supports_mesh.transform(po_trafo_inverse);
            add_volume(supports_mesh, -int(slaposSupportTree));
        }

        // pad mesh
        TriangleMesh pad_mesh = po->pad_mesh();
        if (!pad_mesh.empty()) {
            pad_mesh.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()).cast<float>());
            pad_mesh.transform(po_trafo_inverse);
            add_volume(pad_mesh, -int(slaposPad));
        }
    }

    if (m_volumes.volumes.empty()) {
        // No valid mesh found in the backend. Use the selection to duplicate the volumes
        const Selection& selection = m_parent.get_selection();
        const Selection::IndicesList& idxs = selection.get_volume_idxs();
        for (unsigned int idx : idxs) {
            const GLVolume* v = selection.get_volume(idx);
            if (!v->is_modifier) {
                m_volumes.volumes.emplace_back(new GLVolume());
                GLVolume* new_volume = m_volumes.volumes.back();
                const TriangleMesh& mesh = mo->volumes[v->volume_idx()]->mesh();
                new_volume->model.init_from(mesh);
                new_volume->set_instance_transformation(v->get_instance_transformation());
                new_volume->set_volume_transformation(v->get_volume_transformation());
                new_volume->set_sla_shift_z(v->get_sla_shift_z());
                new_volume->set_color(DISABLED_COLOR);
                new_volume->mesh_raycaster = std::make_unique<GUI::MeshRaycaster>(mesh);
            }
        }
    }

    register_volume_raycasters_for_picking();
}

void GLGizmoSlaBase::render_volumes()
{
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light_clip");
    if (shader == nullptr)
        return;

    shader->start_using();
    shader->set_uniform("emission_factor", 0.0f);
    const Camera& camera = wxGetApp().plater()->get_camera();

    ClippingPlane clipping_plane = (m_c->object_clipper()->get_position() == 0.0) ? ClippingPlane::ClipsNothing() : *m_c->object_clipper()->get_clipping_plane();
    if (m_c->object_clipper()->get_position() != 0.0)
        clipping_plane.set_normal(-clipping_plane.get_normal());
    else
        // on Linux the clipping plane does not work when using DBL_MAX
        clipping_plane.set_offset(FLT_MAX);
    m_volumes.set_clipping_plane(clipping_plane.get_data());

    for (GLVolume* v : m_volumes.volumes) {
        v->is_active = m_show_sla_supports || (!v->is_sla_pad() && !v->is_sla_support());
    }

    m_volumes.render(GLVolumeCollection::ERenderType::Opaque, true, camera.get_view_matrix(), camera.get_projection_matrix());
    shader->stop_using();
}

void GLGizmoSlaBase::register_volume_raycasters_for_picking()
{
    for (size_t i = 0; i < m_volumes.volumes.size(); ++i) {
        const GLVolume* v = m_volumes.volumes[i];
        if (!v->is_sla_pad() && !v->is_sla_support())
            m_volume_raycasters.emplace_back(m_parent.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, VOLUME_RAYCASTERS_BASE_ID + (int)i, *v->mesh_raycaster, v->world_matrix()));
    }
}

void GLGizmoSlaBase::unregister_volume_raycasters_for_picking()
{
    for (size_t i = 0; i < m_volume_raycasters.size(); ++i) {
        m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo, VOLUME_RAYCASTERS_BASE_ID + (int)i);
    }
    m_volume_raycasters.clear();
}

// Unprojects the mouse position on the mesh and saves hit point and normal of the facet into pos_and_normal
// Return false if no intersection was found, true otherwise.
bool GLGizmoSlaBase::unproject_on_mesh(const Vec2d& mouse_pos, std::pair<Vec3f, Vec3f>& pos_and_normal)
{
    if (m_c->raycaster()->raycasters().size() != 1)
        return false;
    if (!m_c->raycaster()->raycaster())
        return false;
    if (m_volumes.volumes.empty())
        return false;

    auto *inst = m_c->selection_info()->model_instance();
    if (!inst)
        return false;

    Transform3d trafo = m_volumes.volumes.front()->world_matrix();
    if (m_c->selection_info() && m_c->selection_info()->print_object()) {
        double shift_z = m_c->selection_info()->print_object()->get_current_elevation();
        trafo = inst->get_transformation().get_matrix();
        trafo.translation()(2) += shift_z;
    }

    // The raycaster query
    Vec3f hit;
    Vec3f normal;
    if (m_c->raycaster()->raycaster()->unproject_on_mesh(
        mouse_pos,
        trafo/*m_volumes.volumes.front()->world_matrix()*/,
        wxGetApp().plater()->get_camera(),
        hit,
        normal,
        m_c->object_clipper()->get_position() != 0.0 ? m_c->object_clipper()->get_clipping_plane() : nullptr)) {
        // Return both the point and the facet normal.
        pos_and_normal = std::make_pair(hit, normal);
        return true;
    }
    return false;
}

} // namespace GUI
} // namespace Slic3r
