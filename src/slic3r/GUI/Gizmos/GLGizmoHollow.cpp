///|/ Copyright (c) Prusa Research 2019 - 2023 Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros, Filip Sykala @Jony01, Lukáš Hejl @hejllukas, Oleksandra Iushchenko @YuSanka, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2019 BeldrothTheGold @BeldrothTheGold
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "libslic3r/libslic3r.h"
#include "GLGizmoHollow.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectSettings.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/SLAPrint.hpp"

#include "libslic3r/Model.hpp"

namespace Slic3r {
namespace GUI {

GLGizmoHollow::GLGizmoHollow(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoSlaBase(parent, icon_filename, sprite_id, slaposAssembly)
{
}


bool GLGizmoHollow::on_init()
{
    m_shortcut_key = WXK_CONTROL_H;
    m_desc["enable"]           = _u8L("Hollow this object");
    m_desc["preview"]          = _u8L("Preview hollowed and drilled model");
    m_desc["offset"]           = _u8L("Offset") + ": ";
    m_desc["quality"]          = _u8L("Quality") + ": ";
    m_desc["closing_distance"] = _u8L("Closing distance") + ": ";
    m_desc["hole_diameter"]    = _u8L("Hole diameter") + ": ";
    m_desc["hole_depth"]       = _u8L("Hole depth") + ": ";
    m_desc["remove_selected"]  = _u8L("Remove selected holes");
    m_desc["remove_all"]       = _u8L("Remove all holes");
    m_desc["clipping_of_view"] = _u8L("Clipping of view")+ ": ";
    m_desc["reset_direction"]  = _u8L("Reset direction");
    m_desc["show_supports"]    = _u8L("Show supports");

    return true;
}

void GLGizmoHollow::data_changed(bool is_serializing)
{
    if (! m_c->selection_info())
        return;

    const ModelObject* mo = m_c->selection_info()->model_object();
    if (m_state == On && mo) {
        if (m_old_mo_id != mo->id()) {
            reload_cache();
            m_old_mo_id = mo->id();
        }

        const SLAPrintObject* po = m_c->selection_info()->print_object();
        if (po != nullptr) {
            std::shared_ptr<const indexed_triangle_set> preview_mesh_ptr = po->get_mesh_to_print();
            if (!preview_mesh_ptr || preview_mesh_ptr->empty())
                reslice_until_step(slaposAssembly);
        }

        update_volumes();

        if (m_hole_raycasters.empty())
            register_hole_raycasters_for_picking();
        else
            update_hole_raycasters_for_picking_transform();

        m_c->instances_hider()->set_hide_full_scene(true);
    }
}



void GLGizmoHollow::on_render()
{
    if (! selected_print_object_exists(m_parent, wxEmptyString)) {
        wxGetApp().CallAfter([this]() {
            // Close current gizmo.
            m_parent.get_gizmos_manager().open_gizmo(m_parent.get_gizmos_manager().get_current_type());
        });
    }
    const Selection& selection = m_parent.get_selection();
    const CommonGizmosDataObjects::SelectionInfo* sel_info = m_c->selection_info();

    // If current m_c->m_model_object does not match selection, ask GLCanvas3D to turn us off
    if (m_state == On
     && (sel_info->model_object() != selection.get_model()->objects[selection.get_object_idx()]
      || sel_info->get_active_instance() != selection.get_instance_idx())) {
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_RESETGIZMOS));
        return;
    }

    if (m_state == On) {
        // This gizmo is showing the object elevated. Tell the common
        // SelectionInfo object to lie about the actual shift.
        m_c->selection_info()->set_use_shift(true);
    }

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_volumes();
    render_points(selection);

    m_selection_rectangle.render(m_parent);
    m_c->object_clipper()->render_cut();
    if (are_sla_supports_shown())
        m_c->supports_clipper()->render_cut();

