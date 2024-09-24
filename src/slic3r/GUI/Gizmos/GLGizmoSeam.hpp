///|/ Copyright (c) Prusa Research 2019 - 2023 Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GLGizmoSeam_hpp_
#define slic3r_GLGizmoSeam_hpp_

#include "GLGizmoPainterBase.hpp"

#include "slic3r/GUI/I18N.hpp"

namespace Slic3r::GUI {

class GLGizmoSeam : public GLGizmoPainterBase
{
public:
    GLGizmoSeam(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
        : GLGizmoPainterBase(parent, icon_filename, sprite_id) {}

    void render_painter_gizmo() override;

protected:
    void on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;
    PainterGizmoType get_painter_type() const override;

    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    std::string get_gizmo_entering_text() const override { return _u8L("Entering Seam painting"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving Seam painting"); }
    std::string get_action_snapshot_name() const override { return _u8L("Paint-on seam editing"); }

private:
    bool on_init() override;

    void update_model_object() const override;
    void update_from_model_object() override;

    void on_opening() override {}
    void on_shutdown() override;

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, std::string> m_desc;
};



} // namespace Slic3r::GUI


#endif // slic3r_GLGizmoSeam_hpp_
