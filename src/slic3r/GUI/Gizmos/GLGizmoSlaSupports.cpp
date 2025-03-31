///|/ Copyright (c) Prusa Research 2019 - 2023 Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Tomáš Mészáros @tamasmeszaros, Filip Sykala @Jony01, Lukáš Hejl @hejllukas, Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2019 BeldrothTheGold @BeldrothTheGold
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "libslic3r/libslic3r.h"
#include "GLGizmoSlaSupports.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include <GL/glew.h>

#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/stattext.h>

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_ObjectSettings.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/SLAPrint.hpp"

#include "libslic3r/SLA/SupportIslands/SampleConfigFactory.hpp"
#include "imgui/imgui_stdlib.h" // string input for ImGui
static const double CONE_RADIUS = 0.25;
static const double CONE_HEIGHT = 0.75;

using namespace Slic3r;
using namespace Slic3r::GUI;

namespace {

enum class IconType : unsigned {
    show_support_points_selected,
    show_support_points_unselected,
    show_support_points_hovered,
    show_support_structure_selected,
    show_support_structure_unselected,
    show_support_structure_hovered,
    // automatic calc of icon's count
    _count
};

IconManager::Icons init_icons(IconManager &mng, ImVec2 size = ImVec2{50, 50}) {
    mng.release();

    // icon order has to match the enum IconType
    IconManager::InitTypes init_types {        
        {"support_structure_invisible.svg", size, IconManager::RasterType::color},           // show_support_points_selected
        {"support_structure_invisible.svg", size, IconManager::RasterType::gray_only_data},  // show_support_points_unselected
        {"support_structure_invisible.svg", size, IconManager::RasterType::color},           // show_support_points_hovered

        {"support_structure.svg", size, IconManager::RasterType::color},           // show_support_structure_selected
        {"support_structure.svg", size, IconManager::RasterType::gray_only_data},  // show_support_structure_unselected
        {"support_structure.svg", size, IconManager::RasterType::color},           // show_support_structure_hovered
    };

    assert(init_types.size() == static_cast<size_t>(IconType::_count));
    std::string path = resources_dir() + "/icons/";
    for (IconManager::InitType &init_type : init_types)
        init_type.filepath = path + init_type.filepath;

    return mng.init(init_types);
}
const IconManager::Icon &get_icon(const IconManager::Icons &icons, IconType type) { 
    return *icons[static_cast<unsigned>(type)]; }

/// <summary>
/// Draw icon buttons to swap between show structure and only supports points
/// </summary>
/// <param name="show_support_structure">In|Out view mode</param>
/// <param name="icons">all loaded icons</param>
/// <returns>True when change is made</returns>
bool draw_view_mode(bool &show_support_structure, const IconManager::Icons &icons) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 8.);
    ScopeGuard sg([] { ImGui::PopStyleVar(); });
    if (show_support_structure) {        
        draw(get_icon(icons, IconType::show_support_structure_selected));
        if(ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", _u8L("Visible support structure").c_str());
        ImGui::SameLine();
        if (clickable(get_icon(icons, IconType::show_support_points_unselected),
                      get_icon(icons, IconType::show_support_points_hovered))) {
            show_support_structure = false;
            return true;
        } else if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", _u8L("Click to show support points without support structure").c_str()); 
    } else { // !show_support_structure
        if (clickable(get_icon(icons, IconType::show_support_structure_unselected),
                      get_icon(icons, IconType::show_support_structure_hovered))) {
            show_support_structure = true;
            return true;
        } else if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", _u8L("Click to show support structure with pad").c_str()); 
        ImGui::SameLine();
        draw(get_icon(icons, IconType::show_support_points_selected));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", _u8L("Visible support points without support structure").c_str());
    }
    return false;
}
} // namespace


GLGizmoSlaSupports::GLGizmoSlaSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoSlaBase(parent, icon_filename, sprite_id, slaposDrillHoles /*slaposSupportPoints*/) {
    show_sla_supports(false);
}

bool GLGizmoSlaSupports::on_init()
{
    m_shortcut_key = WXK_CONTROL_L;

    m_desc["head_diameter"]    = _u8L("Head diameter") + ": ";
    m_desc["lock_supports"]    = _u8L("Lock supports under new islands");
    m_desc["remove_selected"]  = _u8L("Remove selected points");
    m_desc["remove_all"]       = _u8L("Remove all points");
    m_desc["apply_changes"]    = _u8L("Apply changes");
    m_desc["discard_changes"]  = _u8L("Discard changes");
    m_desc["points_density"]   = _u8L("Support points density");
    m_desc["auto_generate"]    = _u8L("Auto-generate points");
    m_desc["manual_editing"]   = _u8L("Manual editing");
    m_desc["clipping_of_view"] = _u8L("Clipping of view")+ ": ";
    m_desc["reset_direction"]  = _u8L("Reset direction");
        
    return true;
}

void GLGizmoSlaSupports::data_changed(bool is_serializing)
{
    if (! m_c->selection_info())
        return;

    ModelObject* mo = m_c->selection_info()->model_object();

    if (m_state == On && mo && mo->id() != m_old_mo_id) {
        disable_editing_mode();
        reload_cache();
        m_old_mo_id = mo->id();
    }

    // If we triggered autogeneration before, check backend and fetch results if they are there
    if (mo) {
        m_c->instances_hider()->set_hide_full_scene(true);

        int last_comp_step = slaposCount;
        const int required_step = get_min_sla_print_object_step();
        const SLAPrintObject* po = m_c->selection_info()->print_object();
        if (po != nullptr)
            last_comp_step = static_cast<int>(po->last_completed_step());

        if (last_comp_step == slaposCount)
            last_comp_step = -1;

        if (po != nullptr && last_comp_step < required_step)
            reslice_until_step((SLAPrintObjectStep)required_step, false);

        update_volumes();

        if (mo->sla_points_status == sla::PointsStatus::Generating)
            get_data_from_backend();

        if (m_point_raycasters.empty())
            register_point_raycasters_for_picking();
        else
            update_point_raycasters_for_picking_transform();

        m_c->instances_hider()->set_hide_full_scene(true);
    }

//    m_parent.toggle_model_objects_visibility(false);
}