    glsafe(::glDisable(GL_BLEND));
}

void GLGizmoHollow::on_register_raycasters_for_picking()
{
    register_hole_raycasters_for_picking();
    register_volume_raycasters_for_picking();
}

void GLGizmoHollow::on_unregister_raycasters_for_picking()
{
    unregister_hole_raycasters_for_picking();
    unregister_volume_raycasters_for_picking();
}

void GLGizmoHollow::render_points(const Selection& selection)
{
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;

    shader->start_using();
    ScopeGuard guard([shader]() { shader->stop_using(); });

    auto *inst = m_c->selection_info()->model_instance();
    if (!inst)
        return;

    double shift_z = m_c->selection_info()->print_object() ? m_c->selection_info()->print_object()->get_current_elevation() : 0.;
    Transform3d trafo(inst->get_transformation().get_matrix());
    trafo.translation()(2) += shift_z;
    const Geometry::Transformation transformation{trafo};

    const Transform3d instance_scaling_matrix_inverse = transformation.get_scaling_factor_matrix().inverse();
    const Camera& camera = wxGetApp().plater()->get_camera();
    const Transform3d& view_matrix = camera.get_view_matrix();
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    ColorRGBA render_color;
    const sla::DrainHoles& drain_holes = m_c->selection_info()->model_object()->sla_drain_holes;
    const size_t cache_size = drain_holes.size();

    for (size_t i = 0; i < cache_size; ++i) {
        const sla::DrainHole& drain_hole = drain_holes[i];
        const bool point_selected = m_selected[i];

        const bool clipped = is_mesh_point_clipped(drain_hole.pos.cast<double>());
        m_hole_raycasters[i]->set_active(!clipped);
        if (clipped)
            continue;

        // First decide about the color of the point.
        if (size_t(m_hover_id) == i)
            render_color = ColorRGBA::CYAN();
        else
            render_color = point_selected ? ColorRGBA(1.0f, 0.3f, 0.3f, 0.5f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f);

        m_cylinder.model.set_color(render_color);
        // Inverse matrix of the instance scaling is applied so that the mark does not scale with the object.
        const Transform3d hole_matrix = Geometry::translation_transform(drain_hole.pos.cast<double>()) * instance_scaling_matrix_inverse;

        if (transformation.is_left_handed())
            glsafe(::glFrontFace(GL_CW));

        // Matrices set, we can render the point mark now.
        Eigen::Quaterniond q;
        q.setFromTwoVectors(Vec3d::UnitZ(), instance_scaling_matrix_inverse * (-drain_hole.normal).cast<double>());
        const Eigen::AngleAxisd aa(q);
        const Transform3d model_matrix = trafo * hole_matrix * Transform3d(aa.toRotationMatrix()) *
            Geometry::translation_transform(-drain_hole.height * Vec3d::UnitZ()) * Geometry::scale_transform(Vec3d(drain_hole.radius, drain_hole.radius, drain_hole.height + sla::HoleStickOutLength));
        shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
        const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
        shader->set_uniform("view_normal_matrix", view_normal_matrix);
        m_cylinder.model.render();

        if (transformation.is_left_handed())
            glsafe(::glFrontFace(GL_CCW));
    }
}

bool GLGizmoHollow::is_mesh_point_clipped(const Vec3d& point) const
{
    if (m_c->object_clipper()->get_position() == 0.)
        return false;

    auto sel_info = m_c->selection_info();
    int active_inst = m_c->selection_info()->get_active_instance();
    const ModelInstance* mi = sel_info->model_object()->instances[active_inst];
    const Transform3d& trafo = mi->get_transformation().get_matrix() * sel_info->model_object()->volumes.front()->get_matrix();

    Vec3d transformed_point =  trafo * point;
    transformed_point(2) += sel_info->get_sla_shift();
    return m_c->object_clipper()->get_clipping_plane()->is_point_clipped(transformed_point);
}

// Following function is called from GLCanvas3D to inform the gizmo about a mouse/keyboard event.
// The gizmo has an opportunity to react - if it does, it should return true so that the Canvas3D is
// aware that the event was reacted to and stops trying to make different sense of it. If the gizmo
// concludes that the event was not intended for it, it should return false.
bool GLGizmoHollow::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    ModelObject* mo = m_c->selection_info()->model_object();
    int active_inst = m_c->selection_info()->get_active_instance();


    // left down with shift - show the selection rectangle:
    if (action == SLAGizmoEventType::LeftDown && (shift_down || alt_down || control_down)) {
        if (m_hover_id == -1) {
            if (shift_down || alt_down)
                m_selection_rectangle.start_dragging(mouse_position, shift_down ? GLSelectionRectangle::EState::Select : GLSelectionRectangle::EState::Deselect);
        }
        else {
            if (m_selected[m_hover_id])
                unselect_point(m_hover_id);
            else {
                if (!alt_down)
                    select_point(m_hover_id);
            }
        }

        return true;
    }

    // left down without selection rectangle - place point on the mesh:
    if (action == SLAGizmoEventType::LeftDown && !m_selection_rectangle.is_dragging() && !shift_down) {
        // If any point is in hover state, this should initiate its move - return control back to GLCanvas:
        if (m_hover_id != -1)
            return false;

        // If there is some selection, don't add new point and deselect everything instead.
        if (m_selection_empty) {
            std::pair<Vec3f, Vec3f> pos_and_normal;
            if (unproject_on_mesh(mouse_position, pos_and_normal)) { // we got an intersection
                Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Add drainage hole"));

                mo->sla_drain_holes.emplace_back(pos_and_normal.first,
                                                -pos_and_normal.second, m_new_hole_radius, m_new_hole_height);
                m_selected.push_back(false);
                assert(m_selected.size() == mo->sla_drain_holes.size());
                m_parent.set_as_dirty();
                m_wait_for_up_event = true;
                unregister_hole_raycasters_for_picking();
                register_hole_raycasters_for_picking();
            }
            else
                return false;
        }
        else
            select_point(NoPoints);

        return true;
    }

