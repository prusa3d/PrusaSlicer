///|/ Copyright (c) Prusa Research 2022 - 2023 Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GLGizmoSlaBase_hpp_
#define slic3r_GLGizmoSlaBase_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/SceneRaycaster.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/Point.hpp"

#include <vector>
#include <string>
#include <memory>

namespace Slic3r {

class SLAPrint;

namespace GUI {

class GLCanvas3D;

class GLGizmoSlaBase : public GLGizmoBase
{
public:
    GLGizmoSlaBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, SLAPrintObjectStep min_step);

    void reslice_until_step(SLAPrintObjectStep step, bool postpone_error_messages = false);

protected:
    virtual CommonGizmosDataID on_get_requirements() const override;

    void update_volumes();
    void render_volumes();

    void register_volume_raycasters_for_picking();
    void unregister_volume_raycasters_for_picking();

    bool is_input_enabled() const { return m_input_enabled; }
    int get_min_sla_print_object_step() const { return m_min_sla_print_object_step; }

    bool unproject_on_mesh(const Vec2d& mouse_pos, std::pair<Vec3f, Vec3f>& pos_and_normal);

    bool are_sla_supports_shown() const { return m_show_sla_supports; }
    void show_sla_supports(bool show) { m_show_sla_supports = show; }

    const GLVolumeCollection &volumes() const { return m_volumes; }

    static bool selected_print_object_exists(const GLCanvas3D& canvas, const wxString& text);

private:
    GLVolumeCollection m_volumes;
    bool m_input_enabled{ false };
    bool m_show_sla_supports{ false };
    int m_min_sla_print_object_step{ -1 };
    std::vector<std::shared_ptr<SceneRaycasterItem>> m_volume_raycasters;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoSlaBase_hpp_
