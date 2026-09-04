#include "Slic3r/App/libvgcode/Viewer.hpp"
#include "ViewerImpl.hpp"

using namespace Slic3r::Biz::libpgcode;

namespace Slic3r::App::libvgcode {

Viewer::Viewer()
{
    m_impl = new ViewerImpl;
}

Viewer::~Viewer()
{
    delete m_impl;
}

void Viewer::init(Render::Device& device, Scene::Scene& scene, Scene::GeometryDataFactory& data_factory)
{
    m_impl->init(device, scene, data_factory);
}

void Viewer::reset()
{
    m_impl->reset();
}

void Viewer::load(ViewerInputData&& gcode_data)
{
    m_impl->load(std::move(gcode_data));
}

void Viewer::load_as_sla(const std::vector<float>& layers_zs, const std::vector<float>& layers_times)
{
    m_impl->load_as_sla(layers_zs, layers_times);
}

void Viewer::render()
{
    m_impl->render();
}

#if ENABLE_RENDER_TO_TEXTURE
std::vector<uint8_t> Viewer::render_to_texture(uint16_t width, uint16_t height, const Transform3f& view_matrix,
    const Transform3f& projection_matrix, const ColorRGBA& background_color)
{
    return m_impl->render_to_texture(width, height, view_matrix, projection_matrix, background_color);
}
#endif // ENABLE_RENDER_TO_TEXTURE

ViewType Viewer::view_type() const
{
    return m_impl->view_type();
}

void Viewer::set_view_type(ViewType type)
{
    m_impl->set_view_type(type);
}

TimeMode Viewer::time_mode() const
{
    return m_impl->time_mode();
}

void Viewer::set_time_mode(TimeMode mode)
{
    m_impl->set_time_mode(mode);
}

bool Viewer::is_top_layer_only_view_range() const
{
    return m_impl->is_top_layer_only_view_range();
}

void Viewer::toggle_top_layer_only_view_range()
{
    m_impl->toggle_top_layer_only_view_range();
}

bool Viewer::is_option_visible(OptionType type) const
{
    return m_impl->is_option_visible(type);
}

void Viewer::toggle_option_visibility(OptionType type)
{
    m_impl->toggle_option_visibility(type);
}

bool Viewer::is_extrusion_role_visible(Domain::GCodeExtrusionRole role) const
{
    return m_impl->is_extrusion_role_visible(role);
}

void Viewer::toggle_extrusion_role_visibility(Domain::GCodeExtrusionRole role)
{
    m_impl->toggle_extrusion_role_visibility(role);
}

const ColorRGB& Viewer::extrusion_role_color(Domain::GCodeExtrusionRole role) const
{
    return m_impl->extrusion_role_color(role);
}

void Viewer::set_extrusion_role_color(Domain::GCodeExtrusionRole role, const ColorRGB& color)
{
    m_impl->set_extrusion_role_color(role, color);
}

void Viewer::reset_default_extrusion_roles_colors()
{
    m_impl->reset_default_extrusion_roles_colors();
}

const ColorRGB& Viewer::option_color(OptionType type) const
{
    return m_impl->option_color(type);
}

void Viewer::set_option_color(OptionType type, const ColorRGB& color)
{
    m_impl->set_option_color(type, color);
}

void Viewer::reset_default_options_colors()
{
    m_impl->reset_default_options_colors();
}

size_t Viewer::tool_colors_count() const
{
    return m_impl->tool_colors_count();
}

const Palette& Viewer::tool_colors() const
{
    return m_impl->tool_colors();
}

void Viewer::set_tool_colors(const Palette& colors)
{
    m_impl->set_tool_colors(colors);
}

size_t Viewer::color_print_colors_count() const
{
    return m_impl->color_print_colors_count();
}

const Palette& Viewer::color_print_colors() const
{
    return m_impl->color_print_colors();
}

void Viewer::set_color_print_colors(const Palette& colors)
{
    m_impl->set_color_print_colors(colors);
}

const ColorRange& Viewer::color_range(ViewType type) const
{
    return m_impl->color_range(type);
}

void Viewer::set_color_range_palette(ViewType type, const Palette& palette)
{
    m_impl->set_color_range_palette(type, palette);
}

float Viewer::travels_radius() const
{
    return m_impl->travels_radius();
}

void Viewer::set_travels_radius(float radius)
{
    m_impl->set_travels_radius(radius);
}

float Viewer::wipes_radius() const
{
    return m_impl->wipes_radius();
}

void Viewer::set_wipes_radius(float radius)
{
    m_impl->set_wipes_radius(radius);
}

size_t Viewer::layers_count() const
{
    return m_impl->layers_count();
}

const Interval& Viewer::layers_range() const
{
    return m_impl->layers_range();
}

void Viewer::set_layers_range(const Interval& range)
{
    m_impl->set_layers_range(range);
}

void Viewer::set_layers_range(Interval::value_type min, Interval::value_type max)
{
    m_impl->set_layers_range(min, max);
}

const Interval& Viewer::view_visible_range() const
{
    return m_impl->view_visible_range();
}

void Viewer::set_view_visible_range(Interval::value_type min, Interval::value_type max)
{
    m_impl->set_view_visible_range(min, max);
}

const Interval& Viewer::view_full_range() const
{
    return m_impl->view_full_range();
}

const Interval& Viewer::view_enabled_range() const
{
    return m_impl->view_enabled_range();
}

const Lights& Viewer::lights() const
{
    return m_impl->lights();
}

void Viewer::set_lights(const Lights& lights)
{
    m_impl->set_lights(lights);
}

const Lights& Viewer::default_lights() const
{
    return m_impl->default_lights();
}

bool Viewer::is_spiral_vase_enabled() const
{
    return m_impl->is_spiral_vase_enabled();
}

float Viewer::layer_z(size_t layer_id) const
{
    return m_impl->layer_z(layer_id);
}

std::vector<float> Viewer::layers_zs() const
{
    return m_impl->layers_zs();
}

size_t Viewer::layer_id_at(float z) const
{
    return m_impl->layer_id_at(z);
}

uint8_t Viewer::used_extruders_count() const
{
    return m_impl->used_extruders_count();
}

uint8_t Viewer::extruders_count() const
{
    return m_impl->extruders_count();
}

std::vector<uint8_t> Viewer::used_extruders_ids() const
{
    return m_impl->used_extruders_ids();
}

float Viewer::used_extruder_used_filament_length(uint8_t extruder_id) const
{
    return m_impl->used_extruder_used_filament_length(extruder_id);
}

float Viewer::used_extruder_used_filament_mass(uint8_t extruder_id) const
{
    return m_impl->used_extruder_used_filament_mass(extruder_id);
}

TimeModes Viewer::time_modes() const
{
    return m_impl->time_modes();
}

size_t Viewer::vertices_count() const
{
    return m_impl->vertices_count();
}

const MoveVertices& Viewer::vertices() const
{
    return m_impl->vertices();
}

const MoveVertex& Viewer::current_vertex() const
{
    return m_impl->current_vertex();
}

size_t Viewer::current_vertex_id() const
{
    return m_impl->current_vertex_id();
}

const MoveVertex& Viewer::vertex_at(size_t id) const
{
    return m_impl->vertex_at(id);
}

float Viewer::estimated_time() const
{
    return m_impl->estimated_time();
}

float Viewer::estimated_time_at(size_t id) const
{
    return m_impl->estimated_time_at(id);
}

ColorRGB Viewer::vertex_color(const MoveVertex& vertex) const
{
    return m_impl->vertex_color(vertex);
}

size_t Viewer::extrusion_roles_count() const
{
    return m_impl->extrusion_roles_count();
}

GCodeExtrusionRoles Viewer::extrusion_roles() const
{
    return m_impl->extrusion_roles();
}

size_t Viewer::visible_extrusion_roles_count() const
{
    return m_impl->visible_extrusion_roles_count();
}

GCodeExtrusionRoles Viewer::visible_extrusion_roles() const
{
    return m_impl->visible_extrusion_roles();
}

size_t Viewer::options_count() const
{
    return m_impl->options_count();
}

const OptionTypes& Viewer::options() const
{
    return m_impl->options();
}

size_t Viewer::visible_options_count() const
{
    return m_impl->visible_options_count();
}

OptionTypes Viewer::visible_options() const
{
    return m_impl->visible_options();
}

size_t Viewer::extruder_color_prints_count(uint8_t extruder_id) const
{
    return m_impl->used_extruder_color_prints_count(extruder_id);
}

ColorPrints Viewer::extruder_color_prints(uint8_t extruder_id) const
{
    return m_impl->used_extruder_color_prints(extruder_id);
}

float Viewer::extrusion_role_estimated_time(Domain::GCodeExtrusionRole role) const
{
    return m_impl->extrusion_role_estimated_time(role);
}

float Viewer::extrusion_role_used_filament_length(Domain::GCodeExtrusionRole role) const
{
    return m_impl->extrusion_role_used_filament_length(role);
}

float Viewer::extrusion_role_used_filament_mass(Domain::GCodeExtrusionRole role) const
{
    return m_impl->extrusion_role_used_filament_mass(role);
}

float Viewer::option_estimated_time(OptionType type) const
{
    return m_impl->option_estimated_time(type);
}

std::vector<float> Viewer::layers_estimated_times() const
{
    return m_impl->layers_estimated_times();
}

size_t Viewer::gcode_events_count() const
{
    return m_impl->gcode_events_count();
}

const GCodeEvents& Viewer::gcode_events() const
{
    return m_impl->gcode_events();
}

BoundingBoxf3 Viewer::bounding_box(const MoveTypes& types) const
{
    return m_impl->bounding_box(types);
}

BoundingBoxf3 Viewer::extrusion_bounding_box(const GCodeExtrusionRoles& roles) const
{
    return m_impl->extrusion_bounding_box(roles);
}

Vec3f Viewer::cog_marker_position() const
{
    return m_impl->cog_marker_position();
}

float Viewer::cog_marker_scale_factor() const
{
    return m_impl->cog_marker_scale_factor();
}

void Viewer::set_cog_marker_scale_factor(float factor)
{
    m_impl->set_cog_marker_scale_factor(factor);
}

bool Viewer::tool_marker_enabled() const
{
    return m_impl->tool_marker_enabled();
}

void Viewer::set_tool_marker_enabled(bool enabled)
{
    m_impl->set_tool_marker_enabled(enabled);
}

float Viewer::tool_marker_offset_z() const
{
    return m_impl->tool_marker_offset_z();
}

void Viewer::set_tool_marker_offset_z(float offset_z)
{
    m_impl->set_tool_marker_offset_z(offset_z);
}

float Viewer::tool_marker_scale_factor() const
{
    return m_impl->tool_marker_scale_factor();
}

void Viewer::set_tool_marker_scale_factor(float factor)
{
    m_impl->set_tool_marker_scale_factor(factor);
}

const ColorRGB& Viewer::tool_marker_color() const
{
    return m_impl->tool_marker_color();
}

void Viewer::set_tool_marker_color(const ColorRGB& color)
{
    m_impl->set_tool_marker_color(color);
}

float Viewer::tool_marker_alpha() const
{
    return m_impl->tool_marker_alpha();
}

void Viewer::set_tool_marker_alpha(float alpha)
{
    m_impl->set_tool_marker_alpha(alpha);
}

BoundingBoxf3 Viewer::tool_marker_bounding_box() const
{
    return m_impl->tool_marker_bounding_box();
}

bool Viewer::export_toolpaths_to_obj(FILE& obj_file, FILE& mtl_file, const ObjExportParams& params) const
{
    return m_impl->export_toolpaths_to_obj(obj_file, mtl_file, params);
}

} // namespace Slic3r::App::libvgcode
