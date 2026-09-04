#pragma once

#include "AbstractViewerWrapper.hpp"
#include "Types.hpp"
#include "Slic3r/App/Imgui/DoubleSlider.hpp"

#include <Slic3r/App/libvgcode/FdmViewerInputData.hpp>
#include <Slic3r/App/libvgcode/FdmViewer.hpp>
#include <Slic3r/Domain/BoundingBox.hpp>
#include <Slic3r/Domain/Color.hpp>

#include "FdmViewerWrapperInputData.hpp"
#include "DoubleSliderForGCode.hpp"
#include "DoubleSliderForLayers.hpp"
#include "GCodeWindow.hpp"
#include "ActualSpeedPlotter.hpp"
#include "Slic3r/Biz/Units.hpp"

namespace Slic3r::Biz::libpgcode {
struct ProcessorResult;
}

namespace Slic3r::App::Preview {

class LegendWindow;

enum class FdmViewerWrapperMode{
    EditorGCode,
    GCodeViewer,
    EditorPreGCode,
};

struct FdmViewerWrapperSettings : public ViewerWrapperBaseSettings
{
    FdmViewerWrapperMode mode{ FdmViewerWrapperMode::EditorGCode };
    bool slider_layers_editable{ false };
    bool slider_layers_use_default_colors{ false };
    bool seq_top_layer_only{ false };

    //
    // wrapper callbacks
    //
    InvalidateSliceCallback                         cb_invalidate_slice{ nullptr };
    UpdateLayersSlider                              cb_update_layers_slider{ nullptr };
    GCodeViewTypeChangedCallback                    cb_gcode_view_type_changed{ nullptr };

    //
    // layers slider callbacks
    //
    TicksChangedCallback                            cb_slider_layers_ticks_changed{ nullptr };
    AutoColorChangeCallback                         cb_slider_layers_auto_color_change{ nullptr };
    NotifyEmptyAutoColorChangeCallback              cb_slider_layers_notify_empty_auto_color_change{ nullptr };
    NotifyEmptyColorChangeGCodeCallback             cb_slider_layers_notify_empty_color_change_gcode{ nullptr };
    GetExtrudersSequenceCallback                    cb_slider_layers_get_extruders_sequence{ nullptr };
    ShowInfoMsgCallback                             cb_slider_layers_show_info_msg{ nullptr };
    GetUsedExtrudersInPrintCallback                 cb_slider_layers_get_used_extruders_in_print{ nullptr };
    //
    // gcode slider callbacks
    //
    Imgui::DoubleSlider::OnThumbMoveCallback        cb_slider_gcode_on_thumb_move{ nullptr };
};

class FdmViewerWrapper : public AbstractViewerWrapper
{
public:
    FdmViewerWrapper() = default;

    bool init(Render::Device& device, Scene::Scene& scene, Scene::GeometryDataFactory& data_factory) override;
    bool set_settings(const FdmViewerWrapperSettings& settings);

    void set_scene(Scene::Scene& scene) override
    {
        m_viewer.set_scene(scene);
    }

    void clear_scene() override
    {
        m_viewer.clear_scene();
    }

    void render_scene() override;
    void render_imgui() override;
    void reset() override;

    const libvgcode::AbstractViewer& viewer() const override {
        return *static_cast<const libvgcode::AbstractViewer*>(&m_viewer); }

    libvgcode::AbstractViewer& viewer() override {
        return *static_cast<libvgcode::AbstractViewer*>(&m_viewer); }

    FdmViewerWrapperMode mode() const { return m_settings.mode; }
    void set_mode(FdmViewerWrapperMode mode);

    void load(
        FdmViewerWrapperInputData&& wrapper_data,
        libvgcode::FdmViewerInputData&& data,
        const Scene::Transform& transform
    );
    void load_from_result(
        const Biz::libpgcode::ProcessorResult& result,
        const Scene::Transform& transform
    );

    void set_extrusion_role_color(Domain::GCodeExtrusionRole role, const Domain::ColorRGB& color) { return m_viewer.set_extrusion_role_color(role, color); }
    
    libvgcode::ViewType view_type() const { return m_viewer.view_type(); }
    void set_view_type(libvgcode::ViewType type);
    bool is_view_type_available(libvgcode::ViewType type) const;

    Domain::BoundingBox3d bounding_box(const Biz::libpgcode::MoveTypes& types = {
        Biz::libpgcode::MoveType::Retract,
        Biz::libpgcode::MoveType::Unretract,
        Biz::libpgcode::MoveType::Seam,
        Biz::libpgcode::MoveType::ToolChange,
        Biz::libpgcode::MoveType::ColorChange,
        Biz::libpgcode::MoveType::PausePrint,
        Biz::libpgcode::MoveType::CustomGCode,
        Biz::libpgcode::MoveType::Travel,
        Biz::libpgcode::MoveType::Wipe,
        Biz::libpgcode::MoveType::Extrude }) const { return m_viewer.bounding_box(types); }

    void render_toolpaths();

    std::unique_ptr<GCodeWindow> unload_gcode_window();
    std::unique_ptr<LegendWindow> unload_legend();
    std::unique_ptr<DoubleSliderForGcode> unload_double_slider_gcode();

    Biz::libpgcode::UnitsSystem units() const { return m_units; }
    void set_units(Biz::libpgcode::UnitsSystem sys);

    Biz::libpgcode::GCodeProducer producer() const { return m_data.producer; }

    const WipeTowerAndFlushFilamentUsagePerExtruder& wipe_tower_and_flush_filament_usage() const;
    bool has_wipe_tower_filament() const;
    bool has_flush_filament() const;