    // left up with selection rectangle - select points inside the rectangle:
    if ((action == SLAGizmoEventType::LeftUp || action == SLAGizmoEventType::ShiftUp || action == SLAGizmoEventType::AltUp) && m_selection_rectangle.is_dragging()) {
        // Is this a selection or deselection rectangle?
        GLSelectionRectangle::EState rectangle_status = m_selection_rectangle.get_state();

        // First collect positions of all the points in world coordinates.
        Geometry::Transformation trafo = mo->instances[active_inst]->get_transformation();
        trafo.set_offset(trafo.get_offset() + Vec3d(0., 0., m_c->selection_info()->get_sla_shift()));
        std::vector<Vec3d> points;
        for (unsigned int i=0; i<mo->sla_drain_holes.size(); ++i)
            points.push_back(trafo.get_matrix() * mo->sla_drain_holes[i].pos.cast<double>());

        // Now ask the rectangle which of the points are inside.
        std::vector<Vec3f> points_inside;
        std::vector<unsigned int> points_idxs = m_selection_rectangle.contains(points);
        m_selection_rectangle.stop_dragging();
        for (size_t idx : points_idxs)
            points_inside.push_back(points[idx].cast<float>());

        // Only select/deselect points that are actually visible
        for (size_t idx : m_c->raycaster()->raycaster()->get_unobscured_idxs(
                 trafo, wxGetApp().plater()->get_camera(), points_inside,
                 m_c->object_clipper()->get_clipping_plane()))
        {
            if (rectangle_status == GLSelectionRectangle::EState::Deselect)
                unselect_point(points_idxs[idx]);
            else
                select_point(points_idxs[idx]);
        }
        return true;
    }

    // left up with no selection rectangle
    if (action == SLAGizmoEventType::LeftUp) {
        if (m_wait_for_up_event) {
            m_wait_for_up_event = false;
            return true;
        }
    }

    // dragging the selection rectangle:
    if (action == SLAGizmoEventType::Dragging) {
        if (m_wait_for_up_event)
            return true; // point has been placed and the button not released yet
                         // this prevents GLCanvas from starting scene rotation

        if (m_selection_rectangle.is_dragging())  {
            m_selection_rectangle.dragging(mouse_position);
            return true;
        }

        return false;
    }

    if (action == SLAGizmoEventType::Delete) {
        // delete key pressed
        delete_selected_points();
        return true;
    }

    if (action == SLAGizmoEventType::RightDown) {
        if (m_hover_id != -1) {
            select_point(NoPoints);
            select_point(m_hover_id);
            delete_selected_points();
            return true;
        }
        return false;
    }

    if (action == SLAGizmoEventType::SelectAll) {
        select_point(AllPoints);
        return true;
    }

    if (action == SLAGizmoEventType::MouseWheelUp && control_down) {
        double pos = m_c->object_clipper()->get_position();
        pos = std::min(1., pos + 0.01);
        m_c->object_clipper()->set_position_by_ratio(pos, true);
        return true;
    }

    if (action == SLAGizmoEventType::MouseWheelDown && control_down) {
        double pos = m_c->object_clipper()->get_position();
        pos = std::max(0., pos - 0.01);
        m_c->object_clipper()->set_position_by_ratio(pos, true);
        return true;
    }

    if (action == SLAGizmoEventType::ResetClippingPlane) {
        m_c->object_clipper()->set_position_by_ratio(-1., false);
        return true;
    }

    return false;
}

void GLGizmoHollow::delete_selected_points()
{
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Delete drainage hole")));
    sla::DrainHoles& drain_holes = m_c->selection_info()->model_object()->sla_drain_holes;

    for (unsigned int idx=0; idx<drain_holes.size(); ++idx) {
        if (m_selected[idx]) {
            m_selected.erase(m_selected.begin()+idx);
            drain_holes.erase(drain_holes.begin() + (idx--));
        }
    }

    unregister_hole_raycasters_for_picking();
    register_hole_raycasters_for_picking();
    select_point(NoPoints);
}