void GLGizmoSlaSupports::on_render()
{
    if (! selected_print_object_exists(m_parent, wxEmptyString)) {
        wxGetApp().CallAfter([this]() {
            // Close current gizmo.
            m_parent.get_gizmos_manager().open_gizmo(m_parent.get_gizmos_manager().get_current_type());
        });
    }

    if (m_state == On) {
        // This gizmo is showing the object elevated. Tell the common
        // SelectionInfo object to lie about the actual shift.
        m_c->selection_info()->set_use_shift(true);
    }

    if (!m_sphere.model.is_initialized()) {
        indexed_triangle_set its = its_make_sphere(1.0, double(PI) / 12.0);
        m_sphere.model.init_from(its);
        m_sphere.mesh_raycaster = std::make_unique<MeshRaycaster>(std::make_shared<const TriangleMesh>(std::move(its)));
    }
    if (!m_cone.model.is_initialized()) {
        indexed_triangle_set its = its_make_cone(1.0, 1.0, double(PI) / 12.0);
        m_cone.model.init_from(its);
        m_cone.mesh_raycaster = std::make_unique<MeshRaycaster>(std::make_shared<const TriangleMesh>(std::move(its)));
    }

    ModelObject* mo = m_c->selection_info()->model_object();
    const Selection& selection = m_parent.get_selection();

    // If current m_c->m_model_object does not match selection, ask GLCanvas3D to turn us off
    if (m_state == On
     && (mo != selection.get_model()->objects[selection.get_object_idx()]
      || m_c->selection_info()->get_active_instance() != selection.get_instance_idx())) {
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_RESETGIZMOS));
        return;
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

void GLGizmoSlaSupports::on_register_raycasters_for_picking()
{
    register_point_raycasters_for_picking();
    register_volume_raycasters_for_picking();
}

void GLGizmoSlaSupports::on_unregister_raycasters_for_picking()
{
    unregister_point_raycasters_for_picking();
    unregister_volume_raycasters_for_picking();
}

void GLGizmoSlaSupports::render_points(const Selection& selection)
{
    const size_t cache_size = m_editing_mode ? m_editing_cache.size() : m_normal_cache.size();

    const bool has_points = (cache_size != 0);
    if (!has_points)
        return;

    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;

    shader->start_using();
    ScopeGuard guard([shader]() { shader->stop_using(); });

    auto *inst = m_c->selection_info()->model_instance();
    if (!inst)
        return;

    double shift_z = m_c->selection_info()->print_object()->get_current_elevation();
    Transform3d trafo = inst->get_transformation().get_matrix();
    trafo.translation()(2) += shift_z;
    const Geometry::Transformation transformation{trafo};

    const Transform3d instance_scaling_matrix_inverse = transformation.get_scaling_factor_matrix().inverse();
    const Camera& camera = wxGetApp().plater()->get_camera();
    const Transform3d& view_matrix = camera.get_view_matrix();
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    const ColorRGBA selected_color = ColorRGBA::REDISH();
    const ColorRGBA hovered_color = ColorRGBA::CYAN();
    const ColorRGBA island_color = ColorRGBA::BLUEISH();
    const ColorRGBA inactive_color = ColorRGBA::LIGHT_GRAY();
    const ColorRGBA manual_color = ColorRGBA::ORANGE();

    ColorRGBA render_color;
    for (size_t i = 0; i < cache_size; ++i) {
        const sla::SupportPoint& support_point = m_editing_mode ? m_editing_cache[i].support_point : m_normal_cache[i];

        const bool clipped = is_mesh_point_clipped(support_point.pos.cast<double>());
        if (i < m_point_raycasters.size()) {
            m_point_raycasters[i].first->set_active(!clipped);
            m_point_raycasters[i].second->set_active(!clipped);
        }
        if (clipped)
            continue;

        render_color = 
            support_point.type == sla::SupportPointType::manual_add ? manual_color : 
            support_point.type == sla::SupportPointType::island ? island_color :
            inactive_color;
        // First decide about the color of the point.
        if (m_editing_mode) {
            if (size_t(m_hover_id) == i) // ignore hover state unless editing mode is active
                render_color = hovered_color;
            else if (m_editing_cache[i].selected)
                render_color = selected_color;
        }

        m_cone.model.set_color(render_color);
        m_sphere.model.set_color(render_color);
        shader->set_uniform("emission_factor", 0.5f);

        // Inverse matrix of the instance scaling is applied so that the mark does not scale with the object.
        const Transform3d support_matrix = Geometry::translation_transform(support_point.pos.cast<double>()) * instance_scaling_matrix_inverse;

        if (transformation.is_left_handed())
            glsafe(::glFrontFace(GL_CW));

        // Matrices set, we can render the point mark now.
        // If in editing mode, we'll also render a cone pointing to the sphere.
        if (m_editing_mode) {
            // in case the normal is not yet cached, find and cache it
            if (m_editing_cache[i].normal == Vec3f::Zero())
                m_c->raycaster()->raycaster()->get_closest_point(m_editing_cache[i].support_point.pos, &m_editing_cache[i].normal);

            Eigen::Quaterniond q;
            q.setFromTwoVectors(Vec3d::UnitZ(), instance_scaling_matrix_inverse * m_editing_cache[i].normal.cast<double>());
            const Eigen::AngleAxisd aa(q);
            const Transform3d model_matrix = transformation.get_matrix() * support_matrix * Transform3d(aa.toRotationMatrix()) *
                Geometry::translation_transform((CONE_HEIGHT + support_point.head_front_radius * RenderPointScale) * Vec3d::UnitZ()) *
                Geometry::rotation_transform({ double(PI), 0.0, 0.0 }) * Geometry::scale_transform({ CONE_RADIUS, CONE_RADIUS, CONE_HEIGHT });

            shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
            const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
            shader->set_uniform("view_normal_matrix", view_normal_matrix);
            m_cone.model.render();
        }

        const double radius = (double)support_point.head_front_radius * RenderPointScale;
        const Transform3d model_matrix = transformation.get_matrix() * support_matrix * Geometry::scale_transform(radius);
        shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
        const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
        shader->set_uniform("view_normal_matrix", view_normal_matrix);
        m_sphere.model.render();

        if (transformation.is_left_handed())
            glsafe(::glFrontFace(GL_CCW));
    }
}

