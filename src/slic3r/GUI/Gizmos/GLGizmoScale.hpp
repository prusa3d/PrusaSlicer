///|/ Copyright (c) Prusa Research 2019 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Filip Sykala @Jony01
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GLGizmoScale_hpp_
#define slic3r_GLGizmoScale_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/Selection.hpp"

namespace Slic3r {
namespace GUI {

class Selection;

class GLGizmoScale3D : public GLGizmoBase
{
    static const double Offset;

    struct AdjacentVolumes
    {
        Selection::IndicesList    volumes[3][2]; // [X/Y/Z][Max/Min]
    };
    struct StartingData
    {
        bool ctrl_down{ false };
        Vec3d scale{ Vec3d::Ones() };
        Vec3d drag_position{ Vec3d::Zero() };
        Vec3d center{ Vec3d::Zero() };
        Vec3d instance_center{ Vec3d::Zero() };
        Vec3d constraint_position{ Vec3d::Zero() };
        BoundingBoxf3 box;
        AdjacentVolumes adjacent_volumes;
    };

    // Indicates which other parts around the selected part will move.
    enum class PartsRelationsAdjustment
    {
        MoveNone = 0,
        MoveAll
    };

    BoundingBoxf3 m_bounding_box;
    Transform3d m_grabbers_transform;
    Vec3d m_center{ Vec3d::Zero() };
    Vec3d m_instance_center{ Vec3d::Zero() };
    Vec3d m_scale{ Vec3d::Ones() };
    double m_snap_step{ 0.05 };
    StartingData m_starting;
    bool  m_imperial_units{ false };
    PartsRelationsAdjustment m_relations_adjustment_mode{PartsRelationsAdjustment::MoveNone};
    std::vector<std::string> m_relations_adjustment_modes{};
    float m_label_width{ 0.f };
    float m_control_width{ 200.f };
    bool m_show_shortcuts{ false };
    std::vector<std::pair<std::string, std::string>>  m_shortcuts;
    float m_shortcut_label_width{ -1.f };


    struct GrabberConnection
    {
        GLModel model;
        std::pair<unsigned int, unsigned int> grabber_indices;
        Vec3d old_v1{ Vec3d::Zero() };
        Vec3d old_v2{ Vec3d::Zero() };
    };
    std::array<GrabberConnection, 7> m_grabber_connections;

    ColorRGBA m_base_color;
    ColorRGBA m_drag_color;
    ColorRGBA m_highlight_color;
public:
    GLGizmoScale3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    double get_snap_step(double step) const { return m_snap_step; }
    void set_snap_step(double step) { m_snap_step = step; }

    const Vec3d& get_scale() const { return m_scale; }
    void set_scale(const Vec3d& scale) { m_starting.scale = scale; m_scale = scale; }

    std::string get_tooltip() const override;

    /// <summary>
    /// Postpone to Grabber for scale
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information otherwise False.</returns>
    bool on_mouse(const wxMouseEvent &mouse_event) override;

    void data_changed(bool is_serializing) override;
    void enable_ununiversal_scale(bool enable);
protected:
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    virtual bool on_is_activable() const override;
    virtual void on_start_dragging() override;
    virtual void on_stop_dragging() override;
    virtual void on_dragging(const UpdateData& data) override;
    virtual void on_render() override;
    virtual void on_register_raycasters_for_picking() override;
    virtual void on_unregister_raycasters_for_picking() override;
    void on_render_input_window(float x, float y, float bottom_limit) override;
    void render_shortcuts();
    void adjust_window_position(float x, float y, float bottom_limit);

private:
    void render_grabbers_connection(unsigned int id_1, unsigned int id_2, const ColorRGBA& color);

    void do_scale_along_axis(Axis axis, const UpdateData& data);
    void do_scale_uniform(const UpdateData& data);

    double calc_ratio(const UpdateData& data) const;
    void update_render_data();
    static AdjacentVolumes get_adjacent_volumes(
        Selection &selection, const BoundingBoxf3& world_bounding_box
    );
};


} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoScale_hpp_