bool GLGizmoHollow::on_mouse(const wxMouseEvent &mouse_event)
{
    if (!is_input_enabled()) return true;
    if (mouse_event.Moving()) return false;
    if (use_grabbers(mouse_event)) return true;

    // wxCoord == int --> wx/types.h
    Vec2i mouse_coord(mouse_event.GetX(), mouse_event.GetY());
    Vec2d mouse_pos = mouse_coord.cast<double>();

    static bool pending_right_up = false;
    if (mouse_event.LeftDown()) {
        bool control_down = mouse_event.CmdDown();
        bool grabber_contains_mouse = (get_hover_id() != -1);
        if ((!control_down || grabber_contains_mouse) &&
            gizmo_event(SLAGizmoEventType::LeftDown, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), false))
            // the gizmo got the event and took some action, there is no need
            // to do anything more
            return true;
    } else if (mouse_event.Dragging()) {
        if (m_parent.get_move_volume_id() != -1)
            // don't allow dragging objects with the Sla gizmo on
            return true;

        bool control_down = mouse_event.CmdDown();
        if (control_down) {
            // CTRL has been pressed while already dragging -> stop current action
            if (mouse_event.LeftIsDown())
                gizmo_event(SLAGizmoEventType::LeftUp, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), true);
            else if (mouse_event.RightIsDown()) {
                pending_right_up = false;
            }
        } else if(gizmo_event(SLAGizmoEventType::Dragging, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), false)) {
            // the gizmo got the event and took some action, no need to do
            // anything more here
            m_parent.set_as_dirty();
            return true;
        }
    } else if (mouse_event.LeftUp()) {    
        if (!m_parent.is_mouse_dragging()) {
            bool control_down = mouse_event.CmdDown();
            // in case gizmo is selected, we just pass the LeftUp event
            // and stop processing - neither object moving or selecting is
            // suppressed in that case
            gizmo_event(SLAGizmoEventType::LeftUp, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), control_down);
            return true;
        }
    } else if (mouse_event.RightDown()) {
        if (m_parent.get_selection().get_object_idx() != -1 &&
            gizmo_event(SLAGizmoEventType::RightDown, mouse_pos, false, false, false)) {
            // we need to set the following right up as processed to avoid showing
            // the context menu if the user release the mouse over the object
            pending_right_up = true;
            // event was taken care of by the SlaSupports gizmo
            return true;
        }
    } else if (mouse_event.RightUp()) {
        if (pending_right_up) {
            pending_right_up = false;
            return true;
        }
    }
    return false;
}

void GLGizmoHollow::register_hole_raycasters_for_picking()
{
    assert(m_hole_raycasters.empty());

    init_cylinder_model();

    const CommonGizmosDataObjects::SelectionInfo* info = m_c->selection_info();
    if (info != nullptr && !info->model_object()->sla_drain_holes.empty()) {
        const sla::DrainHoles& drain_holes = info->model_object()->sla_drain_holes;
        for (int i = 0; i < (int)drain_holes.size(); ++i) {
            m_hole_raycasters.emplace_back(m_parent.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, i, *m_cylinder.mesh_raycaster, Transform3d::Identity()));
        }
        update_hole_raycasters_for_picking_transform();
    }
}

void GLGizmoHollow::unregister_hole_raycasters_for_picking()
{
    for (size_t i = 0; i < m_hole_raycasters.size(); ++i) {
        m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo, i);
    }
    m_hole_raycasters.clear();
}

void GLGizmoHollow::update_hole_raycasters_for_picking_transform()
{
    const CommonGizmosDataObjects::SelectionInfo* info = m_c->selection_info();
    if (info != nullptr) {
        const sla::DrainHoles& drain_holes = info->model_object()->sla_drain_holes;
        if (!drain_holes.empty()) {
            assert(!m_hole_raycasters.empty());

            const GLVolume* vol = m_parent.get_selection().get_first_volume();
            Geometry::Transformation transformation(vol->get_instance_transformation());

            auto *inst = m_c->selection_info()->model_instance();
            if (inst && m_c->selection_info() && m_c->selection_info()->print_object()) {
                double shift_z = m_c->selection_info()->print_object()->get_current_elevation();
                auto trafo = inst->get_transformation().get_matrix();
                trafo.translation()(2) += shift_z;
                transformation.set_matrix(trafo);
            }
            const Transform3d instance_scaling_matrix_inverse = transformation.get_scaling_factor_matrix().inverse();

            for (size_t i = 0; i < drain_holes.size(); ++i) {
                const sla::DrainHole& drain_hole = drain_holes[i];
                const Transform3d hole_matrix = Geometry::translation_transform(drain_hole.pos.cast<double>()) * instance_scaling_matrix_inverse;
                Eigen::Quaterniond q;
                q.setFromTwoVectors(Vec3d::UnitZ(), instance_scaling_matrix_inverse * (-drain_hole.normal).cast<double>());
                const Eigen::AngleAxisd aa(q);
                const Transform3d matrix = transformation.get_matrix() * hole_matrix * Transform3d(aa.toRotationMatrix()) *
                    Geometry::translation_transform(-drain_hole.height * Vec3d::UnitZ()) * Geometry::scale_transform(Vec3d(drain_hole.radius, drain_hole.radius, drain_hole.height + sla::HoleStickOutLength));
                m_hole_raycasters[i]->set_transform(matrix);
            }
        }
    }
}