bool GLGizmoSlaSupports::is_mesh_point_clipped(const Vec3d& point) const
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
bool GLGizmoSlaSupports::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    ModelObject* mo = m_c->selection_info()->model_object();
    int active_inst = m_c->selection_info()->get_active_instance();

    if (m_editing_mode) {

        // left down with shift - show the selection rectangle:
        if (action == SLAGizmoEventType::LeftDown && (shift_down || alt_down || control_down)) {
            if (m_hover_id == -1) {
                if (shift_down || alt_down) {
                    m_selection_rectangle.start_dragging(mouse_position, shift_down ? GLSelectionRectangle::EState::Select : GLSelectionRectangle::EState::Deselect);
                }
            }
            else {
                if (m_editing_cache[m_hover_id].selected)
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
                    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Add support point"));
                    m_editing_cache.emplace_back(sla::SupportPoint{pos_and_normal.first, m_new_point_head_diameter/2.f}, false, pos_and_normal.second);
                    m_parent.set_as_dirty();
                    m_wait_for_up_event = true;
                    unregister_point_raycasters_for_picking();
                    register_point_raycasters_for_picking();
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
            for (unsigned int i=0; i<m_editing_cache.size(); ++i)
                points.push_back(trafo.get_matrix() * m_editing_cache[i].support_point.pos.cast<double>());

            // Now ask the rectangle which of the points are inside.
            std::vector<Vec3f> points_inside;
            std::vector<unsigned int> points_idxs = m_selection_rectangle.contains(points);
            m_selection_rectangle.stop_dragging();
            for (size_t idx : points_idxs)
                points_inside.push_back(points[idx].cast<float>());

            // Only select/deselect points that are actually visible. We want to check not only
            // the point itself, but also the center of base of its cone, so the points don't hide
            // under every miniature irregularity on the model. Remember the actual number and
            // append the cone bases.
            size_t orig_pts_num = points_inside.size();
            for (size_t idx : points_idxs)
                points_inside.emplace_back((trafo.get_matrix().cast<float>() * (m_editing_cache[idx].support_point.pos + m_editing_cache[idx].normal)).cast<float>());

            for (size_t idx : m_c->raycaster()->raycaster()->get_unobscured_idxs(
                    trafo, wxGetApp().plater()->get_camera(), points_inside,
                     m_c->object_clipper()->get_clipping_plane()))
            {
                if (idx >= orig_pts_num) // this is a cone-base, get index of point it belongs to
                    idx -= orig_pts_num;
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

        if (action ==  SLAGizmoEventType::ApplyChanges) {
            editing_mode_apply_changes();
            return true;
        }

        if (action ==  SLAGizmoEventType::DiscardChanges) {
            ask_about_changes([this](){ editing_mode_apply_changes(); },
                                         [this](){ editing_mode_discard_changes(); });
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
    }

    if (!m_editing_mode) {
        if (action == SLAGizmoEventType::AutomaticGeneration) {
            auto_generate();
            return true;
        }

        if (action == SLAGizmoEventType::ManualEditing) {
            switch_to_editing_mode();
            return true;
        }
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

void GLGizmoSlaSupports::delete_selected_points(bool force)
{
    if (! m_editing_mode) {
        std::cout << "DEBUGGING: delete_selected_points called out of editing mode!" << std::endl;
        std::abort();
    }

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Delete support point"));

    for (unsigned int idx=0; idx<m_editing_cache.size(); ++idx) {
        if (m_editing_cache[idx].selected && (!m_editing_cache[idx].support_point.is_island() || !m_lock_unique_islands || force)) {
            m_editing_cache.erase(m_editing_cache.begin() + (idx--));
        }
    }

    unregister_point_raycasters_for_picking();
    register_point_raycasters_for_picking();
    select_point(NoPoints);
}

std::vector<const ConfigOption*> GLGizmoSlaSupports::get_config_options(const std::vector<std::string>& keys) const
{
    std::vector<const ConfigOption*> out;
    const ModelObject* mo = m_c->selection_info()->model_object();

    if (! mo)
        return out;

    const DynamicPrintConfig& object_cfg = mo->config.get();
    const DynamicPrintConfig& print_cfg = wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
    std::unique_ptr<DynamicPrintConfig> default_cfg = nullptr;

    for (const std::string& key : keys) {
        if (object_cfg.has(key))
            out.push_back(object_cfg.option(key));
        else
            if (print_cfg.has(key))
                out.push_back(print_cfg.option(key));
            else { // we must get it from defaults
                if (default_cfg == nullptr)
                    default_cfg.reset(DynamicPrintConfig::new_from_defaults_keys(keys));
                out.push_back(default_cfg->option(key));
            }
    }

    return out;
}

void GLGizmoSlaSupports::on_render_input_window(float x, float y, float bottom_limit)
{
    // Keep resolution of icons for 
    static float rendered_line_height;    
    if (float line_height = ImGui::GetTextLineHeightWithSpacing();
        m_icons.empty() ||
        rendered_line_height != line_height) { // change of view resolution
        rendered_line_height = line_height;

        // need regeneration when change resolution(move between monitors)
        float width = std::round(line_height / 8 + 1) * 8;
        ImVec2 icon_size{width, width};
        m_icons = init_icons(m_icon_manager, icon_size);
    }

    static float last_y = 0.0f;
    static float last_h = 0.0f;

    ModelObject* mo = m_c->selection_info()->model_object();

    if (! mo)
        return;

    bool first_run = true; // This is a hack to redraw the button when all points are removed,
                           // so it is not delayed until the background process finishes.
RENDER_AGAIN:
    //ImGuiPureWrap::set_next_window_pos(x, y, ImGuiCond_Always);
    //const ImVec2 window_size(m_imgui->scaled(18.f, 16.f));
    //ImGui::SetNextWindowPos(ImVec2(x, y - std::max(0.f, y+window_size.y-bottom_limit) ));
    //ImGui::SetNextWindowSize(ImVec2(window_size));

    ImGuiPureWrap::begin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    // adjust window position to avoid overlap the view toolbar
    float win_h = ImGui::GetWindowHeight();
    y = std::min(y, bottom_limit - win_h);
    ImGui::SetWindowPos(ImVec2(x, y), ImGuiCond_Always);
    if (last_h != win_h || last_y != y) {
        // ask canvas for another frame to render the window in the correct position
        m_imgui->set_requires_extra_frame();
        if (last_h != win_h)
            last_h = win_h;
        if (last_y != y)
            last_y = y;
    }

    // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:

    const float settings_sliders_left = ImGuiPureWrap::calc_text_size(m_desc.at("points_density")).x + m_imgui->scaled(1.f);
    const float clipping_slider_left = std::max(ImGuiPureWrap::calc_text_size(m_desc.at("clipping_of_view")).x, ImGuiPureWrap::calc_text_size(m_desc.at("reset_direction")).x) + m_imgui->scaled(1.5f);
    const float diameter_slider_left = ImGuiPureWrap::calc_text_size(m_desc.at("head_diameter")).x + m_imgui->scaled(1.f);
    const float minimal_slider_width = m_imgui->scaled(4.f);
    const float buttons_width_approx = ImGuiPureWrap::calc_text_size(m_desc.at("apply_changes")).x + ImGuiPureWrap::calc_text_size(m_desc.at("discard_changes")).x + m_imgui->scaled(1.5f);
    const float lock_supports_width_approx = ImGuiPureWrap::calc_text_size(m_desc.at("lock_supports")).x + m_imgui->scaled(2.f);

    float window_width = minimal_slider_width + std::max(std::max(settings_sliders_left, clipping_slider_left), diameter_slider_left);
    window_width = std::max(std::max(window_width, buttons_width_approx), lock_supports_width_approx);

    bool force_refresh = false;
    bool remove_selected = false;
    bool remove_all = false;

    if (m_editing_mode) {

        float diameter_upper_cap = static_cast<ConfigOptionFloat*>(wxGetApp().preset_bundle->sla_prints.get_edited_preset().config.option("support_pillar_diameter"))->value;
        if (m_new_point_head_diameter > diameter_upper_cap)
            m_new_point_head_diameter = diameter_upper_cap;
        ImGui::AlignTextToFramePadding();

        ImGuiPureWrap::text(m_desc.at("head_diameter"));
        ImGui::SameLine(diameter_slider_left);
        ImGui::PushItemWidth(window_width - diameter_slider_left);

        // Following is a nasty way to:
        //  - save the initial value of the slider before one starts messing with it
        //  - keep updating the head radius during sliding so it is continuosly refreshed in 3D scene
        //  - take correct undo/redo snapshot after the user is done with moving the slider
        float initial_value = m_new_point_head_diameter;
        m_imgui->slider_float("##head_diameter", &m_new_point_head_diameter, 0.1f, diameter_upper_cap, "%.1f");
        if (m_imgui->get_last_slider_status().clicked) {
            if (m_old_point_head_diameter == 0.f)
                m_old_point_head_diameter = initial_value;
        }
        if (m_imgui->get_last_slider_status().edited) {
            for (auto& cache_entry : m_editing_cache)
                if (cache_entry.selected)
                    cache_entry.support_point.head_front_radius = m_new_point_head_diameter / 2.f;
        }
        if (m_imgui->get_last_slider_status().deactivated_after_edit) {
            // momentarily restore the old value to take snapshot
            for (auto& cache_entry : m_editing_cache)
                if (cache_entry.selected)
                    cache_entry.support_point.head_front_radius = m_old_point_head_diameter / 2.f;
            float backup = m_new_point_head_diameter;
            m_new_point_head_diameter = m_old_point_head_diameter;
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Change point head diameter"));
            m_new_point_head_diameter = backup;
            for (auto& cache_entry : m_editing_cache)
                if (cache_entry.selected)
                    cache_entry.support_point.head_front_radius = m_new_point_head_diameter / 2.f;
            m_old_point_head_diameter = 0.f;
        }

        bool changed = m_lock_unique_islands;
        ImGuiPureWrap::checkbox(m_desc.at("lock_supports"), m_lock_unique_islands);
        force_refresh |= changed != m_lock_unique_islands;

        m_imgui->disabled_begin(m_selection_empty);
        remove_selected = ImGuiPureWrap::button(m_desc.at("remove_selected"));
        m_imgui->disabled_end();

        m_imgui->disabled_begin(m_editing_cache.empty());
        remove_all = ImGuiPureWrap::button(m_desc.at("remove_all"));
        m_imgui->disabled_end();

        ImGuiPureWrap::text(" "); // vertical gap

        if (ImGuiPureWrap::button(m_desc.at("apply_changes"))) {
            editing_mode_apply_changes();
            force_refresh = true;
        }
        ImGui::SameLine();
        bool discard_changes = ImGuiPureWrap::button(m_desc.at("discard_changes"));
        if (discard_changes) {
            editing_mode_discard_changes();
            force_refresh = true;
        }
    }
    else { // not in editing mode:
        m_imgui->disabled_begin(!is_input_enabled());
        ImGuiPureWrap::text(m_desc.at("points_density"));
        ImGui::SameLine();

        if (draw_view_mode(m_show_support_structure, m_icons)){
            show_sla_supports(m_show_support_structure);
            if (m_show_support_structure) {
                if (m_normal_cache.empty()) { 
                    // first click also have to generate point
                    auto_generate();
                } else {
                    reslice_until_step(slaposPad);
                }
            }
        }

        const char *support_points_density = "support_points_density_relative";
        float density = static_cast<const ConfigOptionInt*>(get_config_options({support_points_density})[0])->value; 
        float old_density = density;
        wxString tooltip = _L("Change amount of generated support points.");
        if (m_imgui->slider_float("##density", &density, 50.f, 200.f, "%.f %%", 1.f, false, tooltip)){
            if (density < 10.f) // not neccessary, but lower value seems pointless. Zero cause issues inside algorithms.
                density = 10.f;
            mo->config.set(support_points_density, (int) density);
        }
        
        const ImGuiWrapper::LastSliderStatus &density_status = m_imgui->get_last_slider_status();
        static std::optional<int> density_stash; // Value for undo/redo stack is written on stop dragging
        if (!density_stash.has_value() && !is_approx(density, old_density)) // stash the values of the settings so we know what to revert to after undo
            density_stash = (int)old_density;
        if (density_status.deactivated_after_edit && density_stash.has_value()) { //  slider released            
            // set configuration to value before slide
            // to store this value on undo redo snapshot stack
            mo->config.set(support_points_density, *density_stash);
            density_stash.reset();
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Support parameter change"));
            mo->config.set(support_points_density, (int) density);
            wxGetApp().obj_list()->update_and_show_object_settings_item();
            auto_generate();
        }
        
        const sla::SupportPoints &supports = m_normal_cache;
        int count_user_edited = 0;
        int count_island = 0;
        for (const sla::SupportPoint &support : supports)
            switch (support.type) { 
            case sla::SupportPointType::manual_add: ++count_user_edited; break;
            case sla::SupportPointType::island:     ++count_island;      break;
            //case sla::SupportPointType::slope:
            default: assert(support.type == sla::SupportPointType::slope); }

        std::string stats;
        if (supports.empty()) {
            stats = "No support points generated yet.";
        } else if (count_user_edited == 0) {
            stats = GUI::format("%d support points generated (%d on islands)",
                (int) supports.size(), count_island);
        } else {
            stats = GUI::format("%d(%d manual) support points (%d on islands)", 
                (int) supports.size(), count_user_edited, count_island);
        }
        ImVec4 light_gray{0.4f, 0.4f, 0.4f, 1.0f};
        ImGui::TextColored(light_gray, "%s", stats.c_str());

#ifdef USE_ISLAND_GUI_FOR_SETTINGS
        ImGui::Separator();
        ImGui::Text("Between delimiters is temporary GUI");
        sla::SampleConfig &sample_config = sla::SampleConfigFactory::get_sample_config();
        if (float overhang_sample_distance = sample_config.prepare_config.discretize_overhang_step;
            m_imgui->slider_float("overhang discretization", &overhang_sample_distance, 2e-5f, 10.f, "%.2f mm")){
            sample_config.prepare_config.discretize_overhang_step = overhang_sample_distance;
        } else if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Smaller will slow down. Step for discretization overhang outline for test of support need");        
        
        draw_island_config();
        ImGui::Text("Distribution depends on './resources/data/sla_support.svg'\ninstruction for edit are in file");
        ImGui::Separator();
#endif // USE_ISLAND_GUI_FOR_SETTINGS

        if (ImGuiPureWrap::button(m_desc.at("auto_generate")))
            auto_generate();
        ImGui::SameLine();

        m_imgui->disabled_begin(!is_input_enabled() || m_normal_cache.empty());
        remove_all = ImGuiPureWrap::button(m_desc.at("remove_all"));
        m_imgui->disabled_end();

        ImGui::Separator();
        if (ImGuiPureWrap::button(m_desc.at("manual_editing")))
            switch_to_editing_mode();

        m_imgui->disabled_end();

        // ImGuiPureWrap::text("");
        // ImGuiPureWrap::text(m_c->m_model_object->sla_points_status == sla::PointsStatus::NoPoints ? _(L("No points  (will be autogenerated)")) :
        //              (m_c->m_model_object->sla_points_status == sla::PointsStatus::AutoGenerated ? _(L("Autogenerated points (no modifications)")) :
        //              (m_c->m_model_object->sla_points_status == sla::PointsStatus::UserModified ? _(L("User-modified points")) :
        //              (m_c->m_model_object->sla_points_status == sla::PointsStatus::Generating ? _(L("Generation in progress...")) : "UNKNOWN STATUS"))));
    }


    // Following is rendered in both editing and non-editing mode:
    m_imgui->disabled_begin(!is_input_enabled());
    ImGui::Separator();
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

    ImGui::SameLine(clipping_slider_left);
    ImGui::PushItemWidth(window_width - clipping_slider_left);
    float clp_dist = m_c->object_clipper()->get_position();
    if (m_imgui->slider_float("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f"))
        m_c->object_clipper()->set_position_by_ratio(clp_dist, true);

    if (ImGuiPureWrap::button("?")) {
        wxGetApp().CallAfter([]() {
            SlaGizmoHelpDialog help_dlg;
            help_dlg.ShowModal();
        });
    }

    m_imgui->disabled_end();

    ImGuiPureWrap::end();

    if (remove_selected || remove_all) {
        force_refresh = false;
        m_parent.set_as_dirty();
        bool was_in_editing = m_editing_mode;
        if (! was_in_editing)
            switch_to_editing_mode();
        if (remove_all) {
            select_point(AllPoints);
            delete_selected_points(true); // true - delete regardless of locked status
        }
        if (remove_selected)
            delete_selected_points(false); // leave locked points
        if (! was_in_editing)
            editing_mode_apply_changes();

        if (first_run) {
            first_run = false;
            goto RENDER_AGAIN;
        }
    }

    if (force_refresh)
        m_parent.set_as_dirty();
}

#ifdef USE_ISLAND_GUI_FOR_SETTINGS
void GLGizmoSlaSupports::draw_island_config() {
    if (!ImGui::TreeNode("Support islands:"))
        return; // no need to draw configuration for islands
    sla::SampleConfig &sample_config = sla::SampleConfigFactory::get_sample_config();

    ImGui::SameLine();
    ImGui::Text("head radius %.2f mm", unscale<float>(sample_config.head_radius));

    bool exist_change = false;

    if (float max_for_one = unscale<float>(sample_config.max_length_for_one_support_point); // [in mm]
        ImGui::InputFloat("One support", &max_for_one, .1f, 1.f, "%.2f mm")) {
        sample_config.max_length_for_one_support_point = scale_(max_for_one);
        exist_change = true;
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Maximal island length (longest voronoi path)\n"
            "for support island by exactly one point.\n"
            "Point will be on the longest path center");

    if (float max_for_two = unscale<float>(sample_config.max_length_for_two_support_points); // [in mm]
        ImGui::InputFloat("Two supports", &max_for_two, .1f, 1.f, "%.2f mm")) {
        sample_config.max_length_for_two_support_points = scale_(max_for_two);
        exist_change = true;
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Maximal island length (longest voronoi path)\n"
            "for support by 2 points on path sides\n"
            "To stretch the island.");
    if (float thin_max_width = unscale<float>(sample_config.thin_max_width); // [in mm]
        ImGui::InputFloat("Thin max width", &thin_max_width, .1f, 1.f, "%.2f mm")) {
        sample_config.thin_max_width = scale_(thin_max_width);
        exist_change = true;
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Maximal width of line island supported in the middle of line\n"
            "Must be greater than thick min width(to make hysteresis)");
    if (float thick_min_width = unscale<float>(sample_config.thick_min_width); // [in mm]
        ImGui::InputFloat("Thick min width", &thick_min_width, .1f, 1.f, "%.2f mm")) {
        sample_config.thick_min_width = scale_(thick_min_width);
        exist_change = true;
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Minimal width to be supported by outline\n"
            "Must be smaller than thin max width(to make hysteresis)");
    if (float max_distance = unscale<float>(sample_config.thin_max_distance); // [in mm]
        ImGui::InputFloat("Thin max distance", &max_distance, .1f, 1.f, "%.2f mm")) {
        sample_config.thin_max_distance = scale_(max_distance);
        exist_change = true;
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Maximal distance of supports on thin island's part");
    if (float max_distance = unscale<float>(sample_config.thick_inner_max_distance); // [in mm]
        ImGui::InputFloat("Thick inner max distance", &max_distance, .1f, 1.f, "%.2f mm")) {
        sample_config.thick_inner_max_distance = scale_(max_distance);
        exist_change = true;
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Maximal distance of supports inside thick island's part");
    if (float max_distance = unscale<float>(sample_config.thick_outline_max_distance); // [in mm]
        ImGui::InputFloat("Thick outline max distance", &max_distance, .1f, 1.f, "%.2f mm")) {
        sample_config.thick_outline_max_distance = scale_(max_distance);
        exist_change = true;
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Maximal distance of supports on thick island's part outline");
    
    if (float minimal_distance_from_outline = unscale<float>(sample_config.minimal_distance_from_outline); // [in mm]
        ImGui::InputFloat("From outline", &minimal_distance_from_outline, .1f, 1.f, "%.2f mm")) {
        sample_config.minimal_distance_from_outline = scale_(minimal_distance_from_outline);
        exist_change = true;
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When it is possible, there will be this minimal distance from outline.\n"
            "ZERO mean head center will lay on island outline\n"
            "IMHO value should be bigger than head radius");
    ImGui::SameLine();
    if (float maximal_distance_from_outline = unscale<float>(sample_config.maximal_distance_from_outline); // [in mm]
        ImGui::InputFloat("Max", &maximal_distance_from_outline, .1f, 1.f, "%.2f mm")) {
        sample_config.maximal_distance_from_outline = scale_(maximal_distance_from_outline);
        exist_change = true;
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Measured as sum of VD edge length from outline\n"
            "Used only when there is no space for outline offset on first/last point\n"
            "Must be bigger than value 'From outline'");

    if (float simplification_tolerance = unscale<float>(sample_config.simplification_tolerance); // [in mm]
        ImGui::InputFloat("Simplify", &simplification_tolerance, .1f, 1.f, "%.2f mm")) {
        sample_config.simplification_tolerance = scale_(simplification_tolerance);
        exist_change = true;
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("There is no need to calculate with precisse island Voronoi\n" 
            "NOTE: Slice of Cylinder bottom has tip of trinagles on contour\n"
            "(neighbor coordinate -> create issue in boost::voronoi)\n"
            "Bigger value will speed up");
    ImGui::Text("Aligning termination criteria:");
    if (ImGui::IsItemHovered()) 
        ImGui::SetTooltip("After initial support placement on island, supports are aligned\n"
                          "to more uniformly support area of irregular island shape");
    if (int count = static_cast<int>(sample_config.count_iteration);
        ImGui::SliderInt("max iteration", &count, 0, 100, "%d loops" )){
        sample_config.count_iteration = count;
        exist_change = true;
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Align termination condition, max count of aligning calls");
    if (float minimal_move = unscale<float>(sample_config.minimal_move); // [in mm]
        ImGui::InputFloat("minimal move", &minimal_move, .1f, 1.f, "%.2f mm")) {
        sample_config.minimal_move = scale_(minimal_move);
        exist_change = true;
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Align termination condition, when support points after align did not change their position more,\n"
            "than this distance it is deduce that supports are aligned enough.\n"
            "Bigger value mean speed up of aligning");    

    if (exist_change){
        sla::SampleConfigFactory::verify(sample_config);
    }


#ifdef OPTION_TO_STORE_ISLAND
    bool store_islands = !sample_config.path.empty();
    if (ImGui::Checkbox("StoreIslands", &store_islands)) {
        if (store_islands == true)
            sample_config.path = "C:/data/temp/island<<order>>.svg";
        else
            sample_config.path.clear();
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Store islands into files\n<<order>> is replaced by island order number");
    if (store_islands) {
        ImGui::SameLine();
        std::string path;
        ImGui::InputText("path", &sample_config.path);
    }
#endif // OPTION_TO_STORE_ISLAND

    // end of tree node
    ImGui::TreePop();
}
#endif // USE_ISLAND_GUI_FOR_SETTINGS

bool GLGizmoSlaSupports::on_is_activable() const
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

bool GLGizmoSlaSupports::on_is_selectable() const
{
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA);
}

std::string GLGizmoSlaSupports::on_get_name() const
{
    return _u8L("SLA Support Points");
}

bool GLGizmoSlaSupports::ask_about_changes(std::function<void()> on_yes, std::function<void()> on_no)
{
    MessageDialog dlg(GUI::wxGetApp().mainframe, _L("Do you want to save your manually edited support points?") + "\n",
                      _L("Save support points?"), wxICON_QUESTION | wxYES | wxNO | wxCANCEL );

    const int ret = dlg.ShowModal();
    if (ret == wxID_YES)
        on_yes();
    else if (ret == wxID_NO)
        on_no();
    else
        return false;

    return true;
}

void GLGizmoSlaSupports::on_set_state()
{
    if (m_state == On) { // the gizmo was just turned on
        
        // Make sure that current object is on current bed. Refuse to turn on otherwise.
        if (! selected_print_object_exists(m_parent, _L("Selected object has to be on the active bed."))) {
            m_state = Off;
            return;
        }
        
        // Set default head diameter from config.
        const DynamicPrintConfig& cfg = wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
        m_new_point_head_diameter = static_cast<const ConfigOptionFloat*>(cfg.option("support_head_front_diameter"))->value;
    }
    else {
        if (m_editing_mode && unsaved_changes() && on_is_activable()) {
            if (!ask_about_changes([this]() { editing_mode_apply_changes(); },
                [this]() { editing_mode_discard_changes(); })) {
                m_state = On;
                return;
            }
        }
        else {
            // we are actually shutting down
            disable_editing_mode(); // so it is not active next time the gizmo opens
            m_old_mo_id = -1;
        }

        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_FORCE_UPDATE));
        m_c->instances_hider()->set_hide_full_scene(false);
        m_c->selection_info()->set_use_shift(false); // see top of on_render for details

    }
}

void GLGizmoSlaSupports::on_start_dragging()
{
    if (m_hover_id != -1) {
        select_point(NoPoints);
        select_point(m_hover_id);
        m_point_before_drag = m_editing_cache[m_hover_id];
    }
    else
        m_point_before_drag = CacheEntry();
}


void GLGizmoSlaSupports::on_stop_dragging()
{
    if (m_hover_id != -1) {
        CacheEntry backup = m_editing_cache[m_hover_id];

        if (m_point_before_drag.support_point.pos != Vec3f::Zero() // some point was touched
         && backup.support_point.pos != m_point_before_drag.support_point.pos) // and it was moved, not just selected
        {
            m_editing_cache[m_hover_id] = m_point_before_drag;
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Move support point"));
            m_editing_cache[m_hover_id] = backup;
        }
    }
    m_point_before_drag = CacheEntry();
}

void GLGizmoSlaSupports::on_dragging(const UpdateData &data)
{
    assert(m_hover_id != -1);
    if (!m_editing_mode) return;
    if (m_editing_cache[m_hover_id].support_point.is_island() && m_lock_unique_islands)
        return;
    
    std::pair<Vec3f, Vec3f> pos_and_normal;
    if (!unproject_on_mesh(data.mouse_pos.cast<double>(), pos_and_normal))
        return;

    m_editing_cache[m_hover_id].support_point.pos = pos_and_normal.first;
    m_editing_cache[m_hover_id].support_point.type = sla::SupportPointType::manual_add;
    m_editing_cache[m_hover_id].normal = pos_and_normal.second;        
}

void GLGizmoSlaSupports::on_load(cereal::BinaryInputArchive& ar)
{
    ar(m_new_point_head_diameter,
       m_normal_cache,
       m_editing_cache,
       m_selection_empty
    );
}



void GLGizmoSlaSupports::on_save(cereal::BinaryOutputArchive& ar) const
{
    ar(m_new_point_head_diameter,
       m_normal_cache,
       m_editing_cache,
       m_selection_empty
    );
}



void GLGizmoSlaSupports::select_point(int i)
{
    if (! m_editing_mode) {
        std::cout << "DEBUGGING: select_point called when out of editing mode!" << std::endl;
        std::abort();
    }

    if (i == AllPoints || i == NoPoints) {
        for (auto& point_and_selection : m_editing_cache)
            point_and_selection.selected = ( i == AllPoints );
        m_selection_empty = (i == NoPoints);

        if (i == AllPoints && !m_editing_cache.empty())
            m_new_point_head_diameter = m_editing_cache[0].support_point.head_front_radius * 2.f;
    }
    else {
        m_editing_cache[i].selected = true;
        m_selection_empty = false;
        m_new_point_head_diameter = m_editing_cache[i].support_point.head_front_radius * 2.f;
    }
}


void GLGizmoSlaSupports::unselect_point(int i)
{
    if (! m_editing_mode) {
        std::cout << "DEBUGGING: unselect_point called when out of editing mode!" << std::endl;
        std::abort();
    }

    m_editing_cache[i].selected = false;
    m_selection_empty = true;
    for (const CacheEntry& ce : m_editing_cache) {
        if (ce.selected) {
            m_selection_empty = false;
            break;
        }
    }
}




void GLGizmoSlaSupports::editing_mode_discard_changes()
{
    if (! m_editing_mode) {
        std::cout << "DEBUGGING: editing_mode_discard_changes called when out of editing mode!" << std::endl;
        std::abort();
    }
    select_point(NoPoints);
    disable_editing_mode();
}



void GLGizmoSlaSupports::editing_mode_apply_changes()
{
    // If there are no changes, don't touch the front-end. The data in the cache could have been
    // taken from the backend and copying them to ModelObject would needlessly invalidate them.
    disable_editing_mode(); // this leaves the editing mode undo/redo stack and must be done before the snapshot is taken

    if (unsaved_changes()) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Support points edit"));

        m_normal_cache.clear();
        for (const CacheEntry& ce : m_editing_cache)
            m_normal_cache.push_back(ce.support_point);

        ModelObject* mo = m_c->selection_info()->model_object();
        mo->sla_points_status = sla::PointsStatus::UserModified;
        mo->sla_support_points.clear();
        mo->sla_support_points = m_normal_cache;

        reslice_until_step(m_show_support_structure ? slaposPad : slaposSupportPoints);
    }
}



void GLGizmoSlaSupports::reload_cache()
{
    const ModelObject* mo = m_c->selection_info()->model_object();
    m_normal_cache.clear();
    if (mo->sla_points_status == sla::PointsStatus::AutoGenerated || mo->sla_points_status == sla::PointsStatus::Generating)
        get_data_from_backend();
    else
        for (const sla::SupportPoint& point : mo->sla_support_points)
            m_normal_cache.emplace_back(point);
}


bool GLGizmoSlaSupports::has_backend_supports() const
{
    const ModelObject* mo = m_c->selection_info()->model_object();
    if (! mo)
        return false;

    // find SlaPrintObject with this ID
    for (const SLAPrintObject* po : m_parent.sla_print()->objects()) {
        if (po->model_object()->id() == mo->id())
        	return po->is_step_done(slaposSupportPoints);
    }
    return false;
}

bool GLGizmoSlaSupports::on_mouse(const wxMouseEvent &mouse_event)
{
    if (!is_input_enabled()) return true;
    if (mouse_event.Moving()) return false;
    if (!mouse_event.ShiftDown() && !mouse_event.AltDown() 
        && use_grabbers(mouse_event)) return true;

    // wxCoord == int --> wx/types.h
    Vec2i mouse_coord(mouse_event.GetX(), mouse_event.GetY());
    Vec2d mouse_pos = mouse_coord.cast<double>();

    static bool pending_right_up = false;        
    if (mouse_event.LeftDown()) {
        bool grabber_contains_mouse = (get_hover_id() != -1);
        bool control_down = mouse_event.CmdDown();
        if ((!control_down || grabber_contains_mouse) &&
            gizmo_event(SLAGizmoEventType::LeftDown, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), false))
        return true;
    } else if (mouse_event.Dragging()) {
        bool control_down = mouse_event.CmdDown();
        if (m_parent.get_move_volume_id() != -1) {
            // don't allow dragging objects with the Sla gizmo on
            return true;
        } else if (!control_down &&
                gizmo_event(SLAGizmoEventType::Dragging, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), false)) {
            // the gizmo got the event and took some action, no need to do
            // anything more here
            m_parent.set_as_dirty();
            return true;
        } else if (control_down && (mouse_event.LeftIsDown() || mouse_event.RightIsDown())){
            // CTRL has been pressed while already dragging -> stop current action
            if (mouse_event.LeftIsDown())
                gizmo_event(SLAGizmoEventType::LeftUp, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), true);
            else if (mouse_event.RightIsDown())
                pending_right_up = false;
        }
    } else if (mouse_event.LeftUp() && !m_parent.is_mouse_dragging()) {
        // in case SLA/FDM gizmo is selected, we just pass the LeftUp event
        // and stop processing - neither object moving or selecting is
        // suppressed in that case
        gizmo_event(SLAGizmoEventType::LeftUp, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), mouse_event.CmdDown());
        return true;
    }else if (mouse_event.RightDown()){
        if (m_parent.get_selection().get_object_idx() != -1 &&
            gizmo_event(SLAGizmoEventType::RightDown, mouse_pos, false, false, false)) {
            // we need to set the following right up as processed to avoid showing
            // the context menu if the user release the mouse over the object
            pending_right_up = true;
            // event was taken care of by the SlaSupports gizmo
            return true;
        }
    } else if (pending_right_up && mouse_event.RightUp()) {
        pending_right_up = false;
        return true;
    }
    return false;
}

void GLGizmoSlaSupports::get_data_from_backend()
{
    if (! has_backend_supports())
        return;
    ModelObject* mo = m_c->selection_info()->model_object();

    // find the respective SLAPrintObject, we need a pointer to it
    for (const SLAPrintObject* po : m_parent.sla_print()->objects()) {
        if (po->model_object()->id() == mo->id()) {
            m_normal_cache.clear();
            
            auto mat = po->trafo().inverse().cast<float>(); // TODO: WTF trafo????? !!!!!!
            for (const sla::SupportPoint &p : po->get_support_points())
                m_normal_cache.emplace_back(sla::SupportPoint{mat * p.pos, p.head_front_radius, p.type});

            mo->sla_points_status = sla::PointsStatus::AutoGenerated;
            break;
        }
    }

    // We don't copy the data into ModelObject, as this would stop the background processing.
}



void GLGizmoSlaSupports::auto_generate()
{
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Autogenerate support points"));
    wxGetApp().CallAfter([this]() { reslice_until_step(
        m_show_support_structure ? slaposPad : slaposSupportPoints); });
    ModelObject* mo = m_c->selection_info()->model_object();
    mo->sla_points_status = sla::PointsStatus::Generating;
}

void GLGizmoSlaSupports::switch_to_editing_mode()
{
    wxGetApp().plater()->enter_gizmos_stack();
    m_editing_mode = true;
    show_sla_supports(false);
    m_editing_cache.clear();
    for (const sla::SupportPoint& sp : m_normal_cache)
        m_editing_cache.emplace_back(sp);
    select_point(NoPoints);
    register_point_raycasters_for_picking();
    m_parent.set_as_dirty();
}


void GLGizmoSlaSupports::disable_editing_mode()
{
    if (m_editing_mode) {
        m_editing_mode = false;
        show_sla_supports(m_show_support_structure);
        wxGetApp().plater()->leave_gizmos_stack();
        m_parent.set_as_dirty();
        unregister_point_raycasters_for_picking();
    }
    wxGetApp().plater()->get_notification_manager()->close_notification_of_type(NotificationType::QuitSLAManualMode);
}



bool GLGizmoSlaSupports::unsaved_changes() const
{
    if (m_editing_cache.size() != m_normal_cache.size())
        return true;

    for (size_t i=0; i<m_editing_cache.size(); ++i)
        if (m_editing_cache[i].support_point != m_normal_cache[i])
            return true;

    return false;
}

void GLGizmoSlaSupports::register_point_raycasters_for_picking()
{
    assert(m_point_raycasters.empty());

    if (m_editing_mode && !m_editing_cache.empty()) {
        for (size_t i = 0; i < m_editing_cache.size(); ++i) {
            m_point_raycasters.emplace_back(m_parent.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, i, *m_sphere.mesh_raycaster, Transform3d::Identity()),
                m_parent.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, i, *m_cone.mesh_raycaster, Transform3d::Identity()));
        }
        update_point_raycasters_for_picking_transform();
    }
}

void GLGizmoSlaSupports::unregister_point_raycasters_for_picking()
{
    for (size_t i = 0; i < m_point_raycasters.size(); ++i) {
        m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo, i);
    }
    m_point_raycasters.clear();
}

