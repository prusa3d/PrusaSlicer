///|/ Copyright (c) Prusa Research 2020 - 2023 Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Filip Sykala @Jony01, Oleksandra Iushchenko @YuSanka
///|/ Copyright (c) BambuStudio 2023 manch1n @manch1n
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GCodeViewer_hpp_
#define slic3r_GCodeViewer_hpp_

#include "3DScene.hpp"
#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "GLModel.hpp"

#include "LibVGCode/LibVGCodeWrapper.hpp"
// needed for tech VGCODE_ENABLE_COG_AND_TOOL_MARKERS
#include "../../src/libvgcode/include/Types.hpp"

#include <cstdint>
#include <float.h>
#include <set>
#include <unordered_set>

namespace Slic3r {

class Print;
class TriangleMesh;

namespace GUI {

class GCodeViewer
{
    // helper to render shells
    struct Shells
    {
        GLVolumeCollection volumes;
        bool visible{ false };
        bool force_visible{ false };
    };

    // helper to render center of gravity
    class COG
    {
        GLModel m_model;
        bool m_visible{ false };
#if !VGCODE_ENABLE_COG_AND_TOOL_MARKERS
        // whether or not to render the model with fixed screen size
        bool m_fixed_screen_size{ true };
#endif // !VGCODE_ENABLE_COG_AND_TOOL_MARKERS
        float m_scale_factor{ 1.0f };
        double m_total_mass{ 0.0 };
        Vec3d m_total_position{ Vec3d::Zero() };

    public:
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
      void render(bool fixed_screen_size);
#else
      void render();
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

        void reset() {
            m_total_position = Vec3d::Zero();
            m_total_mass = 0.0;
        }

        bool is_visible() const { return m_visible; }
        void set_visible(bool visible) { m_visible = visible; }

        void add_segment(const Vec3d& v1, const Vec3d& v2, double mass) {
            if (mass > 0.0) {
                m_total_position += mass * 0.5 * (v1 + v2);
                m_total_mass += mass;
            }
        }

        Vec3d cog() const { return (m_total_mass > 0.0) ? (Vec3d)(m_total_position / m_total_mass) : Vec3d::Zero(); }

    private:
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
        void init(bool fixed_screen_size) {
#else
        void init() {
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
            if (m_model.is_initialized())
                return;

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
            const float radius = fixed_screen_size ? 10.0f : 1.0f;
#else
            const float radius = m_fixed_screen_size ? 10.0f : 1.0f;
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
            m_model.init_from(smooth_sphere(32, radius));
        }
    };

public:
    struct SequentialView
    {
#if ENABLE_ACTUAL_SPEED_DEBUG
        struct ActualSpeedImguiWidget
        {
            std::pair<float, float> y_range = { 0.0f, 0.0f };
            std::vector<std::pair<float, ColorRGBA>> levels;
            struct Item
            {
              float pos{ 0.0f };
              float speed{ 0.0f };
              bool internal{ false };
            };
            std::vector<Item> data;
            int plot(const char* label, const std::array<float, 2>& frame_size = { 0.0f, 0.0f });
        };
#endif // ENABLE_ACTUAL_SPEED_DEBUG

        class Marker
        {
            GLModel m_model;
            Vec3f m_world_position;
            // For seams, the position of the marker is on the last endpoint of the toolpath containing it.
            // This offset is used to show the correct value of tool position in the "ToolPosition" window.
            // See implementation of render() method
            Vec3f m_world_offset;
            // z offset of the print
            float m_z_offset{ 0.0f };
            // z offset of the model
            float m_model_z_offset{ 0.5f };
            bool m_visible{ true };
            bool m_fixed_screen_size{ false };
            float m_scale_factor{ 1.0f };
#if ENABLE_ACTUAL_SPEED_DEBUG
            ActualSpeedImguiWidget m_actual_speed_imgui_widget;
#endif // ENABLE_ACTUAL_SPEED_DEBUG

        public:
            void init();

            const BoundingBoxf3& get_bounding_box() const { return m_model.get_bounding_box(); }