std::vector<std::pair<const ConfigOption*, const ConfigOptionDef*>>
GLGizmoHollow::get_config_options(const std::vector<std::string>& keys) const
{
    std::vector<std::pair<const ConfigOption*, const ConfigOptionDef*>> out;
    const ModelObject* mo = m_c->selection_info()->model_object();

    if (! mo)
        return out;

    const DynamicPrintConfig& object_cfg = mo->config.get();
    const DynamicPrintConfig& print_cfg = wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
    std::unique_ptr<DynamicPrintConfig> default_cfg = nullptr;

    for (const std::string& key : keys) {
        if (object_cfg.has(key))
            out.emplace_back(object_cfg.option(key), object_cfg.option_def(key));
        else
            if (print_cfg.has(key))
                out.emplace_back(print_cfg.option(key), print_cfg.option_def(key));
            else { // we must get it from defaults
                if (default_cfg == nullptr)
                    default_cfg.reset(DynamicPrintConfig::new_from_defaults_keys(keys));
                out.emplace_back(default_cfg->option(key), default_cfg->option_def(key));
            }
    }

    return out;
}


void GLGizmoHollow::on_render_input_window(float x, float y, float bottom_limit)
{
    ModelObject* mo = m_c->selection_info()->model_object();
    if (! mo)
        return;

    bool first_run = true; // This is a hack to redraw the button when all points are removed,
                           // so it is not delayed until the background process finishes.

    ConfigOptionMode current_mode = wxGetApp().get_mode();

    std::vector<std::string> opts_keys = {"hollowing_min_thickness", "hollowing_quality", "hollowing_closing_distance"};
    auto opts = get_config_options(opts_keys);
    auto* offset_cfg = static_cast<const ConfigOptionFloat*>(opts[0].first);
    float offset = offset_cfg->value;
    double offset_min = opts[0].second->min;
    double offset_max = opts[0].second->max;

    auto* quality_cfg = static_cast<const ConfigOptionFloat*>(opts[1].first);
    float quality = quality_cfg->value;
    double quality_min = opts[1].second->min;
    double quality_max = opts[1].second->max;
    ConfigOptionMode quality_mode = opts[1].second->mode;

    auto* closing_d_cfg = static_cast<const ConfigOptionFloat*>(opts[2].first);
    float closing_d = closing_d_cfg->value;
    double closing_d_min = opts[2].second->min;
    double closing_d_max = opts[2].second->max;
    ConfigOptionMode closing_d_mode = opts[2].second->mode;

    m_desc["offset"]            = into_u8(_(opts[0].second->label)) + ":";
    m_desc["quality"]           = into_u8(_(opts[1].second->label)) + ":";
    m_desc["closing_distance"]  = into_u8(_(opts[2].second->label)) + ":";


RENDER_AGAIN:
    const float approx_height = m_imgui->scaled(20.0f);
    y = std::min(y, bottom_limit - approx_height);
    ImGuiPureWrap::set_next_window_pos(x, y, ImGuiCond_Always);

    ImGuiPureWrap::begin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:
    const float clipping_slider_left = std::max(ImGuiPureWrap::calc_text_size(m_desc.at("clipping_of_view")).x,
                                                ImGuiPureWrap::calc_text_size(m_desc.at("reset_direction")).x) + m_imgui->scaled(0.5f);

    const float settings_sliders_left =
        std::max(std::max({ImGuiPureWrap::calc_text_size(m_desc.at("offset")).x,
                           ImGuiPureWrap::calc_text_size(m_desc.at("quality")).x,
                           ImGuiPureWrap::calc_text_size(m_desc.at("closing_distance")).x,
                           ImGuiPureWrap::calc_text_size(m_desc.at("hole_diameter")).x,
                           ImGuiPureWrap::calc_text_size(m_desc.at("hole_depth")).x}) + m_imgui->scaled(0.5f), clipping_slider_left);

    const float diameter_slider_left = settings_sliders_left; //ImGuiPureWrap::calc_text_size(m_desc.at("hole_diameter")).x + m_imgui->scaled(1.f);
    const float minimal_slider_width = m_imgui->scaled(4.f);

    const float button_preview_width = ImGuiPureWrap::calc_button_size(m_desc.at("preview")).x;

    float window_width = minimal_slider_width + std::max({settings_sliders_left, clipping_slider_left, diameter_slider_left});
    window_width = std::max(window_width, button_preview_width);

    m_imgui->disabled_begin(!is_input_enabled());

    if (ImGuiPureWrap::button(m_desc["preview"]))
        reslice_until_step(slaposDrillHoles);

    bool config_changed = false;

    ImGui::Separator();

    {
        auto opts = get_config_options({"hollowing_enable"});
        m_enable_hollowing = static_cast<const ConfigOptionBool*>(opts[0].first)->value;
        if (ImGuiPureWrap::checkbox(m_desc["enable"], m_enable_hollowing)) {
            mo->config.set("hollowing_enable", m_enable_hollowing);
            wxGetApp().obj_list()->update_and_show_object_settings_item();
            config_changed = true;
        }
    }

    m_imgui->disabled_end();

    m_imgui->disabled_begin(!is_input_enabled() || !m_enable_hollowing);

    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(m_desc.at("offset"));
    ImGui::SameLine(settings_sliders_left, ImGuiPureWrap::get_item_spacing().x);
    ImGui::PushItemWidth(window_width - settings_sliders_left);
    m_imgui->slider_float("##offset", &offset, offset_min, offset_max, "%.1f mm", 1.0f, true, _L(opts[0].second->tooltip));

    bool slider_clicked = m_imgui->get_last_slider_status().clicked; // someone clicked the slider
    bool slider_edited =m_imgui->get_last_slider_status().edited; // someone is dragging the slider
    bool slider_released =m_imgui->get_last_slider_status().deactivated_after_edit; // someone has just released the slider

    if (current_mode >= quality_mode) {
        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text(m_desc.at("quality"));
        ImGui::SameLine(settings_sliders_left, ImGuiPureWrap::get_item_spacing().x);
        m_imgui->slider_float("##quality", &quality, quality_min, quality_max, "%.1f", 1.0f, true, _L(opts[1].second->tooltip));

        slider_clicked |= m_imgui->get_last_slider_status().clicked;
        slider_edited |= m_imgui->get_last_slider_status().edited;
        slider_released |= m_imgui->get_last_slider_status().deactivated_after_edit;
    }

    if (current_mode >= closing_d_mode) {
        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text(m_desc.at("closing_distance"));
        ImGui::SameLine(settings_sliders_left, ImGuiPureWrap::get_item_spacing().x);
        m_imgui->slider_float("##closing_distance", &closing_d, closing_d_min, closing_d_max, "%.1f mm", 1.0f, true, _L(opts[2].second->tooltip));

        slider_clicked |= m_imgui->get_last_slider_status().clicked;
        slider_edited |= m_imgui->get_last_slider_status().edited;
        slider_released |= m_imgui->get_last_slider_status().deactivated_after_edit;
    }

    if (slider_clicked) {
        m_offset_stash = offset;
        m_quality_stash = quality;
        m_closing_d_stash = closing_d;
    }
    if (slider_edited || slider_released) {
        if (slider_released) {
            mo->config.set("hollowing_min_thickness", m_offset_stash);
            mo->config.set("hollowing_quality", m_quality_stash);
            mo->config.set("hollowing_closing_distance", m_closing_d_stash);
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Hollowing parameter change"));
        }
        mo->config.set("hollowing_min_thickness", offset);
        mo->config.set("hollowing_quality", quality);
        mo->config.set("hollowing_closing_distance", closing_d);
        if (slider_released) {
            wxGetApp().obj_list()->update_and_show_object_settings_item();
            config_changed = true;
        }
    }

    m_imgui->disabled_end();

    bool force_refresh = false;
    bool remove_selected = false;
    bool remove_all = false;

    ImGui::Separator();

    float diameter_upper_cap = 60.;
    if (m_new_hole_radius * 2.f > diameter_upper_cap)
        m_new_hole_radius = diameter_upper_cap / 2.f;
    ImGui::AlignTextToFramePadding();

    m_imgui->disabled_begin(!is_input_enabled());

    ImGuiPureWrap::text(m_desc.at("hole_diameter"));
    ImGui::SameLine(diameter_slider_left, ImGuiPureWrap::get_item_spacing().x);
    ImGui::PushItemWidth(window_width - diameter_slider_left);
    float diam = 2.f * m_new_hole_radius;
    m_imgui->slider_float("##hole_diameter", &diam, 1.f, 25.f, "%.1f mm", 1.f, false);

    // Let's clamp the value (which could have been entered by keyboard) to a larger range
    // than the slider. This allows entering off-scale values and still protects against
    //complete non-sense.
    diam = std::clamp(diam, 0.1f, diameter_upper_cap);
    m_new_hole_radius = diam / 2.f;
    bool clicked = m_imgui->get_last_slider_status().clicked;
    bool edited = m_imgui->get_last_slider_status().edited;
    bool deactivated = m_imgui->get_last_slider_status().deactivated_after_edit;

    ImGui::AlignTextToFramePadding();

    ImGuiPureWrap::text(m_desc["hole_depth"]);
    ImGui::SameLine(diameter_slider_left, ImGuiPureWrap::get_item_spacing().x);
    m_imgui->slider_float("##hole_depth", &m_new_hole_height, 0.f, 10.f, "%.1f mm", 1.f, false);

    m_imgui->disabled_end();

    // Same as above:
    m_new_hole_height = std::clamp(m_new_hole_height, 0.f, 100.f);

    clicked |= m_imgui->get_last_slider_status().clicked;
    edited |= m_imgui->get_last_slider_status().edited;
    deactivated |= m_imgui->get_last_slider_status().deactivated_after_edit;;

    // Following is a nasty way to:
    //  - save the initial value of the slider before one starts messing with it
    //  - keep updating the head radius during sliding so it is continuosly refreshed in 3D scene
    //  - take correct undo/redo snapshot after the user is done with moving the slider
    if (! m_selection_empty) {
        if (clicked) {
            m_holes_stash = mo->sla_drain_holes;
        }
        if (edited) {
            for (size_t idx=0; idx<m_selected.size(); ++idx)
                if (m_selected[idx]) {
                    mo->sla_drain_holes[idx].radius = m_new_hole_radius;
                    mo->sla_drain_holes[idx].height = m_new_hole_height;
                }
        }
        if (deactivated) {
            // momentarily restore the old value to take snapshot
            sla::DrainHoles new_holes = mo->sla_drain_holes;
            mo->sla_drain_holes = m_holes_stash;
            float backup_rad = m_new_hole_radius;
            float backup_hei = m_new_hole_height;
            for (size_t i=0; i<m_holes_stash.size(); ++i) {
                if (m_selected[i]) {
                    m_new_hole_radius = m_holes_stash[i].radius;
                    m_new_hole_height = m_holes_stash[i].height;
                    break;
                }
            }
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Change drainage hole diameter"));
            m_new_hole_radius = backup_rad;
            m_new_hole_height = backup_hei;
            mo->sla_drain_holes = new_holes;
        }
    }

    m_imgui->disabled_begin(!is_input_enabled() || m_selection_empty);
    remove_selected = ImGuiPureWrap::button(m_desc.at("remove_selected"));
    m_imgui->disabled_end();

    m_imgui->disabled_begin(!is_input_enabled() || mo->sla_drain_holes.empty());
    remove_all = ImGuiPureWrap::button(m_desc.at("remove_all"));
    m_imgui->disabled_end();

    // Following is rendered in both editing and non-editing mode:
    ImGui::Separator();
    m_imgui->disabled_begin(!is_input_enabled());
    if (m_c->object_clipper()->get_position() == 0.f) {
        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text(m_desc.at("clipping_of_view"));
    }
    else {
        if (ImGuiPureWrap::button(m_desc.at("reset_direction"))) {
            wxGetApp().CallAfter([this](){
                    m_c->object_clipper()->set_position_by_ratio(-1., false);
                });
        }
    }

    ImGui::SameLine(settings_sliders_left, ImGuiPureWrap::get_item_spacing().x);
    ImGui::PushItemWidth(window_width - settings_sliders_left);
    float clp_dist = m_c->object_clipper()->get_position();
    if (m_imgui->slider_float("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f"))
        m_c->object_clipper()->set_position_by_ratio(clp_dist, true);

    // make sure supports are shown/hidden as appropriate
    ImGui::Separator();
    bool show_sups = are_sla_supports_shown();
    if (ImGuiPureWrap::checkbox(m_desc["show_supports"], show_sups)) {
        show_sla_supports(show_sups);
        force_refresh = true;
    }

    m_imgui->disabled_end();
    ImGuiPureWrap::end();


    if (remove_selected || remove_all) {
        force_refresh = false;
        m_parent.set_as_dirty();

        if (remove_all) {
            select_point(AllPoints);
            delete_selected_points();
        }
        if (remove_selected)
            delete_selected_points();

        if (first_run) {
            first_run = false;
            goto RENDER_AGAIN;
        }
    }

    if (force_refresh)
        m_parent.set_as_dirty();

    if (config_changed)
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_FORCE_UPDATE));
}