void GLGizmoSlaSupports::update_point_raycasters_for_picking_transform()
{
    if (m_editing_cache.empty())
        return;

    assert(!m_point_raycasters.empty());

    const GLVolume* vol = m_parent.get_selection().get_first_volume();
    Geometry::Transformation transformation(vol->world_matrix());

    auto *inst = m_c->selection_info()->model_instance();
    if (inst && m_c->selection_info() && m_c->selection_info()->print_object()) {
        double shift_z = m_c->selection_info()->print_object()->get_current_elevation();
        auto trafo = inst->get_transformation().get_matrix();
        trafo.translation()(2) += shift_z;
        transformation.set_matrix(trafo);
    }

    const Transform3d instance_scaling_matrix_inverse = transformation.get_scaling_factor_matrix().inverse();
    for (size_t i = 0; i < m_editing_cache.size(); ++i) {
        const Transform3d support_matrix = Geometry::translation_transform(m_editing_cache[i].support_point.pos.cast<double>()) * instance_scaling_matrix_inverse;

        if (m_editing_cache[i].normal == Vec3f::Zero())
            m_c->raycaster()->raycaster()->get_closest_point(m_editing_cache[i].support_point.pos, &m_editing_cache[i].normal);

        Eigen::Quaterniond q;
        q.setFromTwoVectors(Vec3d::UnitZ(), instance_scaling_matrix_inverse * m_editing_cache[i].normal.cast<double>());
        const Eigen::AngleAxisd aa(q);
        const Transform3d cone_matrix = transformation.get_matrix() * support_matrix * Transform3d(aa.toRotationMatrix()) *
            Geometry::assemble_transform((CONE_HEIGHT + m_editing_cache[i].support_point.head_front_radius * RenderPointScale) * Vec3d::UnitZ(),
                Vec3d(PI, 0.0, 0.0), Vec3d(CONE_RADIUS, CONE_RADIUS, CONE_HEIGHT));
        m_point_raycasters[i].second->set_transform(cone_matrix);

        const double radius = (double)m_editing_cache[i].support_point.head_front_radius * RenderPointScale;
        const Transform3d sphere_matrix = transformation.get_matrix() * support_matrix * Geometry::scale_transform(radius);
        m_point_raycasters[i].first->set_transform(sphere_matrix);
    }
}