            void set_world_position(const Vec3f& position) { m_world_position = position; }
            void set_world_offset(const Vec3f& offset) { m_world_offset = offset; }
            void set_z_offset(float z_offset) { m_z_offset = z_offset; }

#if ENABLE_ACTUAL_SPEED_DEBUG
            void set_actual_speed_y_range(const std::pair<float, float>& y_range) {
                m_actual_speed_imgui_widget.y_range = y_range;
            }
            void set_actual_speed_levels(const std::vector<std::pair<float, ColorRGBA>>& levels) {
                m_actual_speed_imgui_widget.levels = levels;
            }
            void set_actual_speed_data(const std::vector<ActualSpeedImguiWidget::Item>& data) {
                m_actual_speed_imgui_widget.data = data;
            }
#endif // ENABLE_ACTUAL_SPEED_DEBUG

            bool is_visible() const { return m_visible; }
            void set_visible(bool visible) { m_visible = visible; }

            void render();
            void render_position_window(const libvgcode::Viewer* viewer);
        };

        class GCodeWindow
        {
            struct Line
            {
                std::string command;
                std::string parameters;
                std::string comment;
            };

            struct Range
            {
                std::optional<size_t> min;
                std::optional<size_t> max;
                bool empty() const {
                    return !min.has_value() || !max.has_value();
                }
                bool contains(const Range& other) const {
                    return !this->empty() && !other.empty() && *this->min <= *other.min && *this->max >= other.max;
                }
                size_t size() const {
                    return empty() ? 0 : *this->max - *this->min + 1;
                }
            };

            bool m_visible{ true };
            std::string m_filename;
            bool m_is_binary_file{ false };
            // map for accessing data in file by line number
            std::vector<std::vector<size_t>> m_lines_ends;
            std::vector<Line> m_lines_cache;
            Range m_cache_range;
            size_t m_max_line_length{ 0 };

        public:
            void load_gcode(const GCodeProcessorResult& gcode_result);
            void reset() {
                m_lines_ends.clear();
                m_lines_cache.clear();
                m_filename.clear();
            }
            void toggle_visibility() { m_visible = !m_visible; }
            void render(float top, float bottom, size_t curr_line_id);

        private:
            void add_gcode_line_to_lines_cache(const std::string& src);
        };

        Marker marker;
        GCodeWindow gcode_window;

        void render(float legend_height, const libvgcode::Viewer* viewer, uint32_t gcode_id);
    };

private:
    bool m_gl_data_initialized{ false };
    unsigned int m_last_result_id{ 0 };
    // bounding box of toolpaths
    BoundingBoxf3 m_paths_bounding_box;
    // bounding box of shells
    BoundingBoxf3 m_shells_bounding_box;
    // bounding box of toolpaths + marker tools + shells
    BoundingBoxf3 m_max_bounding_box;
    float m_max_print_height{ 0.0f };
    float m_z_offset{ 0.0f };
    size_t m_extruders_count;
    std::vector<float> m_filament_diameters;
    std::vector<float> m_filament_densities;
    SequentialView m_sequential_view;
    Shells m_shells;
    COG m_cog;
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    // whether or not to render the cog model with fixed screen size
    bool m_cog_marker_fixed_screen_size{ true };
    float m_cog_marker_size{ 1.0f };
    bool m_tool_marker_fixed_screen_size{ false };
    float m_tool_marker_size{ 1.0f };
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    bool m_legend_visible{ true };
    bool m_legend_enabled{ true };
    struct ViewTypeCache
    {
        bool write{ false };
        bool load{ false };
        libvgcode::EViewType value{ libvgcode::EViewType::FeatureType };
    };
    ViewTypeCache m_view_type_cache;

    struct LegendResizer
    {
        bool dirty{ true };
        void reset() { dirty = true; }
    };
    LegendResizer m_legend_resizer;
    PrintEstimatedStatistics m_print_statistics;
    GCodeProcessorResult::SettingsIds m_settings_ids;

    std::vector<CustomGCode::Item> m_custom_gcode_per_print_z;

    bool m_contained_in_bed{ true };

    ConflictResultOpt m_conflict_result;

    libvgcode::Viewer m_viewer;
    bool m_loaded_as_preview{ false };

public:
    GCodeViewer();
    ~GCodeViewer() { reset(); }

    void init();

    // extract rendering data from the given parameters
    void load_as_gcode(const GCodeProcessorResult& gcode_result, const Print& print, const std::vector<std::string>& str_tool_colors,
        const std::vector<std::string>& str_color_print_colors);
    void load_as_preview(libvgcode::GCodeInputData&& data);
    void update_shells_color_by_extruder(const DynamicPrintConfig* config);

