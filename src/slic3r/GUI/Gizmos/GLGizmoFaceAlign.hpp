///|/ Copyright (c) Prusa Research 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GLGizmoFaceAlign_hpp_
#define slic3r_GLGizmoFaceAlign_hpp_

#include "GLGizmoBase.hpp"
#include "GLGizmoFaceAlignTypes.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <memory>
#include <utility>
#include <vector>

class wxMouseEvent;

namespace Slic3r {
namespace GUI {

// Select exactly two instances, activate the gizmo, then pick a source face and a destination face
// (convex-hull patches, same visualization as "Place on face").
class GLGizmoFaceAlign : public GLGizmoBase
{
    enum class Step : unsigned char
    {
        PickSourceFace,
        PickTargetFace,
    };

    struct FacePick
    {
        int   object_idx{ -1 };
        int   instance_idx{ -1 };
        Vec3d face_center_world{ Vec3d::Zero() };
        Vec3d normal_world{ Vec3d::UnitZ() };
        bool  valid{ false };
    };

    Step     m_step{ Step::PickSourceFace };
    FacePick m_source;
    FacePick m_target;

    std::vector<FaceAlignPlaneData>                   m_planes;
    std::vector<std::shared_ptr<SceneRaycasterItem>> m_planes_casters;

    std::pair<int, int> m_cached_inst_a{ -1, -1 };
    std::pair<int, int> m_cached_inst_b{ -1, -1 };
    std::vector<Transform3d> m_vol_mats_a;
    std::vector<Transform3d> m_vol_mats_b;
    Transform3d              m_inst_tf_a{ Transform3d::Identity() };
    Transform3d              m_inst_tf_b{ Transform3d::Identity() };

    void reset_picks();
    void clear_plane_geometry();
    void update_planes(const std::pair<int, int>& ia, const std::pair<int, int>& ib);
    bool is_plane_update_necessary(const std::pair<int, int>& ia, const std::pair<int, int>& ib) const;
    void pick_from_hovered_plane(FacePick& out) const;
    bool compute_alignment_delta(const FacePick& src, const FacePick& tgt, Transform3d& out_delta) const;
    void apply_alignment();

public:
    GLGizmoFaceAlign(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

protected:
    bool on_init() override;
    std::string on_get_name() const override;
    bool on_is_activable() const override;
    CommonGizmosDataID on_get_requirements() const override;
    void on_render() override;
    void on_set_state() override;
    bool on_mouse(const wxMouseEvent& mouse_event) override;
    void data_changed(bool is_serializing) override;
    void on_render_input_window(float x, float y, float bottom_limit) override;
    std::string get_tooltip() const override;

    void on_register_raycasters_for_picking() override;
    void on_unregister_raycasters_for_picking() override;
    void on_set_hover_id() override;

    bool wants_enter_leave_snapshots() const override { return true; }
    std::string get_gizmo_entering_text() const override { return _u8L("Entering Align faces"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving Align faces"); }
    std::string get_action_snapshot_name() const override { return _u8L("Align faces"); }
};

} // namespace GUI
} // namespace Slic3r

#endif