bool GLGizmoHollow::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();

    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptSLA
        || !selection.is_single_full_instance())
        return false;

    // Check that none of the selected volumes is outside. Only SLA auxiliaries (supports) are allowed outside.
    const Selection::IndicesList& list = selection.get_volume_idxs();
    for (const auto& idx : list)
        if (selection.get_volume(idx)->is_outside && selection.get_volume(idx)->composite_id.volume_id >= 0)
            return false;

    // Check that none of the selected volumes is marked as non-pritable.
    for (const auto& idx : list) {
        if (!selection.get_volume(idx)->printable)
            return false;
    }

    return true;
}

bool GLGizmoHollow::on_is_selectable() const
{
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA);
}

std::string GLGizmoHollow::on_get_name() const
{
    return _u8L("Hollow and drill");
}

void GLGizmoHollow::on_set_state()
{
    if (m_state == m_old_state)
        return;

    if (m_state == On) {       
        // Make sure that current object is on current bed. Refuse to turn on otherwise.
        if (! selected_print_object_exists(m_parent, _L("Selected object has to be on the active bed."))) {
            m_state = Off;
            return;
        }
    }

    if (m_state == Off && m_old_state != Off) {
        // the gizmo was just turned Off
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_FORCE_UPDATE));
        m_c->instances_hider()->set_hide_full_scene(false);
        m_c->selection_info()->set_use_shift(false); // see top of on_render for details
    }

    m_old_state = m_state;
}



