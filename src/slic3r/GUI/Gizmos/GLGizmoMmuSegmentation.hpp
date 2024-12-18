///|/ Copyright (c) Prusa Research 2019 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Filip Sykala @Jony01, Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GLGizmoMmuSegmentation_hpp_
#define slic3r_GLGizmoMmuSegmentation_hpp_

#include "GLGizmoPainterBase.hpp"

#include "slic3r/GUI/I18N.hpp"

namespace Slic3r::GUI {

class GLMmSegmentationGizmo3DScene
{
public:
    GLMmSegmentationGizmo3DScene() = delete;

    explicit GLMmSegmentationGizmo3DScene(size_t triangle_indices_buffers_count)
    {
        this->triangle_indices         = std::vector<std::vector<int>>(triangle_indices_buffers_count);
        this->triangle_indices_sizes   = std::vector<size_t>(triangle_indices_buffers_count);
        this->triangle_indices_VBO_ids = std::vector<unsigned int>(triangle_indices_buffers_count);
    }

    virtual ~GLMmSegmentationGizmo3DScene() { release_geometry(); }

    [[nodiscard]] inline bool has_VBOs(size_t triangle_indices_idx) const
    {
        assert(triangle_indices_idx < this->triangle_indices.size());
        return this->triangle_indices_VBO_ids[triangle_indices_idx] != 0;
    }

    // Release the geometry data, release OpenGL VBOs.
    void release_geometry();
    // Finalize the initialization of the geometry, upload the geometry to OpenGL VBO objects
    // and possibly releasing it if it has been loaded into the VBOs.
    void finalize_vertices();
    // Finalize the initialization of the indices, upload the indices to OpenGL VBO objects
    // and possibly releasing it if it has been loaded into the VBOs.
    void finalize_triangle_indices();

    void clear()
    {
        this->vertices.clear();
        for (std::vector<int> &ti : this->triangle_indices)
            ti.clear();

        for (size_t &triangle_indices_size : this->triangle_indices_sizes)
            triangle_indices_size = 0;
    }

    void render(size_t triangle_indices_idx) const;

    std::vector<float>            vertices;
    std::vector<std::vector<int>> triangle_indices;

    // When the triangle indices are loaded into the graphics card as Vertex Buffer Objects,
    // the above mentioned std::vectors are cleared and the following variables keep their original length.
    std::vector<size_t> triangle_indices_sizes;

    // IDs of the Vertex Array Objects, into which the geometry has been loaded.
    // Zero if the VBOs are not sent to GPU yet.
    unsigned int              vertices_VAO_id{ 0 };
    unsigned int              vertices_VBO_id{ 0 };
    std::vector<unsigned int> triangle_indices_VBO_ids;
};

class TriangleSelectorMmGui : public TriangleSelectorGUI {
public:
    TriangleSelectorMmGui() = delete;
    // Plus 1 in the initialization of m_gizmo_scene is because the first position is allocated for non-painted triangles, and the indices above colors.size() are allocated for seed fill.
    explicit TriangleSelectorMmGui(const TriangleMesh& mesh, const std::vector<ColorRGBA>& colors, const ColorRGBA& default_volume_color)
        : TriangleSelectorGUI(mesh), m_colors(colors), m_default_volume_color(default_volume_color), m_gizmo_scene(2 * (colors.size() + 1)) {}

    ~TriangleSelectorMmGui() override = default;

    void render(ImGuiWrapper* imgui, const Transform3d& matrix) override;

private:
    void update_render_data();

    const std::vector<ColorRGBA>&            m_colors;
    const ColorRGBA                          m_default_volume_color;
    GLMmSegmentationGizmo3DScene             m_gizmo_scene;
};

class GLGizmoMmuSegmentation : public GLGizmoPainterBase
{
public:
    GLGizmoMmuSegmentation(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
        : GLGizmoPainterBase(parent, icon_filename, sprite_id) {}
    ~GLGizmoMmuSegmentation() override = default;

    void render_painter_gizmo() override;

    void data_changed(bool is_serializing) override;

    void render_triangles(const Selection& selection) const override;

    // TriangleSelector::serialization/deserialization has a limit to store 19 different states.
    // EXTRUDER_LIMIT + 1 states are used to storing the painting because also uncolored triangles are stored.
    // When increasing EXTRUDER_LIMIT, it needs to ensure that TriangleSelector::serialization/deserialization
    // will be also extended to support additional states, requiring at least one state to remain free out of 19 states.
    static const constexpr size_t EXTRUDERS_LIMIT = 16;

    float get_cursor_radius_min() const override { return CursorRadiusMin; }

protected:
    ColorRGBA get_cursor_sphere_left_button_color() const override;
    ColorRGBA get_cursor_sphere_right_button_color() const override;

    TriangleStateType get_left_button_state_type() const override { return TriangleStateType(m_first_selected_extruder_idx + 1); }
    TriangleStateType get_right_button_state_type() const override { return TriangleStateType(m_second_selected_extruder_idx + 1); }

    void on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;

    bool on_is_selectable() const override;
    bool on_is_activable() const override;

    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    std::string get_gizmo_entering_text() const override { return _u8L("Entering Multimaterial painting"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving Multimaterial painting"); }
    std::string get_action_snapshot_name() const override { return _u8L("Multimaterial painting editing"); }

    size_t                            m_first_selected_extruder_idx  = 0;
    size_t                            m_second_selected_extruder_idx = 1;
    std::vector<std::string>          m_original_extruders_names;
    std::vector<ColorRGBA>            m_original_extruders_colors;
    std::vector<ColorRGBA>            m_modified_extruders_colors;
    std::vector<int>                  m_original_volumes_extruder_idxs;

    static const constexpr float      CursorRadiusMin = 0.1f; // cannot be zero

private:
    bool on_init() override;

    void update_model_object() const override;
    void update_from_model_object() override;

    void on_opening() override;
    void on_shutdown() override;
    PainterGizmoType get_painter_type() const override;

    void init_model_triangle_selectors();
    void init_extruders_data();

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, std::string> m_desc;
};

} // namespace Slic3r


#endif // slic3r_GLGizmoMmuSegmentation_hpp_