    float cog_marker_scale_factor() const { return m_viewer.cog_marker_scale_factor(); }
    void set_cog_marker_scale_factor(float factor) { m_viewer.set_cog_marker_scale_factor(factor); }

    bool tool_marker_enabled() const { return m_viewer.tool_marker_enabled(); }
    void set_tool_marker_enabled(bool enabled) { m_viewer.set_tool_marker_enabled(enabled); }
    float tool_marker_scale_factor() const { return m_viewer.tool_marker_scale_factor(); }
    void set_tool_marker_scale_factor(float factor) { m_viewer.set_tool_marker_scale_factor(factor); }
    Domain::Vec3f tool_marker_position() const { return m_viewer.tool_marker_position(); }

    bool is_top_layer_only_view_range() const { return m_viewer.is_top_layer_only_view_range(); }
    void toggle_top_layer_only_view_range() { m_viewer.toggle_top_layer_only_view_range(); }

    const libvgcode::Interval& view_visible_range() const { return m_viewer.view_visible_range(); }
    const libvgcode::Interval& view_enabled_range() const { return m_viewer.view_enabled_range(); }

    bool is_option_visible(Biz::libpgcode::OptionType type) { return m_viewer.is_option_visible(type); }
    void toggle_option_visibility(Biz::libpgcode::OptionType type) { m_viewer.toggle_option_visibility(type); }
    const Biz::libpgcode::OptionTypes& options() const { return m_viewer.options(); }

    bool is_legend_shown() const { return m_legend_params.is_shown(); }

    const libvgcode::Interval& layers_range() const { return m_viewer.layers_range(); }
    void set_layers_range(libvgcode::Interval::value_type min, libvgcode::Interval::value_type max);

    const libvgcode::GCodeEvents& gcode_events() const { return m_viewer.gcode_events(); }
    uint8_t used_extruders_count() const { return m_viewer.used_extruders_count(); }
    std::vector<uint8_t> used_extruders_ids() const { return m_viewer.used_extruders_ids(); }

    Domain::CustomGCode::Info slider_layers_ticks_values() { return m_slider_layers->ticks_values(); }

    void reset_default_extrusion_roles_colors() { m_viewer.reset_default_extrusion_roles_colors(); }

    void set_extrusion_roles_colors_popup_visible(bool show) { m_extrusion_roles_colors_popup_visible = show; }
    void toggle_extrusion_roles_colors_popup_visible() { m_extrusion_roles_colors_popup_visible = !m_extrusion_roles_colors_popup_visible; }
    bool is_extrusion_roles_colors_popup_visible() const { return m_extrusion_roles_colors_popup_visible; }

    void set_options_colors_popup_visible(bool show) { m_options_colors_popup_visible = show; }
    void toggle_options_colors_popup_visible() { m_options_colors_popup_visible = !m_options_colors_popup_visible; }
    bool is_options_colors_popup_visible() const { return m_options_colors_popup_visible; }

    void set_range_colors_popup_type(libvgcode::ViewType type);
    void set_scale_factor_popup_type(Biz::libpgcode::OptionType type);

    void set_radius_popup_type(Biz::libpgcode::MoveType type);
    Biz::libpgcode::MoveType radius_popup_type() { return m_radius_popup_type; }
    void render_customize_radius_popup();

    size_t current_vertex_id() const { return m_viewer.current_vertex_id(); }

private:
    FdmViewerWrapperSettings m_settings;
    FdmViewerWrapperInputData m_data;

    libvgcode::FdmViewer m_viewer;
    Yoga::Passthrough<DoubleSliderForGcode> m_slider_gcode;
    Yoga::Passthrough<LegendWindow> m_legend;
    Yoga::Passthrough<GCodeWindow> m_gcode_window;
    GCodeWindowData m_gcode_window_data;
    ActualSpeedPlotData m_actual_speed_plot_data;
    LegendParams m_legend_params;

    float m_legend_height{ 0.0f };
    Biz::libpgcode::UnitsSystem m_units{ Biz::libpgcode::UnitsSystem::SI };

    bool m_extrusion_roles_colors_popup_visible{ false };
    bool m_options_colors_popup_visible{ false };
    libvgcode::ViewType m_range_colors_popup_type{ libvgcode::ViewType::COUNT };
    Biz::libpgcode::MoveType m_radius_popup_type{ Biz::libpgcode::MoveType::COUNT };
    Biz::libpgcode::OptionType m_scale_factor_popup_type{ Biz::libpgcode::OptionType::COUNT };

    bool m_loading{ false };

private:
    void update_slider_gcode(std::optional<size_t> visible_range_min = std::nullopt,
                             std::optional<size_t> visible_range_max = std::nullopt);
    void update_legend_type_selector();
    void update_slider_layers();
    void update_view_visible_range(size_t first, size_t last);

    void on_slider_layers_scroll_changed();
    void on_slider_gcode_scroll_changed();
    void on_extrusion_role_visibility_changed();
    void on_request_extra_frames(unsigned int count = 1);
    void on_slider_layers_ticks_changed();
    std::string on_slider_layers_get_gcode(Domain::CustomGCode::Type type);
    libvgcode::Palette on_slider_layers_get_extruder_colors();

    void render_vertex_properties(const WrapperLayoutData& layout);

    void render_customize_extrusion_roles_colors_popup();
    void render_customize_options_colors_popup();
    void render_customize_range_colors_popup();
    void render_customize_scale_factor_popup();
};

} // namespace Slic3r::App::Preview