void GLGizmoHollow::on_start_dragging()
{
    if (m_hover_id != -1) {
        select_point(NoPoints);
        select_point(m_hover_id);
        m_hole_before_drag = m_c->selection_info()->model_object()->sla_drain_holes[m_hover_id].pos;
    }
    else
        m_hole_before_drag = Vec3f::Zero();
}


void GLGizmoHollow::on_stop_dragging()
{
    sla::DrainHoles& drain_holes = m_c->selection_info()->model_object()->sla_drain_holes;
    if (m_hover_id != -1) {
        Vec3f backup = drain_holes[m_hover_id].pos;

        if (m_hole_before_drag != Vec3f::Zero() // some point was touched
         && backup != m_hole_before_drag) // and it was moved, not just selected
        {
            drain_holes[m_hover_id].pos = m_hole_before_drag;
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Move drainage hole")));
            drain_holes[m_hover_id].pos = backup;
        }
    }
    m_hole_before_drag = Vec3f::Zero();
}


void GLGizmoHollow::on_dragging(const UpdateData &data)
{
    assert(m_hover_id != -1);
    std::pair<Vec3f, Vec3f> pos_and_normal;
    if (!unproject_on_mesh(data.mouse_pos.cast<double>(), pos_and_normal))
        return;
    sla::DrainHoles &drain_holes =  m_c->selection_info()->model_object()->sla_drain_holes;
    drain_holes[m_hover_id].pos    = pos_and_normal.first;
    drain_holes[m_hover_id].normal = -pos_and_normal.second;
}