SlaGizmoHelpDialog::SlaGizmoHelpDialog()
: wxDialog(nullptr, wxID_ANY, _L("SLA gizmo keyboard shortcuts"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER)
{
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    const wxString ctrl = GUI::shortkey_ctrl_prefix();
    const wxString alt  = GUI::shortkey_alt_prefix();


    // fonts
    const wxFont& font = wxGetApp().small_font();
    const wxFont& bold_font = wxGetApp().bold_font();

    auto note_text = new wxStaticText(this, wxID_ANY, _L("Note: some shortcuts work in (non)editing mode only."));
    note_text->SetFont(font);

    auto vsizer    = new wxBoxSizer(wxVERTICAL);
    auto gridsizer = new wxFlexGridSizer(2, 5, 15);
    auto hsizer    = new wxBoxSizer(wxHORIZONTAL);

    hsizer->AddSpacer(20);
    hsizer->Add(vsizer);
    hsizer->AddSpacer(20);

    vsizer->AddSpacer(20);
    vsizer->Add(note_text, 1, wxALIGN_CENTRE_HORIZONTAL);
    vsizer->AddSpacer(20);
    vsizer->Add(gridsizer);
    vsizer->AddSpacer(20);

    std::vector<std::pair<wxString, wxString>> shortcuts;
    shortcuts.push_back(std::make_pair(_L("Left click"),              _L("Add point")));
    shortcuts.push_back(std::make_pair(_L("Right click"),             _L("Remove point")));
    shortcuts.push_back(std::make_pair(_L("Drag"),                    _L("Move point")));
    shortcuts.push_back(std::make_pair(ctrl+_L("Left click"),         _L("Add point to selection")));
    shortcuts.push_back(std::make_pair(alt+_L("Left click"),          _L("Remove point from selection")));
    shortcuts.push_back(std::make_pair(wxString("Shift+")+_L("Drag"), _L("Select by rectangle")));
    shortcuts.push_back(std::make_pair(alt+_(L("Drag")),              _L("Deselect by rectangle")));
    shortcuts.push_back(std::make_pair(ctrl+"A",                      _L("Select all points")));
    shortcuts.push_back(std::make_pair("Delete",                      _L("Remove selected points")));
    shortcuts.push_back(std::make_pair(ctrl+_L("Mouse wheel"),        _L("Move clipping plane")));
    shortcuts.push_back(std::make_pair("R",                           _L("Reset clipping plane")));
    shortcuts.push_back(std::make_pair("Enter",                       _L("Apply changes")));
    shortcuts.push_back(std::make_pair("Esc",                         _L("Discard changes")));
    shortcuts.push_back(std::make_pair("M",                           _L("Switch to editing mode")));
    shortcuts.push_back(std::make_pair("A",                           _L("Auto-generate points")));

    for (const auto& pair : shortcuts) {
        auto shortcut = new wxStaticText(this, wxID_ANY, pair.first);
        auto desc = new wxStaticText(this, wxID_ANY, pair.second);
        shortcut->SetFont(bold_font);
        desc->SetFont(font);
        gridsizer->Add(shortcut, -1, wxALIGN_CENTRE_VERTICAL);
        gridsizer->Add(desc, -1, wxALIGN_CENTRE_VERTICAL);
    }

    std::vector<std::pair<std::string, wxString>> point_types;
    point_types.push_back(std::make_pair("sphere_lightgray",_L("Generated support point")));
    point_types.push_back(std::make_pair("sphere_redish",   _L("Selected support point")));
    point_types.push_back(std::make_pair("sphere_orange",   _L("Edited support point")));
    point_types.push_back(std::make_pair("sphere_blueish",  _L("Island support point")));
    point_types.push_back(std::make_pair("sphere_cyan",     _L("Hovered support point")));
    for (const auto &[icon_name, description] : point_types) {
        auto desc = new wxStaticText(this, wxID_ANY, description);
        desc->SetFont(font);
        gridsizer->Add(new wxStaticBitmap(this, wxID_ANY, ScalableBitmap(this, icon_name).bmp()),
            -1, wxALIGN_CENTRE_VERTICAL);
        gridsizer->Add(desc, -1, wxALIGN_CENTRE_VERTICAL);
    }

    SetSizer(hsizer);
    hsizer->SetSizeHints(this);
}
