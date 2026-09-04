#include "Slic3r/App/Preview/FdmViewerWrapper.hpp"

#include "Slic3r/App/Preview/LegendWindow.hpp"
#include "Slic3r/App/Imgui/ImguiExtension.hpp"
#include <Slic3r/App/libvgcode/ColorRange.hpp>

#include "Slic3r/Biz/I18N/I18N.hpp"
#include <Slic3r/Biz/Algorithms/Color.hpp>
#include <Slic3r/Biz/libpgcode/Utils.hpp>
#include <Slic3r/Domain/Color.hpp>

#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Domain/TemplateUtils.hpp"

#include <algorithm>

using namespace Slic3r::Biz::libpgcode;
using namespace Slic3r::App::libvgcode;
using namespace Slic3r::Biz;

using Slic3r::Domain::ColorRGB;
using Slic3r::Domain::GCodeExtrusionRole;

using Slic3r::Biz::Algorithms::Color::encode_color;
using Slic3r::Biz::Algorithms::Color::decode_color;

namespace CustomGCode = Slic3r::Domain::CustomGCode;

namespace Slic3r::App::Preview {

static const std::vector<Palette> PREDEFINED_PALETTES = {
    // palette 1  
    { { 1.00f, 1.00f, 0.80f }, { 1.00f, 0.93f, 0.63f }, { 1.00f, 0.85f, 0.47f },
      { 1.00f, 0.70f, 0.29f }, { 1.00f, 0.56f, 0.24f }, { 0.99f, 0.31f, 0.16f },
      { 0.89f, 0.10f, 0.11f }, { 0.75f, 0.00f, 0.15f }, { 0.50f, 0.00f, 0.15f } },

    // palette 2
    { { 0.65f, 0.00f, 0.15f }, { 0.85f, 0.19f, 0.15f }, { 0.96f, 0.43f, 0.26f },
      { 1.00f, 0.69f, 0.38f }, { 1.00f, 1.00f, 0.57f }, { 1.00f, 1.00f, 0.75f }, 
      { 0.89f, 0.96f, 0.98f }, { 0.67f, 0.85f, 0.92f }, { 0.45f, 0.68f, 0.82f },
      { 0.27f, 0.69f, 0.71f }, { 0.19f, 0.21f, 0.58f } },
    // palette 3
    DEFAULT_RANGES_COLORS
};

bool FdmViewerWrapper::init(Render::Device& device, Scene::Scene& scene, Scene::GeometryDataFactory& data_factory)
{
    try {
        m_viewer.init(device, scene, data_factory);
        return true;
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR(e.what());
        return false;
    }
}

bool FdmViewerWrapper::set_settings(const FdmViewerWrapperSettings &settings)
{
    m_settings = settings;

    try {
        m_slider_layers = Yoga::Passthrough<DoubleSliderForLayers>(std::make_unique<DoubleSliderForLayers>());
        set_layers_slider_base_flags(m_settings.layers_slider_base_flags);
        m_slider_layers->seq_top_layer_only(m_settings.seq_top_layer_only);
        m_slider_layers->set_use_default_colors(m_settings.slider_layers_use_default_colors);
        // set layers slider callbacks
        m_slider_layers->set_app_config_changed_callback(m_settings.layers_slider_base_callbacks.app_config_changed);
        m_slider_layers->set_request_extra_frames_callback(std::bind(&FdmViewerWrapper::on_request_extra_frames, this, std::placeholders::_1));
        m_slider_layers->set_on_thumb_move_callback(std::bind(&FdmViewerWrapper::on_slider_layers_scroll_changed, this));
        m_slider_layers->set_notify_empty_color_change_gcode_callback(m_settings.cb_slider_layers_notify_empty_color_change_gcode);
        m_slider_layers->set_ticks_changed_callback(std::bind(&FdmViewerWrapper::on_slider_layers_ticks_changed, this));
        m_slider_layers->set_get_extruder_colors_callback(std::bind(&FdmViewerWrapper::on_slider_layers_get_extruder_colors, this));
        m_slider_layers->set_auto_color_change_callback(m_settings.cb_slider_layers_auto_color_change);
        m_slider_layers->set_notify_empty_auto_color_change_callback(m_settings.cb_slider_layers_notify_empty_auto_color_change);
        m_slider_layers->set_get_extruders_sequence_callback(m_settings.cb_slider_layers_get_extruders_sequence);
        m_slider_layers->set_show_info_msg_callback(m_settings.cb_slider_layers_show_info_msg);
        m_slider_layers->set_get_gcode_callback(std::bind(&FdmViewerWrapper::on_slider_layers_get_gcode, this, std::placeholders::_1));
        m_slider_layers->set_get_used_extruders_in_print_callback(m_settings.cb_slider_layers_get_used_extruders_in_print);

        m_slider_gcode = Yoga::Passthrough<DoubleSliderForGcode>{std::make_unique<DoubleSliderForGcode>()};
        // set gcode slider callbacks
        m_slider_gcode->set_request_extra_frames_callback(std::bind(&FdmViewerWrapper::on_request_extra_frames, this, std::placeholders::_1));
        m_slider_gcode->set_on_thumb_move_callback(std::bind(&FdmViewerWrapper::on_slider_gcode_scroll_changed, this));

        if (m_viewer.is_top_layer_only_view_range() != settings.seq_top_layer_only)
            m_viewer.toggle_top_layer_only_view_range();

        m_legend = Yoga::Passthrough<LegendWindow>(std::make_unique<LegendWindow>(&m_viewer, this));
        m_legend->callbacks().cb_extrusion_role_visibility_changed = std::bind(&FdmViewerWrapper::on_extrusion_role_visibility_changed, this);
        m_legend->callbacks().cb_view_type_changed = m_settings.cb_gcode_view_type_changed;

        m_gcode_window = Yoga::Passthrough<GCodeWindow>(std::make_unique<GCodeWindow>(&m_viewer, &m_gcode_window_data));

        return true;
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR(e.what());
        return false;
    }
}

void FdmViewerWrapper::render_scene()
{
    render_toolpaths();
}

void FdmViewerWrapper::render_imgui()
{

}
static void set_pregcode_extrusion_role_colors(FdmViewerWrapper& wrapper)
{
    wrapper.set_extrusion_role_color(GCodeExtrusionRole::Skirt,                    ColorRGB(0.5f, 1.0f, 0.5f));
    wrapper.set_extrusion_role_color(GCodeExtrusionRole::ExternalPerimeter,        ColorRGB(1.0f, 1.0f, 0.0f));
    wrapper.set_extrusion_role_color(GCodeExtrusionRole::SupportMaterial,          ColorRGB(0.5f, 1.0f, 0.5f));
    wrapper.set_extrusion_role_color(GCodeExtrusionRole::SupportMaterialInterface, ColorRGB(0.5f, 1.0f, 0.5f));
    wrapper.set_extrusion_role_color(GCodeExtrusionRole::InternalInfill,           ColorRGB(1.0f, 0.5f, 0.5f));
    wrapper.set_extrusion_role_color(GCodeExtrusionRole::SolidInfill,              ColorRGB(1.0f, 0.5f, 0.5f));
    wrapper.set_extrusion_role_color(GCodeExtrusionRole::WipeTower,                ColorRGB(0.5f, 1.0f, 0.5f));
}

void FdmViewerWrapper::set_mode(FdmViewerWrapperMode mode)
{
    m_settings.mode = mode;
    m_slider_layers->enable_editing(m_settings.mode != FdmViewerWrapperMode::GCodeViewer);
    m_legend_params.settings_visible = (m_settings.mode == FdmViewerWrapperMode::GCodeViewer);
    m_legend_params.enabled = (m_settings.mode != FdmViewerWrapperMode::EditorPreGCode);
    if (m_settings.mode == FdmViewerWrapperMode::EditorPreGCode) {
        set_pregcode_extrusion_role_colors(*this);
        m_viewer.set_view_type(ViewType::FeatureType);
    }
    else {
        reset_default_extrusion_roles_colors();
    }
}

void FdmViewerWrapper::reset()
{
    m_viewer.reset();
    m_gcode_window_data.reset();
}

void FdmViewerWrapper::load(
    FdmViewerWrapperInputData&& wrapper_data,
    FdmViewerInputData&& data,
    const Scene::Transform& transform
)
{
    m_loading = true;

    m_data = std::move(wrapper_data);

    if (data.gcode->empty() && m_settings.mode == FdmViewerWrapperMode::EditorGCode)
        set_mode(FdmViewerWrapperMode::EditorPreGCode);
    else if (!data.gcode->empty() && m_settings.mode == FdmViewerWrapperMode::EditorPreGCode)
        set_mode(FdmViewerWrapperMode::EditorGCode);

    m_gcode_window_data.set_gcode(data.gcode);
    m_viewer.load(std::move(data), transform);

    update_legend_type_selector();
    update_slider_layers();

    m_loading = false;
}

static Domain::BasicPrintStatistics get_basic_statistics(const Domain::PrintStatistics& stats)
{
    return std::visit(
        Domain::overloaded{
            [](const Domain::FullPrintStatistics& stats)
            {
                Domain::BasicPrintStatistics result;

                result.normal_mode_time         = stats.normal_mode_time;
                result.silent_mode_time         = stats.silent_mode_time;
                result.volumes_per_color_change = stats.used_filament_per_color_change_cm3;
                for (unsigned extruder : stats.printing_extruders) {
                    result.volumes_per_extruder[extruder] =
                        stats.used_filament_per_extruder_cm3[extruder] * 1000;
                    result.cost_per_extruder[extruder] = stats.filament_cost_per_extruder[extruder];
                }
                result.used_filaments_per_role = stats.used_filaments_per_role;
                return result;
            },
            [](const Domain::BasicPrintStatistics& stats) { return stats; }
        },
        stats
    );
}

static FdmViewerInputData extract_viewer_input_data_from_result(const ProcessorResult& result)
{
    FdmViewerInputData ret;

    std::vector<std::string> str_tool_colors = result.extruder_str_colors;
    std::vector<std::string> str_color_print_colors;
    if (!result.custom_gcode_per_print_z.empty()) {
        str_color_print_colors = result.extruder_str_colors;
        for (const CustomGCode::Item& code : result.custom_gcode_per_print_z) {
            if (code.type == CustomGCode::Type::ColorChange)
                str_color_print_colors.emplace_back(code.color);
        }
        str_color_print_colors.push_back(DUMMY_STR_COLOR);
    }

    ret.tools_colors.reserve(str_tool_colors.size());
    for (const std::string& str_color : str_tool_colors) {
        ColorRGB color;
        decode_color(str_color, color);
        ret.tools_colors.emplace_back(color);
    }

    const std::vector<std::string>& str_colors = str_color_print_colors.empty() ? str_tool_colors : str_color_print_colors;
    ret.color_print_colors.reserve(str_colors.size());
    for (const std::string& str_color : str_colors) {
        ColorRGB color;
        decode_color(str_color, color);
        ret.color_print_colors.emplace_back(color);
    }

    const Domain::BasicPrintStatistics print_statistics{
        get_basic_statistics(result.print_statistics)
    };

    for (const auto& [role, values] : print_statistics.used_filaments_per_role) {
        float length = values.first;
        float mass   = values.second;
        ret.used_filament_by_roles.insert({ role, { length, mass } });
    }

    for (const auto& [extruder_id, volume] : print_statistics.volumes_per_extruder) {
        float v = 0.001f * volume;
        float length = v / result.filament_geometry(extruder_id).area_cross_section;
        float mass = v * result.filament_densities[extruder_id];
        ret.used_filament_by_extruders.insert({ extruder_id, { length, mass } });
    }

    std::array<size_t, TIME_MODES_COUNT> shifts = {};
    size_t color_changes_count = 0;
    for (size_t i = 0; i < result.custom_gcode_per_print_z.size(); ++i) {
        const auto& item = result.custom_gcode_per_print_z[i];
        assert(item.extruder > 0);
        std::array<float, TIME_MODES_COUNT> times = {};
        std::array<float, 2> used_filament = { 0.0f, 0.0f };

        const Domain::TimeStatistics& normal_mode = print_statistics.normal_mode_time;
        auto it = std::find_if(normal_mode.custom_gcode_times.begin() + shifts[0], normal_mode.custom_gcode_times.end(),
            [&item](const std::pair<CustomGCode::Type, std::pair<float, float>>& gc_item) { return gc_item.first == item.type; });
        if (it != normal_mode.custom_gcode_times.end()) {
            shifts[0] = std::distance(normal_mode.custom_gcode_times.begin(), it) + 1;
            times[0] = it->second.first;
        }

        const auto& silent_mode_opt = print_statistics.silent_mode_time;
        if (silent_mode_opt) {
            const Domain::TimeStatistics& silent_mode = *silent_mode_opt;
            auto it = std::find_if(silent_mode.custom_gcode_times.begin() + shifts[1], silent_mode.custom_gcode_times.end(),
                [&item](const std::pair<CustomGCode::Type, std::pair<float, float>>& gc_item) { return gc_item.first == item.type; });
            if (it != silent_mode.custom_gcode_times.end()) {
                shifts[1] = std::distance(silent_mode.custom_gcode_times.begin(), it) + 1;
                times[1] = it->second.first;
            }
        }

        auto* basic_print_stats{
            std::get_if<Domain::BasicPrintStatistics>(&result.print_statistics)
        };
        if (basic_print_stats && item.type == CustomGCode::Type::ColorChange) {
            const std::vector<float>& volumes_per_color_change{
                basic_print_stats->volumes_per_color_change
            };
            if (!volumes_per_color_change.empty()) {
                float volume  = 0.001f * volumes_per_color_change[color_changes_count++];
                used_filament = {
                    volume
                        / result.filament_geometry(uint8_t(item.extruder - 1)).area_cross_section,
                    volume * result.filament_densities[item.extruder - 1]
                };
            }
        }
        ret.gcode_events.push_back({item.type, uint8_t(item.extruder - 1), times, used_filament});
    }

    ret.extruders_count = result.extruders_count;
    ret.gcode = result.const_gcode();
    ret.vertices = result.const_moves();

    return ret;
}

struct WipeTowerAndFlushFilamentUsageData
{
    WipeTowerAndFlushFilamentUsagePerExtruder usage_per_extruder;
    bool has_wipe_tower_filament{false};
    bool has_flush_filament{false};
};

static float value_or_zero(const std::vector<float>& values, size_t index)
{
    return index < values.size() ? values[index] : 0.f;
}

static WipeTowerAndFlushFilamentUsageData extract_wipe_tower_and_flush_filament_usage(
    const ProcessorResult& result
)
{
    return std::visit(
        Domain::overloaded{
            [](const Domain::FullPrintStatistics& stats)
            {
                WipeTowerAndFlushFilamentUsageData ret;
                ret.has_wipe_tower_filament = stats.total_used_filament_for_wipe_tower_mm > 0.f;
                ret.has_flush_filament      = stats.total_used_filament_for_flush_mm > 0.f;

                const size_t extruders_count{std::max(
                    {stats.used_filament_for_wipe_tower_per_extruder_mm.size(),
                     stats.used_filament_for_wipe_tower_per_extruder_g.size(),
                     stats.used_filament_for_flush_per_extruder_mm.size(),
                     stats.used_filament_for_flush_per_extruder_g.size()}
                )};

                for (size_t extruder_id = 0; extruder_id < extruders_count; ++extruder_id) {
                    const WipeTowerAndFlushFilamentUsage usage{
                        .wipe_tower_m = 0.001f
                            * value_or_zero(stats.used_filament_for_wipe_tower_per_extruder_mm,
                                            extruder_id),
                        .wipe_tower_g = value_or_zero(
                            stats.used_filament_for_wipe_tower_per_extruder_g,
                            extruder_id
                        ),
                        .flush_m = 0.001f
                            * value_or_zero(
                                       stats.used_filament_for_flush_per_extruder_mm,
                                       extruder_id
                            ),
                        .flush_g =
                            value_or_zero(stats.used_filament_for_flush_per_extruder_g, extruder_id)
                    };

                    const bool has_wipe_tower{usage.wipe_tower_m > 0.f || usage.wipe_tower_g > 0.f};
                    const bool has_flush{usage.flush_m > 0.f || usage.flush_g > 0.f};

                    ret.has_wipe_tower_filament |= has_wipe_tower;
                    ret.has_flush_filament |= has_flush;
                    if (has_wipe_tower || has_flush) {
                        ret.usage_per_extruder[static_cast<uint8_t>(extruder_id)] = usage;
                    }
                }

                return ret;
            },
            [&result](const Domain::BasicPrintStatistics& stats)
            {
                const auto length_from_volume = [&result](uint8_t extruder_id, float volume)
                {
                    if (extruder_id >= result.extruders_count) {
                        return 0.f;
                    }

                    const float area{result.filament_geometry(extruder_id).area_cross_section};
                    return area > 0.f ? volume / area * 0.001f : 0.f;
                };

                const auto mass_from_volume = [&result](uint8_t extruder_id, float volume)
                {
                    return extruder_id < result.filament_densities.size() ?
                        volume * result.filament_densities[extruder_id] * 0.001f :
                        0.f;
                };

                WipeTowerAndFlushFilamentUsageData ret;
                for (const auto& [extruder_id, volume] : stats.wipe_tower_volumes_per_extruder) {
                    if (volume <= 0.f) {
                        continue;
                    }

                    ret.has_wipe_tower_filament = true;

                    WipeTowerAndFlushFilamentUsage& usage{ret.usage_per_extruder[extruder_id]};
                    usage.wipe_tower_m = length_from_volume(extruder_id, volume);
                    usage.wipe_tower_g = mass_from_volume(extruder_id, volume);
                }

                for (const auto& [extruder_id, volume] : stats.flush_volumes_per_extruder) {
                    if (volume <= 0.f) {
                        continue;
                    }

                    ret.has_flush_filament = true;

                    WipeTowerAndFlushFilamentUsage& usage{ret.usage_per_extruder[extruder_id]};
                    usage.flush_m = length_from_volume(extruder_id, volume);
                    usage.flush_g = mass_from_volume(extruder_id, volume);
                }

                return ret;
            }
        },

        result.print_statistics
    );
}

static FdmViewerWrapperInputData extract_wrapper_input_data_from_result(const ProcessorResult& result)
{
    FdmViewerWrapperInputData ret;

    CustomGCode::Info ticks_info_from_model;
    ticks_info_from_model.mode = CustomGCode::Mode::SingleExtruder;// should be  Undef; -> ysFIXME-spe3494.
    ticks_info_from_model.gcodes = result.custom_gcode_per_print_z;
    ret.producer = result.producer;
    ret.custom_gcode_info = ticks_info_from_model;
    ret.print_settings = result.print_settings;
    ret.sequential_print = result.sequential_print;
    ret.color_change_gcode = result.color_change_gcode;
    ret.pause_print_gcode = result.pause_print_gcode;
    ret.template_custom_gcode = result.template_custom_gcode;

    WipeTowerAndFlushFilamentUsageData usage_data{
        extract_wipe_tower_and_flush_filament_usage(result)
    };
    ret.wipe_tower_and_flush_filament_usage = std::move(usage_data.usage_per_extruder);
    ret.has_wipe_tower_filament             = usage_data.has_wipe_tower_filament;
    ret.has_flush_filament                  = usage_data.has_flush_filament;

    return ret;
}

void
FdmViewerWrapper::load_from_result(const ProcessorResult& result, const Scene::Transform& transform)
{
    FdmViewerInputData viewer_data = extract_viewer_input_data_from_result(result);
    FdmViewerWrapperInputData wrapper_data = extract_wrapper_input_data_from_result(result);

    load(std::move(wrapper_data), std::move(viewer_data), transform);
}

void FdmViewerWrapper::render_toolpaths()
{
    m_viewer.render();
}

std::unique_ptr<GCodeWindow> FdmViewerWrapper::unload_gcode_window()
{
    return m_gcode_window.release();
}

std::unique_ptr<LegendWindow> FdmViewerWrapper::unload_legend()
{
    return  m_legend.release();
}

std::unique_ptr<DoubleSliderForGcode> FdmViewerWrapper::unload_double_slider_gcode()
{
    return m_slider_gcode.release();
}

void FdmViewerWrapper::set_units(UnitsSystem sys)
{
    m_units = sys;
    m_slider_layers->set_units(sys);
}

const WipeTowerAndFlushFilamentUsagePerExtruder&
FdmViewerWrapper::wipe_tower_and_flush_filament_usage() const
{
    return m_data.wipe_tower_and_flush_filament_usage;
}

bool FdmViewerWrapper::has_wipe_tower_filament() const
{
    return m_data.has_wipe_tower_filament;
}

bool FdmViewerWrapper::has_flush_filament() const
{
    return m_data.has_flush_filament;
}

void FdmViewerWrapper::set_layers_range(Interval::value_type min, Interval::value_type max)
{
    m_viewer.set_layers_range(min, max);
    update_slider_gcode();
}

void FdmViewerWrapper::set_range_colors_popup_type(ViewType type)
{
    m_range_colors_popup_type = type;
    // force dimmed background without animation
    Imgui::disable_background_fadeout_animation();
}

void FdmViewerWrapper::set_radius_popup_type(MoveType type)
{
    m_radius_popup_type = type;
    // force dimmed background without animation
    Imgui::disable_background_fadeout_animation();
}

void FdmViewerWrapper::set_scale_factor_popup_type(OptionType type)
{
    m_scale_factor_popup_type = type;
    // force dimmed background without animation
    Imgui::disable_background_fadeout_animation();
}

void FdmViewerWrapper::update_slider_gcode(std::optional<size_t> visible_range_min, std::optional<size_t> visible_range_max)
{
    if (!has_data())
        return;

    if (!m_slider_gcode->is_visible())
        return;

    const Interval& range = m_viewer.view_enabled_range();
    uint32_t last_gcode_id = m_viewer.vertex_at(range[0]).gcode_id;
    std::optional<uint32_t> gcode_id_min = visible_range_min.has_value() ?
        std::optional<uint32_t>(m_viewer.vertex_at(*visible_range_min).gcode_id) : std::nullopt;
    std::optional<uint32_t> gcode_id_max = visible_range_max.has_value() ?
        std::optional<uint32_t>(m_viewer.vertex_at(*visible_range_max).gcode_id) : std::nullopt;

    size_t range_size = range[1] - range[0] + 1;
    std::vector<unsigned int> values;
    values.reserve(range_size);
    std::vector<unsigned int> alternate_values;
    alternate_values.reserve(range_size);

    std::optional<uint32_t> visible_range_min_id;
    std::optional<uint32_t> visible_range_max_id;
    uint32_t counter = 0;

    for (size_t i = range[0]; i <= range[1]; ++i) {
        uint32_t gcode_id = m_viewer.vertex_at(i).gcode_id;
        bool skip = false;
        if (i > range[0]) {
            // skip consecutive moves with same gcode id (resulting from processing G2 and G3 lines)
            if (last_gcode_id == gcode_id) {
                values.back() = static_cast<unsigned int>(i + 1);
                skip = true;
            }
            else
                last_gcode_id = gcode_id;
        }

        if (!skip) {
            values.emplace_back(static_cast<unsigned int>(i + 1));
            alternate_values.emplace_back(gcode_id);
            if (gcode_id_min.has_value() && alternate_values.back() == *gcode_id_min)
                visible_range_min_id = counter;
            else if (gcode_id_max.has_value() && alternate_values.back() == *gcode_id_max)
                visible_range_max_id = counter;
            ++counter;
        }
    }

    int span_min_id = visible_range_min_id.has_value() ? *visible_range_min_id : 0;
    int span_max_id = visible_range_max_id.has_value() ? *visible_range_max_id : int(values.size()) - 1;

    int max_pos = values.empty() ? 0 : int(values.size() - 1);
    m_slider_gcode->set_slider_values(std::move(values));
    m_slider_gcode->set_slider_alternate_values(std::move(alternate_values));
    m_slider_gcode->freeze();
    m_slider_gcode->set_max_pos(max_pos);
    m_slider_gcode->set_selection_span(span_min_id, span_max_id);
    m_slider_gcode->thaw();
    m_slider_gcode->show_lower_thumb(!m_viewer.is_top_layer_only_view_range());
}

static bool has_layer_times(const libvgcode::FdmViewer& viewer)
{
    std::vector<float> layers_times = viewer.layers_estimated_times();
    return !layers_times.empty() && layers_times.size() == viewer.layers_count();
}

void FdmViewerWrapper::update_legend_type_selector()
{
    bool has_layers_times = has_layer_times(m_viewer);
    const std::array<ViewType, 2> layer_times_types = { ViewType::LayerTimeLinear, ViewType::LayerTimeLogarithmic };

    std::vector<ViewType> options;
    options.reserve(VIEW_TYPES_COUNT);
    for (int i = 0; i < int(VIEW_TYPES_COUNT); ++i) {
        ViewType type = ViewType(i);
        if (has_layers_times ||
            std::find(layer_times_types.begin(), layer_times_types.end(), type) == layer_times_types.end())
            options.emplace_back(type);
    }

    ViewType selection = m_viewer.view_type();
    if (!has_layers_times &&
        std::find(layer_times_types.begin(), layer_times_types.end(), selection) != layer_times_types.end())
        selection = ViewType::FeatureType;
    int selection_id = int(std::distance(options.begin(), std::find(options.begin(), options.end(), selection)));

    std::vector<std::string> types;
    types.reserve(options.size());
    for (ViewType type : options) {
        types.emplace_back(to_string(type));
    }

    m_legend->update_type_selector(types, options, selection_id);
}

void FdmViewerWrapper::set_view_type(ViewType type)
{
    m_viewer.set_view_type(type);
    update_legend_type_selector();
}

bool FdmViewerWrapper::is_view_type_available(ViewType type) const
{
    if (type == ViewType::LayerTimeLinear || type == ViewType::LayerTimeLogarithmic)
        return has_layer_times(m_viewer);
    return true;
}

static void adjust_ticks_values(std::vector<CustomGCode::Item>& gcodes, const std::vector<float>& zs)
{
    // All ticks that would end up outside the slider range should be erased.
    // TODO: this should be placed into more appropriate part of code,
    // this function is e.g. not called when the last object is deleted
    gcodes.erase(std::remove_if(gcodes.begin(), gcodes.end(), [zs](const CustomGCode::Item& item) {
        return std::lower_bound(zs.begin(), zs.end(), item.print_z - Domain::EPSILON) == zs.end();
    }), gcodes.end());
}

static std::vector<std::string> convert(const Palette& palette)
{
    std::vector<std::string> ret;
    ret.reserve(palette.size());
    for (const ColorRGB& c : palette) {
        ret.emplace_back(encode_color(c));
    }
    return ret;
}

void FdmViewerWrapper::update_slider_layers()
{
    // Save the initial slider span.
    float z_low = m_slider_layers->lower_value();
    float z_high = m_slider_layers->higher_value();
    bool was_empty = m_slider_layers->max_pos() == 0;

    std::vector<float> layers_zs = m_viewer.layers_zs();

    bool force_sliders_full_range = was_empty || layers_zs.empty() || std::abs(layers_zs.back() - m_slider_layers->max_value()) > Domain::EPSILON;
    bool snap_to_min = force_sliders_full_range || m_slider_layers->is_lower_at_min();
    bool snap_to_max = force_sliders_full_range || m_slider_layers->is_higher_at_max();

    int max_pos = layers_zs.empty() ? 0 : int(layers_zs.size()) - 1;

    size_t old_ticks_size = m_data.custom_gcode_info.gcodes.size();
    adjust_ticks_values(m_data.custom_gcode_info.gcodes, layers_zs);
    size_t new_ticks_size = m_data.custom_gcode_info.gcodes.size();
    if (old_ticks_size != new_ticks_size) {
        if (m_settings.cb_invalidate_slice != nullptr)
            m_settings.cb_invalidate_slice();
    }

    int idx_low = 0;
    int idx_high = max_pos;
    if (!layers_zs.empty()) {
        if (!snap_to_min) {
            int idx_new = DoubleSliderForLayers::find_close_layer_idx(layers_zs, z_low, float(Domain::EPSILON));
            if (idx_new != -1)
                idx_low = idx_new;
        }
        if (!snap_to_max) {
            int idx_new = DoubleSliderForLayers::find_close_layer_idx(layers_zs, z_high, float(Domain::EPSILON));
            if (idx_new != -1)
                idx_high = idx_new;
        }
    }

    m_slider_layers->set_extruder_colors(convert(m_viewer.tool_colors()));
    bool one_extruder_printed_model = used_extruders_count() == 1;
    int8_t only_extruder = (one_extruder_printed_model && m_viewer.extruders_count() > 1) ? m_viewer.used_extruders_ids().front() : -1;
    m_slider_layers->set_slider_values(std::move(layers_zs));
    m_slider_layers->set_ticks_values(m_data.custom_gcode_info);
    m_slider_layers->set_mode_and_only_extruder(one_extruder_printed_model, only_extruder);
    m_slider_layers->force_ruler_update();
    assert(m_slider_layers->min_pos() == 0);
    m_slider_layers->freeze();
    m_slider_layers->set_max_pos(max_pos);
    m_slider_layers->set_selection_span(idx_low, idx_high);
    m_slider_layers->set_draw_mode(false, m_data.sequential_print);

    if (!m_data.keep_layers_times)
        m_slider_layers->set_layers_times(m_viewer.layers_estimated_times(), m_viewer.estimated_time());

    m_slider_layers->thaw();

    if (m_settings.cb_update_layers_slider != nullptr)
        m_settings.cb_update_layers_slider(m_data.custom_gcode_info);
}

void FdmViewerWrapper::update_view_visible_range(size_t first, size_t last)
{
    m_viewer.set_view_visible_range(first, last);
    const Interval& enabled_range = m_viewer.view_enabled_range();
    if (enabled_range[1] != m_viewer.view_visible_range()[1]) {
        m_actual_speed_plot_data.levels.clear();
        m_actual_speed_plot_data.data.clear();
        const ColorRange& color_range = m_viewer.color_range(ViewType::ActualSpeed);
        m_actual_speed_plot_data.y_range = color_range.range();
        const MoveVertex& curr_vertex = m_viewer.current_vertex();
        if (curr_vertex.is_extrusion() || curr_vertex.is_travel() || curr_vertex.is_wipe() || curr_vertex.type == MoveType::Seam) {
            size_t vertices_count = m_viewer.vertices_count();
            // collect vertices sharing the same gcode_id
            size_t curr_id = m_viewer.current_vertex_id();
            size_t start_id = curr_id;
            while (start_id > 0) {
                --start_id;
                if (curr_vertex.gcode_id != m_viewer.vertex_at(start_id).gcode_id)
                    break;
            }
            size_t end_id = curr_id;
            while (end_id < vertices_count - 1) {
                ++end_id;
                if (curr_vertex.gcode_id != m_viewer.vertex_at(end_id).gcode_id)
                    break;
            }

            if (m_viewer.vertex_at(end_id - 1).type == MoveType::Seam)
                --end_id;

            assert(end_id - start_id >= 2);

            float total_len = 0.0f;
            for (size_t i = start_id; i < end_id; ++i) {
                const MoveVertex& v = m_viewer.vertex_at(i);
                float len = (i > start_id) ? (v.position - m_viewer.vertex_at(i - 1).position).norm() : 0.0f;
                total_len += len;
                if (i == start_id || len > float(Domain::EPSILON))
                    m_actual_speed_plot_data.data.push_back({ total_len, v.actual_feedrate, v.time[0] == 0.0f });
            }

            std::vector<float> values = color_range.values();
            for (float value : values) {
                m_actual_speed_plot_data.levels.push_back({ value, color_range.color_at(value) });
            }
        }
    }

    if (m_settings.layers_slider_base_callbacks.request_extra_frames != nullptr)
        m_settings.layers_slider_base_callbacks.request_extra_frames(1);
}

void FdmViewerWrapper::on_slider_layers_scroll_changed()
{
    if (m_slider_layers->is_visible()) {
        set_layers_range(uint32_t(m_slider_layers->lower_pos()), uint32_t(m_slider_layers->higher_pos()));
        if (m_settings.layers_slider_base_callbacks.on_thumb_move != nullptr)
            m_settings.layers_slider_base_callbacks.on_thumb_move();
    }
}

void FdmViewerWrapper::on_slider_gcode_scroll_changed()
{
    if (m_slider_gcode->is_visible()) {
        update_view_visible_range(size_t(m_slider_gcode->lower_value() - 1), size_t(m_slider_gcode->higher_value() - 1));
        if (m_settings.cb_slider_gcode_on_thumb_move != nullptr)
            m_settings.cb_slider_gcode_on_thumb_move();
    }
}

void FdmViewerWrapper::on_extrusion_role_visibility_changed()
{
    const Interval& range = m_viewer.view_visible_range();
    update_slider_gcode(range[0], range[1]);
}

void FdmViewerWrapper::on_request_extra_frames(unsigned int count)
{
    if (m_settings.layers_slider_base_callbacks.request_extra_frames != nullptr)
        m_settings.layers_slider_base_callbacks.request_extra_frames(count);
}

void FdmViewerWrapper::on_slider_layers_ticks_changed()
{
    if (!m_loading) {
        m_legend_params.enabled = false;
        ViewType view_type = ViewType::FeatureType;
        if (!slider_layers_ticks_values().gcodes.empty()) {
            view_type = ViewType::ColorPrint;
        }
        set_view_type(view_type);
        set_pregcode_extrusion_role_colors(*this);
        if (m_settings.mode == FdmViewerWrapperMode::EditorGCode)
            set_mode(FdmViewerWrapperMode::EditorPreGCode);
    }

    if (m_settings.cb_slider_layers_ticks_changed != nullptr)
        m_settings.cb_slider_layers_ticks_changed();
}

std::string FdmViewerWrapper::on_slider_layers_get_gcode(CustomGCode::Type type)
{
    switch (type) {
    case CustomGCode::Type::ColorChange: { return m_data.color_change_gcode; }
    case CustomGCode::Type::PausePrint:  { return m_data.pause_print_gcode; }
    case CustomGCode::Type::Template:    { return m_data.template_custom_gcode; }
    default:                       { return std::string(); }
    }
}

Palette FdmViewerWrapper::on_slider_layers_get_extruder_colors()
{
    return m_viewer.tool_colors();
}

void FdmViewerWrapper::render_vertex_properties(const WrapperLayoutData& layout)
{
    static bool docked = true;
    static bool actual_speed_graph_visible = false;
    static bool actual_speed_table_visible = false;

    const MoveVertex& vertex = m_viewer.current_vertex();

    const ImGuiViewport& viewport = *ImGui::GetMainViewport();
    if (docked)
        ImGui::SetNextWindowPos({ viewport.Size.x - m_slider_layers->size().x, layout.menubar_height }, ImGuiCond_Always, { 1.0f, 0.0f });
    else
        ImGui::SetNextWindowPos({ viewport.Size.x - m_slider_layers->size().x, layout.menubar_height }, ImGuiCond_Once, { 1.0f, 0.0f });

    Imgui::UnifiedWindowStyle unified_window_style;
    unified_window_style.push();
    std::string title = _u8L("Properties");
    // the following is a hack which allows to properly resize the window, in a single frame,
    // when the user selects a different vertex 
    ImGuiWindow* wnd = ImGui::FindWindowByName(title.c_str());
    if (wnd != nullptr)
        wnd->DC.CursorMaxPos.x = wnd->DC.CursorStartPos.x;
    ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoFocusOnAppearing);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu(_u8L("Options").c_str())) {
            ImGui::MenuItem(_u8L("Docked").c_str(), nullptr, &docked);
            ImGui::MenuItem(_u8L("Show actual speed profile").c_str(), nullptr, &actual_speed_graph_visible);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    if (ImGui::BeginTable("properties", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInner)) {

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", _u8L("G-Code line").c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d", vertex.gcode_id);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", _u8L("Layer").c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d", 1 + vertex.layer_id);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", _u8L("Type").c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%s", to_string(vertex.type).c_str());

        if (vertex.is_extrusion()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", _u8L("Extruder").c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", std::to_string(vertex.extruder_id + 1).c_str());            

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", _u8L("Feature type").c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", to_string(vertex.extrusion_role).c_str());
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", _u8L("Position").c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%s, %s, %s",
            convert_and_format_units(vertex.position.x(), UnitsType::Millimeters, (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches, 3, false).c_str(),
            convert_and_format_units(vertex.position.y(), UnitsType::Millimeters, (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches, 3, false).c_str(),
            convert_and_format_units(vertex.position.z(), UnitsType::Millimeters, (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches, 3).c_str());

        if (vertex.is_extrusion()) {

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", _u8L("Extrusion").c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", convert_and_format_units(vertex.delta_extruder, UnitsType::Millimeters, (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches, (m_units == UnitsSystem::SI) ? 3 : 6).c_str());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", _u8L("Width").c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", convert_and_format_units(vertex.width, UnitsType::Millimeters, (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches, (m_units == UnitsSystem::SI) ? 3 : 6).c_str());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", _u8L("Height").c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", convert_and_format_units(vertex.height, UnitsType::Millimeters, (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches, (m_units == UnitsSystem::SI) ? 3 : 6).c_str());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", _u8L("Speed").c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", convert_and_format_units(vertex.feedrate, UnitsType::MillimetersPerSecond, (m_units == UnitsSystem::SI) ? UnitsType::MillimetersPerSecond : UnitsType::InchesPerSecond, 3).c_str());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", _u8L("Fan speed").c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.0f %%", vertex.fan_speed);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", _u8L("Volumetric flow rate").c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", convert_and_format_units(vertex.volumetric_rate(), UnitsType::MillimetersCubePerSecond, (m_units == UnitsSystem::SI) ? UnitsType::MillimetersCubePerSecond : UnitsType::InchesCubePerSecond, (m_units == UnitsSystem::SI) ? 3 : 6).c_str());
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", _u8L("Temperature").c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%s", convert_and_format_units(vertex.temperature, UnitsType::Celsius, (m_units == UnitsSystem::SI) ? UnitsType::Celsius : UnitsType::Farhenheit).c_str());

        float estimated_time = m_viewer.estimated_time_at(m_viewer.current_vertex_id());
        TimeMode time_mode = m_viewer.time_mode();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", _u8L("Time").c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.3fs (%s)", vertex.time[size_t(time_mode)], format_time_dhms(estimated_time).c_str());

        ImGui::EndTable();

        if (actual_speed_graph_visible) {
            ImVec2 top_pos = ImGui::GetCursorScreenPos();
            ImGui::SeparatorText(_u8L("Actual speed profile").c_str());
            int hover_id = plot_actual_speed_profile(m_actual_speed_plot_data, { -1.0f, 150.0f });
            if (!m_actual_speed_plot_data.data.empty()) {
                if (ImGui::Button(actual_speed_table_visible ? _u8L("Hide table").c_str() : _u8L("Show table").c_str(),
                    { -1.0f, 0.0f }))
                    actual_speed_table_visible = !actual_speed_table_visible;

                ImVec2 bottom_pos = ImGui::GetCursorScreenPos();
                if (actual_speed_table_visible) {
                    static float table_wnd_height = 0.0f;

                    ImVec2 wnd_pos = ImGui::GetWindowPos();
                    ImGui::SetNextWindowPos({ wnd_pos.x, top_pos.y }, ImGuiCond_Always, { 1.0f, 0.0f });
                    ImGui::SetNextWindowSizeConstraints({ 0.0f, 0.0f }, { -1.0f, bottom_pos.y - top_pos.y });
                    Imgui::UnifiedWindowStyle unified_window_style;
                    unified_window_style.push();
                    ImGui::Begin(_u8L("Actual speed profile table").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing);

                    if (ImGui::BeginTable("ActualSpeedTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY)) {
                        char buff[1024];
                        ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
                        sprintf(buff, "%s (%s)", _u8L("Position").c_str(), format_units((m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches).c_str());
                        ImGui::TableSetupColumn(buff);
                        sprintf(buff, "%s (%s)", _u8L("Speed").c_str(), format_units((m_units == UnitsSystem::SI) ? UnitsType::MillimetersPerSecond : UnitsType::InchesPerSecond).c_str());
                        ImGui::TableSetupColumn(buff);
                        ImGui::TableHeadersRow();
                        int counter = 0;
                        for (const ActualSpeedPlotDataItem& item : m_actual_speed_plot_data.data) {
                            bool highlight = hover_id >= 0 && (counter == hover_id || counter == hover_id + 1);
                            if (highlight && counter == hover_id)
                                ImGui::SetScrollHereY();
                            ImGui::TableNextRow();
                            ImU32 row_bg_color = ImGui::GetColorU32(highlight ? ImGuiCol_TableRowBg : ImGuiCol_TableRowBgAlt);
                            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, row_bg_color);
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextColored(ImGui::GetStyleColorVec4(highlight ? ImGuiCol_ButtonHovered : ImGuiCol_Text),
                                "%s", convert_and_format_units(item.position, UnitsType::Millimeters, (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches, 3, false).c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextColored(ImGui::GetStyleColorVec4(highlight ? ImGuiCol_ButtonHovered : ImGuiCol_Text),
                                "%s", convert_and_format_units(item.speed, UnitsType::MillimetersPerSecond, (m_units == UnitsSystem::SI) ? UnitsType::MillimetersPerSecond : UnitsType::InchesPerSecond, 1, false).c_str());
                            ++counter;
                        }
                        ImGui::EndTable();
                    }

                    float curr_table_wnd_height = ImGui::GetWindowHeight();
                    if (table_wnd_height != curr_table_wnd_height) {
                        table_wnd_height = curr_table_wnd_height;
                        // require extra frame to hide the table scroll bar (bug in imgui)
                        // -> needs extra frame
                    }

                    ImGui::End();
                    unified_window_style.pop();
                }
            }
        }
    }

    ImGui::End();
    unified_window_style.pop();
}

void FdmViewerWrapper::render_customize_extrusion_roles_colors_popup()
{
    bool open = true;
    std::string wnd_name = _u8L("Edit extrusion roles colors");
    Imgui::UnifiedWindowStyle unified_window_style;
    unified_window_style.push();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, { 0.5f, 0.5f });

    ImGuiWindowFlags windows_flag = ImGuiWindowFlags_AlwaysAutoResize
                                  | ImGuiWindowFlags_NoCollapse
                                  | ImGuiWindowFlags_NoResize
                                  | ImGuiWindowFlags_NoScrollbar
                                  | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::OpenPopup(wnd_name.c_str());
    if (ImGui::BeginPopupModal(wnd_name.c_str(), &open, windows_flag)) {
        ImGui::Text("%s:", _u8L("Extrusion Roles Colors").c_str());
        if (ImGui::BeginTable("Roles", 1, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInner)) {
            for (uint8_t i = 0; i < uint8_t(GCODE_EXTRUSION_ROLES_COUNT); ++i) {
                GCodeExtrusionRole role = GCodeExtrusionRole(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const ColorRGB& role_color = m_viewer.extrusion_role_color(role);
                std::array<float, 3> color = { role_color.r(), role_color.g(), role_color.b() };
                if (ImGui::ColorEdit3(to_string(role).c_str(), color.data(), ImGuiColorEditFlags_NoInputs))
                    m_viewer.set_extrusion_role_color(role, { color[0], color[1], color[2] });
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::NewLine();
        float btn_width = 50.0f;
        ImGui::SameLine(ImGui::GetCurrentWindow()->Size.x - ImGui::GetStyle().WindowPadding.x - btn_width);
        if (ImGui::Button(_u8L("Close").c_str(), { btn_width, 0.0f }) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_extrusion_roles_colors_popup_visible = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    unified_window_style.pop();

    if (!open)
        m_extrusion_roles_colors_popup_visible = false;
}

void FdmViewerWrapper::render_customize_options_colors_popup()
{
    bool open = true;
    std::string wnd_name = _u8L("Edit options colors");
    Imgui::UnifiedWindowStyle unified_window_style;
    unified_window_style.push();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, { 0.5f, 0.5f });

    ImGuiWindowFlags windows_flag = ImGuiWindowFlags_AlwaysAutoResize
                                  | ImGuiWindowFlags_NoCollapse
                                  | ImGuiWindowFlags_NoResize
                                  | ImGuiWindowFlags_NoScrollbar
                                  | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::OpenPopup(wnd_name.c_str());
    if (ImGui::BeginPopupModal(wnd_name.c_str(), &open, windows_flag)) {
        ImGui::Text("%s:", _u8L("Options Colors").c_str());
        if (ImGui::BeginTable("Options", 1, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInner)) {
            for (uint8_t i = uint8_t(OptionType::Travels); i <= uint8_t(OptionType::CustomGCodes); ++i) {
                OptionType option = OptionType(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const ColorRGB& option_color = m_viewer.option_color(option);
                std::array<float, 3> color = { option_color.r(), option_color.g(), option_color.b() };
                if (ImGui::ColorEdit3(to_string(option).c_str(), color.data(), ImGuiColorEditFlags_NoInputs))
                    m_viewer.set_option_color(option, { color[0], color[1], color[2] });
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::NewLine();
        float btn_width = 50.0f;
        ImGui::SameLine(ImGui::GetCurrentWindow()->Size.x - ImGui::GetStyle().WindowPadding.x - btn_width);
        if (ImGui::Button(_u8L("Close").c_str(), { btn_width, 0.0f }) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_options_colors_popup_visible = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    unified_window_style.pop();

    if (!open)
        m_options_colors_popup_visible = false;
}

void FdmViewerWrapper::render_customize_range_colors_popup()
{
    if (m_range_colors_popup_type == ViewType::COUNT)
        return;
    
    Palette palette = m_viewer.color_range(m_range_colors_popup_type).palette();

    std::string label;
    switch (m_range_colors_popup_type)
    {
    case ViewType::Height:                   { label = _u8L("Height"); break; }
    case ViewType::Width:                    { label = _u8L("Width"); break; }
    case ViewType::Speed:                    { label = _u8L("Speed"); break; }
    case ViewType::ActualSpeed:              { label = _u8L("Actual speed"); break; }
    case ViewType::FanSpeed:                 { label = _u8L("Fan speed"); break; }
    case ViewType::Temperature:              { label = _u8L("Temperature"); break; }
    case ViewType::VolumetricFlowRate:       { label = _u8L("Volumetric flow rate"); break; }
    case ViewType::ActualVolumetricFlowRate: { label = _u8L("Actual volumetric flow rate"); break; }
    case ViewType::LayerTimeLinear:          { label = _u8L("Layer time linear"); break; }
    case ViewType::LayerTimeLogarithmic:     { label = _u8L("Layer time logarithmic"); break; }
    default:                                 { return; }
    }
    label += " " + _u8L("range colors") + ":";

    bool update_required = false;

    bool open = true;
    std::string wnd_name = _u8L("Edit range colors");

    Imgui::UnifiedWindowStyle unified_window_style;
    unified_window_style.push();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, { 0.5f, 0.5f });

    ImGuiWindowFlags windows_flag = ImGuiWindowFlags_AlwaysAutoResize | 
                                    ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::OpenPopup(wnd_name.c_str());
    if (ImGui::BeginPopupModal(wnd_name.c_str(), &open, windows_flag)) {
        ImGui::Text("%s", label.c_str());
        if (ImGui::BeginTable("Colors", 1, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInner)) {
            for (size_t i = 0; i < palette.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ColorRGB& color_i = palette[i];
                std::array<float, 3> color = { color_i.r(), color_i.g(), color_i.b() };
                std::string label = _u8L("Color") + " #" + std::to_string(i + 1);
                if (ImGui::ColorEdit3(label.c_str(), color.data(), ImGuiColorEditFlags_NoInputs)) {
                    color_i = { color[0], color[1], color[2] };
                    update_required = true;
                }
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText(_u8L("Predefined palettes").c_str());

        float rect_size = ImGui::GetTextLineHeight() + 2.0f * ImGui::GetStyle().FramePadding.y;
        for (size_t i = 0; i < PREDEFINED_PALETTES.size(); ++i) {
            const Palette& pp = PREDEFINED_PALETTES[i];
            std::string label = _u8L("Palette") + " #" + std::to_string(i + 1);
            if (ImGui::Button(label.c_str())) {
                palette = pp;
                update_required = true;
            }
            ImGui::SameLine();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            for (size_t j = 0; j < pp.size(); ++j) {
                ImGui::RenderFrame(pos + ImVec2(1.0f, 1.0f), pos + ImVec2(rect_size, rect_size), Imgui::to_ImU32(pp[j]));
                pos.x += rect_size;
            }
            ImGui::Dummy({ float(pp.size()) * rect_size, rect_size });
        }

        if (update_required)
            m_viewer.set_color_range_palette(m_range_colors_popup_type, palette);

        ImGui::Separator();
        ImGui::NewLine();
        float btn_width = 50.0f;
        ImGui::SameLine(ImGui::GetCurrentWindow()->Size.x - ImGui::GetStyle().WindowPadding.x - btn_width);
        if (ImGui::Button(_u8L("Close").c_str(), { btn_width, 0.0f }) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_range_colors_popup_type = ViewType::COUNT;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    unified_window_style.pop();

    if (!open)
        m_range_colors_popup_type = ViewType::COUNT;
}

void FdmViewerWrapper::render_customize_radius_popup()
{
    if (m_radius_popup_type == MoveType::COUNT)
        return;

    std::string label;
    float radius = 0.0f;
    float min = 0.0f;
    float max = 0.0f;

    switch (m_radius_popup_type)
    {
    case MoveType::Travel:
    {
        label  = _u8L("Travel moves");
        radius = convert(m_viewer.travels_radius(), UnitsType::Millimeters, (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches);
        min    = convert(MIN_TRAVELS_RADIUS_MM, UnitsType::Millimeters, (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches);
        max    = convert(MAX_TRAVELS_RADIUS_MM, UnitsType::Millimeters, (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches);
        break;
    }
    case MoveType::Wipe:
    {
        label  = _u8L("Wipe moves");
        radius = convert(m_viewer.wipes_radius(), UnitsType::Millimeters, (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches);
        min    = convert(MIN_WIPES_RADIUS_MM, UnitsType::Millimeters, (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches);
        max    = convert(MAX_WIPES_RADIUS_MM, UnitsType::Millimeters, (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches);
        break;
    }
    default: { return; }
    }
    label += " " + _u8L("radius") + " (" + format_units((m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches) + "):";

    bool edited = false;
    bool open = true;
    std::string wnd_name = _u8L("Edit moves thickness");

    Imgui::UnifiedWindowStyle unified_window_style;
    unified_window_style.push();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, { 0.5f, 0.5f });

    ImGuiWindowFlags windows_flag = ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::OpenPopup(wnd_name.c_str());
    if (ImGui::BeginPopupModal(wnd_name.c_str(), &open, windows_flag)) {
        ImGui::Text("%s", label.c_str());
        ImGui::AlignTextToFramePadding();
        std::string mask = (m_units == UnitsSystem::Imperial) ? "%.4f" : "%.2f";
        ImGui::Text(mask.c_str(), min);
        ImGui::SameLine();
        if (ImGui::SliderFloat("##Radius", &radius, min, max, mask.c_str(), ImGuiSliderFlags_NoInput))
            edited = true;
        ImGui::SameLine();
        ImGui::Text(mask.c_str(), max);

        ImGui::Separator();
        ImGui::NewLine();
        float btn_width = 50.0f;
        ImGui::SameLine(ImGui::GetCurrentWindow()->Size.x - ImGui::GetStyle().WindowPadding.x - btn_width);
        if (ImGui::Button(_u8L("Close").c_str(), { btn_width, 0.0f }) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_radius_popup_type = MoveType::COUNT;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    unified_window_style.pop();

    if (!open)
        m_radius_popup_type = MoveType::COUNT;

    if (edited) {
        radius = convert(radius, (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches, UnitsType::Millimeters);
        switch (m_radius_popup_type)
        {
        case MoveType::Travel: { m_viewer.set_travels_radius(radius); break; }
        case MoveType::Wipe:   { m_viewer.set_wipes_radius(radius); break; }
        default:               { break; }
        }
    }
}

void FdmViewerWrapper::render_customize_scale_factor_popup()
{
    if (m_scale_factor_popup_type == OptionType::COUNT)
        return;

    std::string label;
    float scale_factor = 0.0f;
    float min = 1.0f;
    float max = 10.0f;
    switch (m_scale_factor_popup_type)
    {
    case OptionType::CenterOfGravity:
    {
        label = _u8L("Center of gravity marker");
        scale_factor = m_viewer.cog_marker_scale_factor();
        break;
    }
    case OptionType::ToolMarker:
    {
        label = _u8L("Tool marker");
        scale_factor = m_viewer.tool_marker_scale_factor();
        break;
    }
    default:
    {
        return;
    }
    }
    label += " " + _u8L("scale factor");

    bool edited = false;
    bool open = true;
    std::string wnd_name = _u8L("Edit scale factor");

    Imgui::UnifiedWindowStyle unified_window_style;
    unified_window_style.push();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, { 0.5f, 0.5f });

    ImGuiWindowFlags windows_flag = ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::OpenPopup(wnd_name.c_str());
    if (ImGui::BeginPopupModal(wnd_name.c_str(), &open, windows_flag)) {
        ImGui::Text("%s:", label.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%.2f", min);
        ImGui::SameLine();
        if (ImGui::SliderFloat("##Radius", &scale_factor, min, max, "%.2f", ImGuiSliderFlags_NoInput))
            edited = true;
        ImGui::SameLine();
        ImGui::Text("%.2f", max);

        ImGui::Separator();
        ImGui::NewLine();
        float btn_width = 50.0f;
        ImGui::SameLine(ImGui::GetCurrentWindow()->Size.x - ImGui::GetStyle().WindowPadding.x - btn_width);
        if (ImGui::Button(_u8L("Close").c_str(), { btn_width, 0.0f }) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_scale_factor_popup_type = OptionType::COUNT;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    unified_window_style.pop();

    if (!open)
        m_scale_factor_popup_type = OptionType::COUNT;

    if (edited) {
        switch (m_scale_factor_popup_type)
        {
        case OptionType::CenterOfGravity: { m_viewer.set_cog_marker_scale_factor(scale_factor); break; }
        case OptionType::ToolMarker:      { m_viewer.set_tool_marker_scale_factor(scale_factor); break; }
        default:                          { return; }
        }
    }
}

} // namespace Slic3r::App::Preview
