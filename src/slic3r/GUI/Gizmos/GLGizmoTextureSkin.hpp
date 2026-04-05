///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GLGizmoTextureSkin_hpp_
#define slic3r_GLGizmoTextureSkin_hpp_

#include "GLGizmoPainterBase.hpp"

#include "slic3r/GUI/I18N.hpp"

namespace Slic3r::GUI {

class GLGizmoTextureSkin : public GLGizmoPainterBase
{
public:
    GLGizmoTextureSkin(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id) : GLGizmoPainterBase(parent, icon_filename, sprite_id) {}

    void render_painter_gizmo() override;

protected:
    void        on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;

    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    std::string get_gizmo_entering_text() const override { return _u8L("Entering Paint-on texture skin"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving Paint-on texture skin"); }
    std::string get_action_snapshot_name() const override { return _u8L("Paint-on texture skin editing"); }

    TriangleStateType get_left_button_state_type() const override { return TriangleStateType::TEXTURE_SKIN; }
    TriangleStateType get_right_button_state_type() const override { return TriangleStateType::NONE; }

private:
    bool on_init() override;

    void update_model_object() const override;
    void update_from_model_object() override;

    void             on_opening() override {}
    void             on_shutdown() override;
    PainterGizmoType get_painter_type() const override;

    std::map<std::string, std::string> m_desc;
};

} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoTextureSkin_hpp_
