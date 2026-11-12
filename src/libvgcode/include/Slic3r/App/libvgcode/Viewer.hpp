#pragma once

#include "Types.hpp"
#include "ViewerInputData.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"

#include <Slic3r/Biz/libpgcode/ProcessorResult.hpp>

#define ENABLE_RENDER_TO_TEXTURE (0 && !SLIC3R_OPENGL_ES)

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Scene {
class Scene;
class GeometryDataFactory;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::libvgcode {

class ColorRange;
struct ColorPrint;
class ViewerImpl;

class Viewer
{
public:
    Viewer();
    ~Viewer();
    Viewer(const Viewer& other) = delete;
    Viewer(Viewer&& other) = delete;
    Viewer& operator = (const Viewer& other) = delete;
    Viewer& operator = (Viewer&& other) = delete;

    /**
     * @brief Initialize the viewer
     *
     * @param device The current device.
     * @param scene The current scene.
     * @param data_factory The geometry factory.
     */
    void init(Render::Device& device, Scene::Scene& scene, Scene::GeometryDataFactory& data_factory);
    //
    // Reset the contents of the viewer.
    // Automatically called by load() method.
    //
    void reset();
    //
    // Setup the viewer content from the given data.
    // See: ViewerInputData
    //
    void load(ViewerInputData&& gcode_data);
    //
    // Setup the viewer content from the given data (support for SLA printers).
    //
    void load_as_sla(const std::vector<float>& layers_zs, const std::vector<float>& layers_times);
    //
    // Render the toolpaths according to the current settings
    //
    void render();

#if ENABLE_RENDER_TO_TEXTURE
    //
    // Render the toolpaths according to the current settings and
    // using the given camera matrices and background color on a texture.
    //
    std::vector<uint8_t> render_to_texture(uint16_t width, uint16_t height, const Transform3f& view_matrix,
        const Transform3f& projection_matrix, const ColorRGBA& background_color);
#endif // ENABLE_RENDER_TO_TEXTURE

    //
    // ************************************************************************
    // Settings
    // The following methods can be used to query/customize the parameters
    // used to render the toolpaths.
    // ************************************************************************
    //

    //
    // View type
    // See: ViewType
    //
    ViewType view_type() const;
    void set_view_type(ViewType type);
    //
    // Time mode
    // See: Biz::libpgcode::TimeMode
    //
    Biz::libpgcode::TimeMode time_mode() const;
    void set_time_mode(Biz::libpgcode::TimeMode mode);
    //
    // Top layer only
    // Whether or not the visible range is limited to the current top layer only.
    //
    bool is_top_layer_only_view_range() const;
    //
    // Toggle the top layer only state.
    //
    void toggle_top_layer_only_view_range();
    //
    // Returns true if the given option is visible.
    //
    bool is_option_visible(Biz::libpgcode::OptionType type) const;
    //
    // Toggle the visibility state of the given option.
    //
    void toggle_option_visibility(Biz::libpgcode::OptionType type);
    //
    // Returns true if the given extrusion role is visible.
    //
    bool is_extrusion_role_visible(Domain::GCodeExtrusionRole role) const;
    //
    // Toggle the visibility state of the given extrusion role.
    //
    void toggle_extrusion_role_visibility(Domain::GCodeExtrusionRole role);
    //
    // Return the color used to render the given extrusion rols.
    //
    const ColorRGB& extrusion_role_color(Domain::GCodeExtrusionRole role) const;
    //
    // Set the color used to render the given extrusion role.
    //
    void set_extrusion_role_color(Domain::GCodeExtrusionRole role, const ColorRGB& color);
    //
    // Reset the colors used to render the extrusion roles to the default value.
    //
    void reset_default_extrusion_roles_colors();
    //
    // Return the color used to render the given option.
    //
    const ColorRGB& option_color(Biz::libpgcode::OptionType type) const;
    //
    // Set the color used to render the given option.
    //
    void set_option_color(Biz::libpgcode::OptionType type, const ColorRGB& color);
    //
    // Reset the colors used to render the options to the default value.
    //
    void reset_default_options_colors();
    //
    // Return the count of colors in the palette used to render
    // the toolpaths when the view type is ViewType::Tool.
    //
    size_t tool_colors_count() const;
    //
    // Return the palette used to render the toolpaths when
    // the view type is ViewType::Tool.
    //
    const Palette& tool_colors() const;
    //
    // Set the palette used to render the toolpaths when
    // the view type is ViewType::Tool with the given one.
    //
    void set_tool_colors(const Palette& colors);
    //
    // Return the count of colors in the palette used to render
    // the toolpaths when the view type is ViewType::ColorPrint.
    //
    size_t color_print_colors_count() const;
    //
    // Return the palette used to render the toolpaths when
    // the view type is ViewType::ColorPrint.
    //
    const Palette& color_print_colors() const;
    //
    // Set the palette used to render the toolpaths when
    // the view type is ViewType::ColorPrint with the given one.
    //
    void set_color_print_colors(const Palette& colors);
    //
    // Get the color range for the given view type.
    // Valid view types are:
    // ViewType::Height
    // ViewType::Width
    // ViewType::Speed
    // ViewType::ActualSpeed
    // ViewType::FanSpeed
    // ViewType::Temperature
    // ViewType::VolumetricFlowRate
    // ViewType::ActualVolumetricFlowRate
    // ViewType::LayerTimeLinear
    // ViewType::LayerTimeLogarithmic
    //
    const ColorRange& color_range(ViewType type) const;
    //
    // Set the palette for the color range corresponding to the given view type
    // with the given value.
    // Valid view types are:
    // ViewType::Height
    // ViewType::Width
    // ViewType::Speed
    // ViewType::ActualSpeed
    // ViewType::FanSpeed
    // ViewType::Temperature
    // ViewType::VolumetricFlowRate
    // ViewType::ActualVolumetricFlowRate
    // ViewType::LayerTimeLinear
    // ViewType::LayerTimeLogarithmic
    //
    void set_color_range_palette(ViewType type, const Palette& palette);
    //
    // Get the radius, in mm, of the cylinders used to render the travel moves.
    //
    float travels_radius() const;
    //
    // Set the radius, in mm, of the cylinders used to render the travel moves.
    // Radius is clamped to [MIN_TRAVELS_RADIUS_MM..MAX_TRAVELS_RADIUS_MM]
    //
    void set_travels_radius(float radius);
    //
    // Get the radius, in mm, of the cylinders used to render the wipe moves.
    //
    float wipes_radius() const;
    //
    // Set the radius, in mm, of the cylinders used to render the wipe moves.
    // Radius is clamped to [MIN_WIPES_RADIUS_MM..MAX_WIPES_RADIUS_MM]
    //
    void set_wipes_radius(float radius);
    //
    // Return the count of detected layers.
    //
    size_t layers_count() const;
    //
    // Return the current visible layers range.
    //
    const Interval& layers_range() const;
    //
    // Set the current visible layers range with the given interval.
    // Values are clamped to [0..get_layers_count() - 1].
    //
    void set_layers_range(const Interval& range);
    //
    // Set the current visible layers range with the given min and max values.
    // Values are clamped to [0..get_layers_count() - 1].
    //
    void set_layers_range(Interval::value_type min, Interval::value_type max);
    //
    // Return the current visible range.
    // Three ranges are defined: full, enabled and visible.
    // For all of them the range endpoints represent:
    // [0] -> min vertex id
    // [1] -> max vertex id
    // Full is the range of vertices that could potentially be visualized accordingly to the current settings.
    // Enabled is the part of the full range that is selected for visualization accordingly to the current settings.
    // Visible is the part of the enabled range that is actually visualized accordingly to the current settings.
    // 
    const Interval& view_visible_range() const;
    //
    // Set the current visible range.
    // Values are clamped to the current view enabled range;
    // 
    void set_view_visible_range(Interval::value_type min, Interval::value_type max);
    //
    // Return the current full range.
    // Three ranges are defined: full, enabled and visible.
    // For all of them the range endpoints represent:
    // [0] -> min vertex id
    // [1] -> max vertex id
    // Full is the range of vertices that could potentially be visualized accordingly to the current settings.
    // Enabled is the part of the full range that is selected for visualization accordingly to the current settings.
    // Visible is the part of the enabled range that is actually visualized accordingly to the current settings.
    // 
    const Interval& view_full_range() const;
    //
    // Return the current enabled range.
    // Three ranges are defined: full, enabled and visible.
    // For all of them the range endpoints represent:
    // [0] -> min vertex id
    // [1] -> max vertex id
    // Full is the range of vertices that could potentially be visualized accordingly to the current settings.
    // Enabled is the part of the full range that is selected for visualization accordingly to the current settings.
    // Visible is the part of the enabled range that is actually visualized accordingly to the current settings.
    // 
    const Interval& view_enabled_range() const;

    //
    // Return the lights used for render the toolpaths.
    // 
    const Lights& lights() const;
    //
    // Set the lights used for render the toolpaths.
    // 
    void set_lights(const Lights& lights);
    //
    // Return the default lights used for render the toolpaths.
    // 
    const Lights& default_lights() const;

    //
    // ************************************************************************
    // Property getters
    // The following methods can be used to query detected properties.
    // ************************************************************************
    //

    //
    // Spiral vase mode
    // Whether or not the gcode was generated with spiral vase mode enabled.
    // See: GCodeInputData
    //
    bool is_spiral_vase_enabled() const;
    //
    // Return the z of the layer with the given id
    // or 0.0f if the id does not belong to [0..get_layers_count() - 1].
    //
    float layer_z(size_t layer_id) const;
    //
    // Return the list of zs of the detected layers.
    //
    std::vector<float> layers_zs() const;
    //
    // Return the id of the layer closest to the given z.
    //
    size_t layer_id_at(float z) const;
    //
    // Return the count of detected used extruders.
    //
    uint8_t used_extruders_count() const;
    //
    // Return the count of detected used extruders.
    //
    uint8_t extruders_count() const;
    //
    // Return the list of ids of the detected used extruders.
    //
    std::vector<uint8_t> used_extruders_ids() const;
    //
    // Return the length in mm of used filament for the extruder with the given id.
    //
    float used_extruder_used_filament_length(uint8_t extruder_id) const;
    //
    // Return the mass in g of used filament for the extruder with the given id.
    //
    float used_extruder_used_filament_mass(uint8_t extruder_id) const;
    //
    // Return the list of detected time modes.
    //
    Biz::libpgcode::TimeModes time_modes() const;
    //
    // Return the count of vertices used to render the toolpaths
    //
    size_t vertices_count() const;
    //
    // Return the vertices used to render the toolpaths
    //
    const Biz::libpgcode::MoveVertices& vertices() const;
    //
    // Return the vertex pointed by the max value of the view visible range
    //
    const Biz::libpgcode::MoveVertex& current_vertex() const;
    //
    // Return the index of vertex pointed by the max value of the view visible range
    //
    size_t current_vertex_id() const;
    //
    // Return the vertex at the given index
    //
    const Biz::libpgcode::MoveVertex& vertex_at(size_t id) const;
    //
    // Return the total estimated time, in seconds, using the current time mode.
    //
    float estimated_time() const;
    //
    // Return the estimated time, in seconds, at the vertex with the given index
    // using the current time mode.
    //
    float estimated_time_at(size_t id) const;
    //
    // Return the color used to render the given vertex with the current settings.
    //
    ColorRGB vertex_color(const Biz::libpgcode::MoveVertex& vertex) const;
    //
    // Return the count of detected extrusion roles
    //
    size_t extrusion_roles_count() const;
    //
    // Return the list of detected extrusion roles
    //
    Biz::libpgcode::GCodeExtrusionRoles extrusion_roles() const;
    //
    // Return the count of visible extrusion roles
    //
    size_t visible_extrusion_roles_count() const;
    //
    // Return the list of visible extrusion roles
    //
    Biz::libpgcode::GCodeExtrusionRoles visible_extrusion_roles() const;
    //
    // Return the count of detected options.
    //
    size_t options_count() const;
    //
    // Return the list of detected options.
    //
    const Biz::libpgcode::OptionTypes& options() const;
    //
    // Return the count of visible options.
    //
    size_t visible_options_count() const;
    //
    // Return the list of visible options.
    //
    Biz::libpgcode::OptionTypes visible_options() const;
    //
    // Return the count of detected color prints for the extruder with the given id.
    //
    size_t extruder_color_prints_count(uint8_t extruder_id) const;
    //
    // Return the list of detected color prints for the extruder with the given id.
    //
    ColorPrints extruder_color_prints(uint8_t extruder_id) const;
    //
    // Return the estimated time for the given role and the current time mode.
    //
    float extrusion_role_estimated_time(Domain::GCodeExtrusionRole role) const;
    //
    // Return the length in mm of used filament for the given role.
    //
    float extrusion_role_used_filament_length(Domain::GCodeExtrusionRole role) const;
    //
    // Return the mass in g of used filament for the given role.
    //
    float extrusion_role_used_filament_mass(Domain::GCodeExtrusionRole role) const;
    //
    // Return the estimated time for the given option type and the current time mode.
    //
    float option_estimated_time(Biz::libpgcode::OptionType type) const;
    //
    // Return the list of layers time for the current time mode.
    //
    std::vector<float> layers_estimated_times() const;
    //
    // Return the count of gcode events.
    //
    size_t gcode_events_count() const;
    //
    // Return the list of gcode events.
    //
    const GCodeEvents& gcode_events() const;
    //
    // Return the axes aligned bounding box containing all the given types.
    //
    BoundingBoxf3 bounding_box(const Biz::libpgcode::MoveTypes& types = {
        Biz::libpgcode::MoveType::Retract,
        Biz::libpgcode::MoveType::Unretract,
        Biz::libpgcode::MoveType::Seam,
        Biz::libpgcode::MoveType::ToolChange,
        Biz::libpgcode::MoveType::ColorChange,
        Biz::libpgcode::MoveType::PausePrint,
        Biz::libpgcode::MoveType::CustomGCode,
        Biz::libpgcode::MoveType::Travel,
        Biz::libpgcode::MoveType::Wipe,
        Biz::libpgcode::MoveType::Extrude }) const;
    //
    // Return the axes aligned bounding box containing all the extrusions with the given roles.
    //
    BoundingBoxf3 extrusion_bounding_box(const Biz::libpgcode::GCodeExtrusionRoles& roles = {
        Domain::GCodeExtrusionRole::Perimeter,
        Domain::GCodeExtrusionRole::ExternalPerimeter,
        Domain::GCodeExtrusionRole::OverhangPerimeter,
        Domain::GCodeExtrusionRole::InternalInfill,
        Domain::GCodeExtrusionRole::SolidInfill,
        Domain::GCodeExtrusionRole::TopSolidInfill,
        Domain::GCodeExtrusionRole::Ironing,
        Domain::GCodeExtrusionRole::BridgeInfill,
        Domain::GCodeExtrusionRole::GapFill,
        Domain::GCodeExtrusionRole::Skirt,
        Domain::GCodeExtrusionRole::SupportMaterial,
        Domain::GCodeExtrusionRole::SupportMaterialInterface,
        Domain::GCodeExtrusionRole::WipeTower,
        Domain::GCodeExtrusionRole::Custom }) const;
    /** @brief Returns the center of gravity marker position in world coordinates
     *
     * @note The following extrusion types are ignored:
     * Skirt
     * Support Material
     * Support Material Interface
     * WipeTower
     * Custom
     */
    Vec3f cog_marker_position() const;

    float cog_marker_scale_factor() const;
    void set_cog_marker_scale_factor(float factor);

    bool tool_marker_enabled() const;
    void set_tool_marker_enabled(bool enabled);

    float tool_marker_offset_z() const;
    void set_tool_marker_offset_z(float offset_z);

    float tool_marker_scale_factor() const;
    void set_tool_marker_scale_factor(float factor);

    const ColorRGB& tool_marker_color() const;
    void set_tool_marker_color(const ColorRGB& color);

    float tool_marker_alpha() const;
    void set_tool_marker_alpha(float alpha);

    BoundingBoxf3 tool_marker_bounding_box() const;

    bool export_toolpaths_to_obj(FILE& obj_file, FILE& mtl_file, const ObjExportParams& params) const;

private:
    ViewerImpl* m_impl{ nullptr };
};

} // namespace Slic3r::App::libvgcode
