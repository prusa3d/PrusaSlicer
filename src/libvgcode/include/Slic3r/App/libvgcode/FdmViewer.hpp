#pragma once

#include "AbstractViewer.hpp"
#include "Settings.hpp"
#include "SegmentTemplate.hpp"
#include "OptionTemplate.hpp"
#include "CogMarker.hpp"
#include "ToolMarker.hpp"
#include "Bitset.hpp"
#include "ExtrusionRoles.hpp"
#include "Extruders.hpp"
#include "ColorRange.hpp"
#include "FdmViewerInputData.hpp"

#include <Slic3r/Biz/libpgcode/ProcessorResult.hpp>
#include <Slic3r/Domain/Color.hpp>

#include <Slic3r/App/Render/Buffer.hpp>
#include <Slic3r/App/Scene/AabbRaycastNodeComponent.hpp>
#include "Slic3r/App/Scene/Transform.hpp"

#include "Slic3r/Domain/Types.hpp"

#define USE_TEXTURE_BUFFER (1 && SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED)

namespace Slic3r::App::Scene {
class Node;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::libvgcode {

struct GCodeInputData;

class FdmViewer : public AbstractViewer
{
public:
    FdmViewer();
    ~FdmViewer() = default;
    FdmViewer(const FdmViewer&) = delete;
    FdmViewer(FdmViewer&&) = delete;
    FdmViewer& operator = (const FdmViewer&) = delete;
    FdmViewer& operator = (FdmViewer&&) = delete;

    /**
     * @brief Initialize rendering geometry
     *
     * @param device The current device.
     * @param scene The current scene.
     * @param data_factory The geometry factory.
     */
    void init(Render::Device& device, Scene::Scene& scene, Scene::GeometryDataFactory& data_factory) override;

    void set_scene(Scene::Scene& scene) override;
    void clear_scene() override;

    //
    // Reset all caches and free gpu memory.
    //
    void reset() override;
    //
    // Setup all the variables used for visualization of the toolpaths
    // from the given gcode data.
    //
    void load(FdmViewerInputData&& gcode_data, const Scene::Transform& transform);

    //
    // Update the visibility property of toolpaths in dependence
    // of the current settings
    //
    void update_enabled_entities();
    //
    // Update the color of toolpaths in dependence of the current
    // view type and settings
    //
    void update_colors();
    void update_colors_texture();

    //
    // Render the toolpaths
    //
    void render() override;

#if ENABLE_RENDER_TO_TEXTURE 
    std::vector<uint8_t> render_to_texture(uint16_t width, uint16_t height, const Transform3f& view_matrix,
        const Transform3f& projection_matrix, const ColorRGBA& background_color);
#endif // ENABLE_RENDER_TO_TEXTURE

    ViewType view_type() const { return m_settings.view_type; }
    void set_view_type(ViewType type);

    Biz::libpgcode::TimeMode time_mode() const { return m_settings.time_mode; }
    void set_time_mode(Biz::libpgcode::TimeMode mode);

    void set_layers_range(Interval::value_type min, Interval::value_type max) override;

    bool is_top_layer_only_view_range() const { return m_settings.top_layer_only_view_range; }
    void toggle_top_layer_only_view_range();

    bool is_spiral_vase_enabled() const { return m_settings.spiral_vase_enabled; }

    const Biz::libpgcode::TimeModes& time_modes() const { return m_time_modes; }

    uint8_t used_extruders_count() const { return m_used_extruders.extruders_count(); }
    std::vector<uint8_t> used_extruders_ids() const { return m_used_extruders.extruders_ids(); }
    size_t used_extruder_color_prints_count(uint8_t extruder_id) const { return m_used_extruders.extruder_color_prints_count(extruder_id); }
    ColorPrints used_extruder_color_prints(uint8_t extruder_id) const { return m_used_extruders.extruder_color_prints(extruder_id); }
    float used_extruder_used_filament_length(uint8_t extruder_id) const { return m_used_extruders.extruder_used_filament_length(extruder_id); }
    float used_extruder_used_filament_mass(uint8_t extruder_id) const { return m_used_extruders.extruder_used_filament_mass(extruder_id); }