    void reset();
    void render();
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    void render_cog() {
        if (!m_loaded_as_preview && m_viewer.get_layers_count() > 0)
            m_cog.render(m_cog_marker_fixed_screen_size);
    }
#else
    void render_cog() {
        if (!m_loaded_as_preview && m_viewer.get_layers_count() > 0)
            m_cog.render();
    }
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    bool has_data() const { return !m_viewer.get_extrusion_roles().empty(); }

    bool can_export_toolpaths() const;

    const BoundingBoxf3& get_paths_bounding_box() const { return m_paths_bounding_box; }
    const BoundingBoxf3& get_shells_bounding_box() const { return m_shells_bounding_box; }

    const BoundingBoxf3& get_max_bounding_box() const {
        BoundingBoxf3& max_bounding_box = const_cast<BoundingBoxf3&>(m_max_bounding_box);
        if (!max_bounding_box.defined) {
            if (m_shells_bounding_box.defined)
                max_bounding_box = m_shells_bounding_box;
            if (m_paths_bounding_box.defined) {
                max_bounding_box.merge(m_paths_bounding_box);
                max_bounding_box.merge(m_paths_bounding_box.max + m_sequential_view.marker.get_bounding_box().size().z() * Vec3d::UnitZ());
            }
        }
        return m_max_bounding_box;
    }

    std::vector<double> get_layers_zs() const {
        const std::vector<float> zs = m_viewer.get_layers_zs();
        std::vector<double> ret;
        std::transform(zs.begin(), zs.end(), std::back_inserter(ret), [](float z) { return static_cast<double>(z); });
        return ret;
    }
    std::vector<float> get_layers_times() const { return m_viewer.get_layers_estimated_times(); }

    const SequentialView& get_sequential_view() const { return m_sequential_view; }
    void update_sequential_view_current(unsigned int first, unsigned int last);

    const libvgcode::Interval& get_gcode_view_full_range() const { return m_viewer.get_view_full_range(); }
    const libvgcode::Interval& get_gcode_view_enabled_range() const { return m_viewer.get_view_enabled_range(); }
    const libvgcode::Interval& get_gcode_view_visible_range() const { return m_viewer.get_view_visible_range(); }
    const libvgcode::PathVertex& get_gcode_vertex_at(size_t id) const { return m_viewer.get_vertex_at(id); }

    bool is_contained_in_bed() const { return m_contained_in_bed; }

    void set_view_type(libvgcode::EViewType type) {
        m_viewer.set_view_type((m_view_type_cache.load && m_view_type_cache.value != type) ? m_view_type_cache.value : type);
        const libvgcode::EViewType view_type = get_view_type();
        if (m_view_type_cache.write && m_view_type_cache.value != view_type)
            m_view_type_cache.value = view_type;
    }

    libvgcode::EViewType get_view_type() const { return m_viewer.get_view_type(); }
    void enable_view_type_cache_load(bool enable) { m_view_type_cache.load = enable; }
    void enable_view_type_cache_write(bool enable) { m_view_type_cache.write = enable; }
    bool is_view_type_cache_load_enabled() const { return m_view_type_cache.load; }
    bool is_view_type_cache_write_enabled() const { return m_view_type_cache.write; }
    void set_layers_z_range(const std::array<unsigned int, 2>& layers_z_range);

    bool is_legend_shown() const { return m_legend_visible && m_legend_enabled; }
    void show_legend(bool show) { m_legend_visible = show; }
    void enable_legend(bool enable) { m_legend_enabled = enable; }

    void set_force_shells_visible(bool visible) { m_shells.force_visible = visible; }

    void export_toolpaths_to_obj(const char* filename) const;

    void toggle_gcode_window_visibility() { m_sequential_view.gcode_window.toggle_visibility(); }

    size_t get_extruders_count() { return m_extruders_count; }

    void invalidate_legend() { m_legend_resizer.reset(); }

    const ConflictResultOpt& get_conflict_result() const { return m_conflict_result; }

    void load_shells(const Print& print);

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    float get_cog_marker_scale_factor() const { return m_viewer.get_cog_marker_scale_factor(); }
    void set_cog_marker_scale_factor(float factor) { return m_viewer.set_cog_marker_scale_factor(factor); }
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

private:
    void load_wipetower_shell(const Print& print);
    void render_toolpaths();
    void render_shells();
    void render_legend(float& legend_height);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GCodeViewer_hpp_