void GLGizmoHollow::on_load(cereal::BinaryInputArchive& ar)
{
    ar(m_new_hole_radius,
       m_new_hole_height,
       m_selected,
       m_selection_empty
    );
}



void GLGizmoHollow::on_save(cereal::BinaryOutputArchive& ar) const
{
    ar(m_new_hole_radius,
       m_new_hole_height,
       m_selected,
       m_selection_empty
    );
}



void GLGizmoHollow::select_point(int i)
{
    const sla::DrainHoles& drain_holes = m_c->selection_info()->model_object()->sla_drain_holes;

    if (i == AllPoints || i == NoPoints) {
        m_selected.assign(m_selected.size(), i == AllPoints);
        m_selection_empty = (i == NoPoints);

        if (i == AllPoints && !drain_holes.empty()) {
            m_new_hole_radius = drain_holes[0].radius;
            m_new_hole_height = drain_holes[0].height;
        }
    }
    else {
        while (size_t(i) >= m_selected.size())
            m_selected.push_back(false);
        m_selected[i] = true;
        m_selection_empty = false;
        m_new_hole_radius = drain_holes[i].radius;
        m_new_hole_height = drain_holes[i].height;
    }
}


void GLGizmoHollow::unselect_point(int i)
{
    m_selected[i] = false;
    m_selection_empty = true;
    for (const bool sel : m_selected) {
        if (sel) {
            m_selection_empty = false;
            break;
        }
    }
}

void GLGizmoHollow::reload_cache()
{
    m_selected.clear();
    m_selected.assign(m_c->selection_info()->model_object()->sla_drain_holes.size(), false);
}


void GLGizmoHollow::on_set_hover_id()
{
    if (m_c->selection_info()->model_object() == nullptr)
        return;

    if (int(m_c->selection_info()->model_object()->sla_drain_holes.size()) <= m_hover_id)
        m_hover_id = -1;
}

void GLGizmoHollow::init_cylinder_model()
{
    if (!m_cylinder.model.is_initialized()) {
        indexed_triangle_set its = its_make_cylinder(1.0, 1.0);
        m_cylinder.model.init_from(its);
        m_cylinder.mesh_raycaster = std::make_unique<MeshRaycaster>(std::make_shared<const TriangleMesh>(std::move(its)));
    }
}



} // namespace GUI
} // namespace Slic3r