    uint8_t extruders_count() const { return m_extruders_count; }

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
        Biz::libpgcode::MoveType::Extrude }) const;
    Domain::BoundingBox3d extrusion_bounding_box(const Biz::libpgcode::GCodeExtrusionRoles& roles = {
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

    bool is_option_visible(Biz::libpgcode::OptionType type) const;
    void toggle_option_visibility(Biz::libpgcode::OptionType type);

    bool is_extrusion_role_visible(Domain::GCodeExtrusionRole role) const;
    void toggle_extrusion_role_visibility(Domain::GCodeExtrusionRole role);

    void set_view_visible_range(Interval::value_type min, Interval::value_type max) override;

    size_t vertices_count() const { return m_vertices->size(); }
    const Biz::libpgcode::MoveVertices& vertices() const { return *m_vertices; }
    const Biz::libpgcode::MoveVertex& current_vertex() const { return vertex_at(current_vertex_id()); }
    size_t current_vertex_id() const { return size_t(m_view_range.visible()[1]); }
    const Biz::libpgcode::MoveVertex& vertex_at(size_t id) const {
        return (id < m_vertices->size()) ? (*m_vertices)[id] : Biz::libpgcode::DUMMY_MOVE_VERTEX;
    }
    float estimated_time() const override { return m_total_time[size_t(m_settings.time_mode)]; }
    float estimated_time_at(size_t id) const override;
    Domain::ColorRGB vertex_color(const Biz::libpgcode::MoveVertex& vertex) const;

    size_t extrusion_roles_count() const { return m_extrusion_roles.roles_count(); }
    Biz::libpgcode::GCodeExtrusionRoles extrusion_roles() const { return m_extrusion_roles.roles(); }
    size_t visible_extrusion_roles_count() const;
    Biz::libpgcode::GCodeExtrusionRoles visible_extrusion_roles() const;
    float extrusion_role_estimated_time(Domain::GCodeExtrusionRole role) const {
        return m_extrusion_roles.time(role, m_settings.time_mode);
    }
    float extrusion_role_used_filament_length(Domain::GCodeExtrusionRole role) const {
        return m_extrusion_roles.used_filament_length(role);
    }
    float extrusion_role_used_filament_mass(Domain::GCodeExtrusionRole role) const {
        return m_extrusion_roles.used_filament_mass(role);
    }

    size_t options_count() const { return m_options.size(); }
    const Biz::libpgcode::OptionTypes& options() const { return m_options; }
    size_t visible_options_count() const;
    Biz::libpgcode::OptionTypes visible_options() const;
    float option_estimated_time(Biz::libpgcode::OptionType type) const;

    std::vector<float> layers_estimated_times() const override { return m_layers.times(m_settings.time_mode); }

    size_t gcode_events_count() const { return m_gcode_events.size(); }
    const GCodeEvents& gcode_events() const { return m_gcode_events; }

    const Palette& tool_colors() const;

    size_t color_print_colors_count() const { return m_color_print_colors.size(); }
    const Palette& color_print_colors() const { return m_color_print_colors; }

    const Domain::ColorRGB& extrusion_role_color(Domain::GCodeExtrusionRole role) const;
    void set_extrusion_role_color(Domain::GCodeExtrusionRole role, const Domain::ColorRGB& color);
    void reset_default_extrusion_roles_colors();

    const Domain::ColorRGB& option_color(Biz::libpgcode::OptionType type) const;
    void set_option_color(Biz::libpgcode::OptionType type, const Domain::ColorRGB& color);
    void reset_default_options_colors();

    const ColorRange& color_range(ViewType type) const;
    void set_color_range_palette(ViewType type, const Palette& palette);

    float travels_radius() const { return m_travels_radius; }
    void set_travels_radius(float radius);

    float wipes_radius() const { return m_wipes_radius; }
    void set_wipes_radius(float radius);

    Domain::Vec3f cog_marker_position() const { return m_cog_marker.position(); }
    float cog_marker_scale_factor() const { return m_cog_marker.scale_factor(); }
    void set_cog_marker_scale_factor(float factor) { m_cog_marker.set_scale_factor(factor); }

    bool tool_marker_enabled() const { return m_tool_marker.enabled(); }
    void set_tool_marker_enabled(bool enabled);
    float tool_marker_offset_z() const { return m_tool_marker.offset_z(); }
    void set_tool_marker_offset_z(float offset_z) { m_tool_marker.set_offset_z(offset_z); }
    float tool_marker_scale_factor() const { return m_tool_marker.scale_factor(); }
    void set_tool_marker_scale_factor(float factor) { m_tool_marker.set_scale_factor(factor); }
    const Domain::ColorRGB& tool_marker_color() const { return m_tool_marker.color(); }
    void set_tool_marker_color(const Domain::ColorRGB& color) { m_tool_marker.set_color(color); }
    float tool_marker_alpha() const { return m_tool_marker.alpha(); }
    void set_tool_marker_alpha(float alpha) { m_tool_marker.set_alpha(alpha); }
    Domain::BoundingBox3d tool_marker_bounding_box() const;
    Domain::Vec3f tool_marker_position() const { return current_vertex().position + m_tool_marker.offset_z() * Domain::Vec3f::UnitZ(); }

    bool export_toolpaths_to_obj(FILE& obj_file, FILE& mtl_file, const ObjExportParams& params) const;
    bool has_gcode_events_to_show() const;

private:
    //
    // Settings used to render the toolpaths
    //
    Settings m_settings;
    //
    // Detected extrusion roles
    //
    ExtrusionRoles m_extrusion_roles;
    //
    // Detected extruders count
    //
    uint8_t m_extruders_count{ Biz::libpgcode::MIN_EXTRUDERS_COUNT };
    //
    // Detected options
    //
    Biz::libpgcode::OptionTypes m_options;
    //
    // Detected used extruders
    //
    Extruders m_used_extruders;
    //
    // Detected total moves times
    //
    Biz::libpgcode::Times m_total_time{};
    //
    // Detected time modes
    //
    Biz::libpgcode::TimeModes m_time_modes;
    //
    // Detected options moves times
    //
    std::vector<std::pair<Biz::libpgcode::OptionType, Biz::libpgcode::Times>> m_options_times;
    //
    // List of gcode events
    //
    GCodeEvents m_gcode_events;
    //
    // Radius of cylinders used to render travel moves segments
    //
    float m_travels_radius{ DEFAULT_TRAVELS_RADIUS_MM };
    //
    // Radius of cylinders used to render wipe moves segments
    //
    float m_wipes_radius{ DEFAULT_WIPES_RADIUS_MM };
    //
    // Palette used to render extrusion roles
    //
    std::array<Domain::ColorRGB, Biz::libpgcode::GCODE_EXTRUSION_ROLES_COUNT> m_extrusion_roles_colors;
    //
    // Palette used to render options
    //
    std::array<Domain::ColorRGB, Biz::libpgcode::OPTION_TYPES_COUNT> m_options_colors;

    //
    // The OpenGL element used to represent all option markers
    //
    OptionTemplate m_option_template;
    //
    // The OpenGL element used to represent the center of gravity
    //
    CogMarker m_cog_marker;
    //
    // The OpenGL element used to represent the tool nozzle
    //
    ToolMarker m_tool_marker;
    //
    // cpu buffer to store vertices
    //
    std::shared_ptr<const Biz::libpgcode::MoveVertices> m_vertices;

    // Cache for the colors to reduce the need to recalculate colors of all the vertices.
    std::vector<float> m_vertices_colors;

    //
    // Variables used for toolpaths visibiliity
    //
    BitSet<> m_valid_lines_bitset;
    //
    // Variables used for toolpaths coloring
    //
    std::optional<Settings> m_ranges_settings;
    ColorRange m_height_range;
    ColorRange m_width_range;
    ColorRange m_speed_range;
    ColorRange m_actual_speed_range;
    ColorRange m_fan_speed_range;
    ColorRange m_temperature_range;
    ColorRange m_volumetric_rate_range;
    ColorRange m_actual_volumetric_rate_range;
    std::array<ColorRange, COLOR_RANGE_TYPES_COUNT> m_layer_time_range{
        ColorRange(ColorRangeType::Linear),
        ColorRange(ColorRangeType::Logarithmic)
    };
    Palette m_tool_colors;
    Palette m_color_print_colors;
 
#if USE_TEXTURE_BUFFER
    Render::TextureBuffer* m_positions_buffer{ nullptr };
    Render::TextureBuffer* m_heights_widths_angles_buffer{ nullptr };
    Render::TextureBuffer* m_colors_buffer{ nullptr };
    Render::TextureBuffer* m_enabled_segments_buffer{ nullptr };
    Render::TextureBuffer* m_enabled_options_buffer{ nullptr };
#else
    class TextureData
    {
    public:
        void init(Render::Device* device, size_t vertices_count);
        void set_positions(const std::vector<Vec4f>& positions);
        void set_heights_widths_angles(const std::vector<Vec4f>& heights_widths_angles);
        void set_colors(const std::vector<float>& colors);
        void set_enabled_segments(const std::vector<uint32_t>& enabled_segments);
        void set_enabled_options(const std::vector<uint32_t>& enabled_options);
        void reset();
        size_t count() const { return m_count; }
        std::pair<Render::TexturePtr, size_t> positions_tex(size_t id) const;
        std::pair<Render::TexturePtr, size_t> heights_widths_angles_tex(size_t id) const;
        std::pair<Render::TexturePtr, size_t> colors_tex(size_t id) const;
        std::pair<Render::TexturePtr, size_t> enabled_segments_tex(size_t id) const;
        std::pair<Render::TexturePtr, size_t> enabled_options_tex(size_t id) const;

        size_t max_texture_capacity() const { return m_width * m_height; }

    private:
        Render::Device* m_device{ nullptr };

        //
        // Texture width
        //
        size_t m_width{ 0 };
        //
        // Texture height
        //
        size_t m_height{ 0 };
        //
        // Count of textures
        //
        size_t m_count{ 0 };

        size_t m_max_texture_size{ 0 };

        struct Textures
        {
            std::pair<Render::TexturePtr, size_t> positions{ nullptr, 0 };
            std::pair<Render::TexturePtr, size_t> heights_widths_angles{ nullptr, 0 };
            std::pair<Render::TexturePtr, size_t> colors{ nullptr, 0 };
            std::pair<Render::TexturePtr, size_t> enabled_segments{ nullptr, 0 };
            std::pair<Render::TexturePtr, size_t> enabled_options{ nullptr, 0 };
        };

        std::vector<Textures> m_tex_ids;
    };

    TextureData m_texture_data;
#endif // USE_TEXTURE_BUFFER

    size_t m_enabled_segments_count{ 0 };
    size_t m_enabled_options_count{ 0 };

    std::pair<Domain::TriangleMesh, AABBMesh> m_aabb;

    Scene::Node* m_main_node{nullptr};

    Scene::Transform m_inverse_transform{ Scene::Transform::Identity() };

    void update_view_full_range() override;
    void update_color_ranges();
    void update_heights_widths();
    void render_segments(Scene::Node* node, const Domain::Vec3f& camera_position);
    void render_options(Scene::Node* node);
    void render_cog_marker(Scene::Node* node);
    void render_tool_marker(Scene::Node* node);
};

} // namespace Slic3r::App::libvgcode
