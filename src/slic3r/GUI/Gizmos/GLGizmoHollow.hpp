///|/ Copyright (c) Prusa Research 2019 - 2023 Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Oleksandra Iushchenko @YuSanka, Filip Sykala @Jony01, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GLGizmoHollow_hpp_
#define slic3r_GLGizmoHollow_hpp_

#include "GLGizmoSlaBase.hpp"
#include "slic3r/GUI/GLSelectionRectangle.hpp"

#include <libslic3r/SLA/Hollowing.hpp>
#include <libslic3r/ObjectID.hpp>
#include <wx/dialog.h>

#include <cereal/types/vector.hpp>


namespace Slic3r {

class ConfigOption;
class ConfigOptionDef;

namespace GUI {

enum class SLAGizmoEventType : unsigned char;
class Selection;
class GLGizmoHollow : public GLGizmoSlaBase
{
public:
    GLGizmoHollow(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    void data_changed(bool is_serializing) override;
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);
    void delete_selected_points();    
    bool is_selection_rectangle_dragging() const override {
        return m_selection_rectangle.is_dragging();
    }
        
    /// <summary>
    /// Postpone to Grabber for move
    /// Detect move of object by dragging
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information otherwise False.</returns>
    bool on_mouse(const wxMouseEvent &mouse_event) override;

protected:
    bool on_init() override;
    void on_render() override;
    virtual void on_register_raycasters_for_picking() override;
    virtual void on_unregister_raycasters_for_picking() override;

private:
    void render_points(const Selection& selection);
    void register_hole_raycasters_for_picking();
    void unregister_hole_raycasters_for_picking();
    void update_hole_raycasters_for_picking_transform();

    ObjectID m_old_mo_id = -1;

    PickingModel m_cylinder;
    std::vector<std::shared_ptr<SceneRaycasterItem>> m_hole_raycasters;

    float m_new_hole_radius = 2.f;        // Size of a new hole.
    float m_new_hole_height = 6.f;
    mutable std::vector<bool> m_selected; // which holes are currently selected

    bool m_enable_hollowing = true;

    // Stashes to keep data for undo redo. Is taken after the editing
    // is done, the data are updated continuously.
    float m_offset_stash = 3.0f;
    float m_quality_stash = 0.5f;
    float m_closing_d_stash = 2.f;
    Vec3f m_hole_before_drag = Vec3f::Zero();
    sla::DrainHoles m_holes_in_drilled_mesh;

    sla::DrainHoles m_holes_stash;
    
    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, std::string> m_desc;

    GLSelectionRectangle m_selection_rectangle;

    bool m_wait_for_up_event = false;
    bool m_selection_empty = true;
    EState m_old_state = Off; // to be able to see that the gizmo has just been closed (see on_set_state)

    std::vector<std::pair<const ConfigOption*, const ConfigOptionDef*>> get_config_options(const std::vector<std::string>& keys) const;
    bool is_mesh_point_clipped(const Vec3d& point) const;

    // Methods that do the model_object and editing cache synchronization,
    // editing mode selection, etc:
    enum {
        AllPoints = -2,
        NoPoints,
    };
    void select_point(int i);
    void unselect_point(int i);
    void reload_cache();

protected:
    void on_set_state() override;
    void on_set_hover_id() override;
    void on_start_dragging() override;
    void on_stop_dragging() override;
    void on_dragging(const UpdateData &data) override;
    void on_render_input_window(float x, float y, float bottom_limit) override;

    std::string on_get_name() const override;
    bool on_is_activable() const override;
    bool on_is_selectable() const override;
    void on_load(cereal::BinaryInputArchive& ar) override;
    void on_save(cereal::BinaryOutputArchive& ar) const override;

    void init_cylinder_model();
};



} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoHollow_hpp_
