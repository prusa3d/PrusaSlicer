
#include "ProcessorImpl.hpp"
#include "Slic3r/Biz/libpgcode/Utils.hpp"
#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Math.hpp"

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <assert.h>
#include <numbers>

#if __has_include(<charconv>)
#include <charconv>
#include <utility>
#endif // __has_include

namespace Slic3r::Biz::libpgcode {
using namespace Domain;
using GCodeReader::GCodeReader;

constexpr auto PI{std::numbers::pi_v<float>};

template<typename T>
constexpr inline T sqr(T x)
{
    return x * x;
}

ProcessorImpl::ProcessorImpl(ProcessorConfig&& config)
: m_options_z_corrector(m_result)
{
    init();
    apply_config(std::move(config));
}

class Progress
{
public:
    Progress(std::function<void(float)> progress_callback, uint64_t buffer_size)
    : m_cb(progress_callback)
    , m_buffer_size(buffer_size)
    {
    }

    void update(uint64_t count) {
        m_processed_count += count;
        float curr = float(m_processed_count) / float(m_buffer_size);
        if (curr > m_last_notify + NOTIFY_THRESHOLD) {
            m_cb(curr);
            m_last_notify = curr;
        }
    }

private:
    std::function<void(float)> m_cb{ nullptr };
    uint64_t m_buffer_size{ 0 };
    uint64_t m_processed_count{ 0 };
    float m_last_notify{ 0.0f };
    const float NOTIFY_THRESHOLD{ 0.01f };
};

void ProcessorImpl::process_buffer(std::string&& buffer, std::function<void(float)> progress_callback)
{
    if (buffer.empty())
        return;

    assert(!m_result.moves().empty());
    Progress progress(progress_callback, uint64_t(buffer.length()));

    m_parser.parse_buffer(buffer, [this, &progress, progress_callback](GCodeReader&, const GCodeReader::GCodeLine& line) {
        process_gcode_line(line);
        if (progress_callback != nullptr)
            progress.update(uint64_t(1 + line.raw().length()));
    });

    LineView& gcode_ref = m_result.gcode();
    if (gcode_ref.empty()) {
        m_result.set_gcode(std::move(buffer));
    } else {
        gcode_ref.push_lines(buffer);
    }
}

ProcessorResult ProcessorImpl::finalize()
{
    MoveVertices& moves = m_result.moves();
    // update width/height of wipe moves
    for (MoveVertex& move : moves) {
        if (move.type == MoveType::Wipe) {
            move.width  = DEFAULT_WIPE_WIDTH;
            move.height = DEFAULT_WIPE_HEIGHT;
        }
    }

    calculate_time();

    {
        // Set moves' mass and add 'phantom' moves.
        // to allow libvgcode to properly detect the start/end of a path we need to add a 'phantom' vertex
        // equal to the current one with the exception of position and actual_feedrate,
        // which should match the previous move position, and mass and times, which are set to zero
        // To do this effectively, first go through the moves vector and remember all elements
        // that should be preceded by its copy. Then resize the vector and put everything in place.
    
        std::vector<uint32_t> mod_moves; // Stores indices into moves - elements that need to be preceded by the copy.
        mod_moves.reserve(moves.size());

        for (size_t i = 1; i < moves.size(); ++i) {
            MoveVertex& curr = moves[i];
            const MoveVertex& prev = moves[i - 1];
            curr.mass = m_result.filament_densities[m_extruder_id] * curr.mm3_per_mm * (curr.position - prev.position).norm();
            OptionType option_type = move_type_to_option(curr.type);
            if (option_type == OptionType::COUNT || option_type == OptionType::Travels || option_type == OptionType::Wipes) {
                if (mod_moves.empty() || prev.type != curr.type || prev.extrusion_role != curr.extrusion_role) {
                    // This move shall be preceded by its (almost) copy.
                    mod_moves.emplace_back(i);
                }
            }
        }
        // Now move all the vertices into place:
        int orig_idx = int(moves.size()) - 1;
        moves.resize(moves.size() + mod_moves.size());
        for (int i = int(moves.size()) - 1; i >= 0; --i) {
            moves[i] = moves[orig_idx];
            if (! mod_moves.empty() && static_cast<int>(mod_moves.back()) == orig_idx) {
                // What we just copied should be preceded by its copy (except for some fields).
                moves[i-1] = moves[i];
                MoveVertex& v = moves[i-1];
                v.actual_feedrate = moves[orig_idx-1].actual_feedrate;
                v.mass            = 0.0f;
                v.position        = moves[orig_idx-1].position;
                v.time            = {};
                --i;
                mod_moves.pop_back();
            }
            --orig_idx;
        }
    }

    // process the time blocks
    for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        CustomGCodeTime& gcode_time = machine.gcode_time;
        if (gcode_time.needed && gcode_time.cache != 0.0f)
            gcode_time.times.push_back({ CustomGCode::Type::ColorChange, gcode_time.cache });
    }

    m_used_filaments.process_caches(m_result, m_extruder_id, m_extrusion_role);
    update_basic_statistics();

    ProcessorResult ret = std::move(m_result);
    return ret;
}

PostProcessorConfig ProcessorImpl::post_processor_config()
{
    PostProcessorConfig ret;
    ret.export_remaining_time_enabled = m_config.export_remaining_time_enabled;
    ret.do_M104_backtrace = m_config.do_M104_backtrace;
    ret.extruder_temps_config = m_config.extruders.temps_config;
    ret.extruder_temps_first_layer_config = m_config.extruders.temps_first_layer_config;

    for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        TimeMachineData& machine_data = ret.time_machines[i];
        machine_data.enabled = machine.enabled;
        machine_data.time = float(machine.time);
        machine_data.first_layer_time = machine.first_layer_time;
        machine_data.line_m73_main_mask = machine.line_m73_main_mask;
        machine_data.line_m73_stop_mask = machine.line_m73_stop_mask;
        machine_data.g1_times_cache = std::move(machine.g1_times_cache);
        machine_data.stop_times = std::move(machine.stop_times);
    }
    return ret;
}

void ProcessorImpl::init()
{
    reset();
    assert(is_decimal_separator_point());
    m_result.set_new_id();
    m_parser.reset();
}

void ProcessorImpl::apply_config(ProcessorConfig&& config)
{
    m_config = std::move(config);

    m_extruder_temps.resize(m_config.extruders.count);
    m_extruder_colors.resize(m_config.extruders.count);
    for (uint8_t i = 0; i < m_config.extruders.count; ++i) {
        m_extruder_colors[i] = uint8_t(i);
    }

    m_result.producer = m_config.producer;
    m_result.z_offset = m_config.z_offset;
    m_result.sequential_print = m_config.sequential_print;
    m_result.spiral_vase_enabled = m_config.spiral_vase_enabled;
    m_result.max_print_height = m_config.max_print_height;
    m_result.extruders_count = m_config.extruders.count;
    m_result.extruder_str_colors = std::move(m_config.extruders.str_colors);
    m_result.color_change_gcode = std::move(m_config.color_change_gcode);
    m_result.pause_print_gcode = std::move(m_config.pause_print_gcode);
    m_result.template_custom_gcode = std::move(m_config.template_custom_gcode);
    m_result.filament_diameters = std::move(m_config.filaments.diameters);
    m_result.filament_densities = std::move(m_config.filaments.densities);
    m_result.filament_costs = std::move(m_config.filaments.costs);
    m_result.bed_shape = std::move(m_config.bed_shape);
    m_result.print_settings = std::move(m_config.print_settings);

    m_time_processor.machine_limits = std::move(m_config.machine_limits);
    m_time_processor.tool_change_time = m_config.filament_change_time;
    m_time_processor.machines[size_t(TimeMode::Stealth)].enabled = m_config.stealth_time_estimator_enabled;

    m_cb_log = m_config.callbacks.cb_log;

    //
    // data validation
    //
    if (m_config.extruders.offsets.size() < size_t(m_result.extruders_count)) {
        if (m_cb_log != nullptr)
            m_cb_log("Set default value for extruder offset. This may result in incorrect gcode visualization.");
        m_config.extruders.offsets.resize(m_result.extruders_count, DEFAULT_EXTRUDER_OFFSET);
    }

    if (m_config.extruders.temps_first_layer_config.size() < size_t(m_result.extruders_count)) {
        if (m_cb_log != nullptr)
            m_cb_log("Set default value for first layer temperature. This may result in incorrect gcode visualization.");
        m_config.extruders.temps_first_layer_config.resize(m_result.extruders_count, 0);
    }

    if (m_config.extruders.temps_config.size() < size_t(m_result.extruders_count)) {
        if (m_cb_log != nullptr)
            m_cb_log("Set default value for temperature. This may result in incorrect gcode visualization.");
        m_config.extruders.temps_config.resize(m_result.extruders_count, 0);
    }

    for (size_t i = 0; i < m_config.extruders.temps_config.size(); ++i) {
        if (m_config.extruders.temps_config[i] == 0)
            // This means the value should be ignored and first layer temp should be used.
            m_config.extruders.temps_config[i] = m_config.extruders.temps_first_layer_config[i];
    }

    if (m_result.filament_densities.size() < size_t(m_result.extruders_count)) {
        if (m_cb_log != nullptr)
            m_cb_log("Set default value for filament density. This may result in incorrect gcode visualization.");
        m_result.filament_densities.resize(m_result.extruders_count, DEFAULT_FILAMENT_DENSITY);
    }

    if (m_result.filament_diameters.size() < size_t(m_result.extruders_count)) {
        if (m_cb_log != nullptr)
            m_cb_log("Set default value for filament diameter. This may result in incorrect gcode visualization.");
        m_result.filament_diameters.resize(m_result.extruders_count, DEFAULT_FILAMENT_DIAMETER);
    }

    if (m_result.filament_costs.size() < size_t(m_result.extruders_count)) {
        if (m_cb_log != nullptr)
            m_cb_log("Set default value for filament cost. This may result in incorrect gcode visualization.");
        m_result.filament_costs.resize(m_result.extruders_count, DEFAULT_FILAMENT_COST);
    }

    if (m_result.extruder_str_colors.size() < size_t(m_result.extruders_count))
        m_result.extruder_str_colors.resize(m_result.extruders_count, std::string());

    for (size_t i = 0; i < m_result.extruder_str_colors.size(); ++i) {
        if (m_result.extruder_str_colors[i].empty())
            m_result.extruder_str_colors[i] = DEFAULT_EXTRUDER_STR_COLOR;
    }

    const std::vector<std::string> missing = m_time_processor.machine_limits.validate();
    if (!missing.empty()) {
        if (m_cb_log != nullptr)
            m_cb_log("Missing values for machine limits. This may result in incorrect gcode visualization.");
    }

    m_time_processor.update_machine_accelerations(m_config.flavor);

    if (m_config.extruders.offsets.empty())
        m_config.extruders.offsets = std::vector<Vec3f>(m_config.extruders.count, Vec3f::Zero());
}

void ProcessorImpl::process_gcode_line(const GCodeReader::GCodeLine& line)
{
    ++m_line_id;

    // update start position
    m_start_position = m_end_position;

    const std::string_view cmd = line.cmd();
    if (cmd.length() > 1) {
        // process command lines
        switch (cmd[0])
        {
        case 'g':
        case 'G':
            switch (cmd.size()) {
            case 2:
                switch (cmd[1]) {
                case '0': { process_G0(line); break; }  // Move
                case '1': { process_G1(line); break; }  // Move
                case '2': { process_G2_G3(line, true); break; }   // CW Arc Move
                case '3': { process_G2_G3(line, false); break; }  // CCW Arc Move
                default: break;
                }
                break;
            case 3:
                switch (cmd[1]) {
                case '1':
                    switch (cmd[2]) {
                    case '0': { process_G10(line); break; } // Retract or Set tool temperature
                    case '1': { process_G11(line); break; } // Unretract
                    default: break;
                    }
                    break;
                case '2':
                    switch (cmd[2]) {
                    case '0': { process_G20(line); break; } // Set Units to Inches
                    case '1': { process_G21(line); break; } // Set Units to Millimeters
                    case '2': { process_G22(line); break; } // Firmware controlled retract
                    case '3': { process_G23(line); break; } // Firmware controlled unretract
                    case '8': { process_G28(line); break; } // Move to origin
                    default: break;
                    }
                    break;
                case '6':
                    switch (cmd[2]) {
                    case '0': { process_G60(line); break; } // Save Current Position
                    case '1': { process_G61(line); break; } // Return to Saved Position
                    default: break;
                    }
                    break;
                case '9':
                    switch (cmd[2]) {
                    case '0': { process_G90(line); break; } // Set to Absolute Positioning
                    case '1': { process_G91(line); break; } // Set to Relative Positioning
                    case '2': { process_G92(line); break; } // Set Position
                    default: break;
                    }
                    break;
                }
                break;
            default:
                break;
            }
            break;
        case 'm':
        case 'M':
            switch (cmd.size()) {
            case 2:
                switch (cmd[1]) {
                case '1': { process_M1(line); break; }   // Sleep or Conditional stop
                default: break;
                }
                break;
            case 3:
                switch (cmd[1]) {
                case '8':
                    switch (cmd[2]) {
                    case '2': { process_M82(line); break; }  // Set extruder to absolute mode
                    case '3': { process_M83(line); break; }  // Set extruder to relative mode
                    default: break;
                    }
                    break;
                default:
                    break;
                }
                break;
            case 4:
                switch (cmd[1]) {
                case '1':
                    switch (cmd[2]) {
                    case '0':
                        switch (cmd[3]) {
                        case '4': { process_M104(line); break; } // Set extruder temperature
                        case '6': { process_M106(line); break; } // Set fan speed
                        case '7': { process_M107(line); break; } // Disable fan
                        case '8': { process_M108(line); break; } // Set tool (Sailfish)
                        case '9': { process_M109(line); break; } // Set extruder temperature and wait
                        default: break;
                        }
                        break;
                    case '3':
                        switch (cmd[3]) {
                        case '2': { process_M132(line); break; } // Recall stored home offsets
                        case '5': { process_M135(line); break; } // Set tool (MakerWare)
                        default: break;
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                case '2':
                    switch (cmd[2]) {
                    case '0':
                        switch (cmd[3]) {
                        case '1': { process_M201(line); break; } // Set max printing acceleration
                        case '3': { process_M203(line); break; } // Set maximum feedrate
                        case '4': { process_M204(line); break; } // Set default acceleration
                        case '5': { process_M205(line); break; } // Advanced settings
                        default: break;
                        }
                        break;
                    case '2':
                        switch (cmd[3]) {
                        case '0': { process_M220(line); break; } // Set Feedrate Percentage
                        case '1': { process_M221(line); break; } // Set extrude factor override percentage
                        default: break;
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                case '4':
                    switch (cmd[2]) {
                    case '0':
                        switch (cmd[3]) {
                        case '1': { process_M401(line); break; } // Repetier: Store x, y and z position
                        case '2': { process_M402(line); break; } // Repetier: Go to stored position
                        default: break;
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                case '5':
                    switch (cmd[2]) {
                    case '6':
                        switch (cmd[3]) {
                        case '6': { process_M566(line); break; } // Set allowable instantaneous speed change
                        default: break;
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                case '7':
                    switch (cmd[2]) {
                    case '0':
                        switch (cmd[3]) {
                        case '2': { process_M702(line); break; } // Unload the current filament into the MK3 MMU2 unit at the end of print.
                        default: break;
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                default:
                    break;
                }
                break;
            default:
                break;
            }
            break;
        case 't':
        case 'T':
            process_T(line); // Select Tool
            break;
        default:
            break;
        }
    } else {
        // Leading whitespace is trimmed so that tags inside indented blocks (e.g. {if}) are recognized.
        const std::string_view comment = skip_whitespaces(std::string_view(line.raw()));
        if (comment.length() > 2 && comment.front() == ';')
            // Process tags embedded into comments. Tag comments always start at the start of a line
            // with a comment and continue with a tag without any whitespace separator.
            process_tags(skip_whitespaces(comment.substr(1)));
    }
}

void ProcessorImpl::process_G1(const GCodeReader::GCodeLine& line)
{
    std::array<std::optional<float>, 4> g1_axes = { std::nullopt, std::nullopt, std::nullopt, std::nullopt };
    if (line.has_x()) g1_axes[X] = line.x();
    if (line.has_y()) g1_axes[Y] = line.y();
    if (line.has_z()) g1_axes[Z] = line.z();
    if (line.has_e()) g1_axes[E] = line.e();
    std::optional<float> g1_feedrate = std::nullopt;
    if (line.has_f()) g1_feedrate = line.f();
    process_G1(g1_axes, g1_feedrate);
}

static MoveType detect_move_type(const Vec4f& delta_pos, bool is_wiping = false)
{
    if (is_wiping)
        return MoveType::Wipe;
    else if (delta_pos[E] < 0.0f)
        return (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f || delta_pos[Z] != 0.0f) ? MoveType::Travel : MoveType::Retract;
    else if (delta_pos[E] > 0.0f) {
        if (delta_pos[X] == 0.0f && delta_pos[Y] == 0.0f)
            return (delta_pos[Z] == 0.0f) ? MoveType::Unretract : MoveType::Travel;
        else if (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f)
            return MoveType::Extrude;
    }
    else if (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f || delta_pos[Z] != 0.0f)
        return MoveType::Travel;

    return MoveType::Noop;
}

static bool is_relative_move(Axis axis, PositioningType global_positioning_type, PositioningType e_local_positioning_type)
{
    bool ret = (global_positioning_type == PositioningType::Relative);
    if (axis == E)
        ret |= (e_local_positioning_type == PositioningType::Relative);
    return ret;
}

static float extract_absolute_position_on_axis(Axis axis, std::optional<float> value, float area_filament_cross_section,
    PositioningType global_positioning_type, PositioningType e_local_positioning_type, UnitsType units, bool use_volumetric_e,
    const Vec4f& start_position, const Vec4f& origin)
{
    if (value.has_value()) {
        bool is_relative = is_relative_move(axis, global_positioning_type, e_local_positioning_type);
        float ret = convert(*value, units, UnitsType::Millimeters);
        if (axis == E && use_volumetric_e)
            ret /= area_filament_cross_section;
        return is_relative ? start_position[axis] + ret : origin[axis] + ret;
    }
    else
        return start_position[axis];
}

float calc_junction_acceleration(const TimeBlock &block, const Vec4f &junction_vector, const TimeProcessor &time_processor, TimeMode mode)
{
    float junction_acceleration = block.acceleration;
    for (unsigned char axis_idx = X; axis_idx <= E; ++axis_idx) {
        if (junction_vector[axis_idx] == 0.f) {
            continue;
        }

        const float axis_max_acceleration = time_processor.axis_max_acceleration(mode, Axis(axis_idx));
        junction_acceleration = std::min(junction_acceleration, std::abs(axis_max_acceleration / junction_vector[axis_idx]));
    }

    return junction_acceleration;
}

static float move_length(const Vec4f& delta_pos)
{
    float sq_xyz_length = sqr(delta_pos[X]) + sqr(delta_pos[Y]) + sqr(delta_pos[Z]);
    return (sq_xyz_length > 0.0f) ? std::sqrt(sq_xyz_length) : std::abs(delta_pos[E]);
}

static bool is_extrusion_only_move(const Vec4f& delta_pos)
{
    return delta_pos[X] == 0.0f && delta_pos[Y] == 0.0f && delta_pos[Z] == 0.0f && delta_pos[E] != 0.0f;
}

static Vec4f normalized_junction_vector(const Vec4f& junction_vector)
{
    float magnitude_sq = 0.f;
    for (unsigned char axis_idx = X; axis_idx <= E; ++axis_idx) {
        magnitude_sq += Slic3r::sqr(junction_vector[axis_idx]);
    }

    return junction_vector / std::sqrt(magnitude_sq);
}

void ProcessorImpl::process_G1(const std::array<std::optional<float>, 4>& axes, const std::optional<float>& feedrate,
    G1DiscretizationOrigin origin, const std::optional<unsigned int>& remaining_internal_g1_lines)
{
    FilamentGeometry filament_geo = m_result.filament_geometry(m_extruder_id);
    float filament_radius = 0.5f * filament_geo.diameter;

    ++m_g1_line_id;

    // enable processing of lines M201/M203/M204/M205
    m_time_processor.machine_envelope_processing_enabled = true;

    // updates axes positions from line
    for (uint8_t a = X; a <= E; ++a) {
        m_end_position[a] = extract_absolute_position_on_axis(Axis(a), axes[a], filament_geo.area_cross_section,
            m_global_positioning_type, m_e_local_positioning_type, m_units, m_config.use_volumetric_e,
            m_start_position, m_origin);
    }

    // updates feedrate from line, if present
    if (feedrate.has_value())
        m_feedrate = m_feed_multiply.current * convert(*feedrate, UnitsType::MillimetersPerMinute, UnitsType::MillimetersPerSecond);

    // calculates movement deltas
    Vec4f delta_pos = m_end_position - m_start_position;
    if (std::all_of(delta_pos.begin(), delta_pos.end(), [](float a) { return a == 0.0f; }))
        return;

    float volume_extruded_filament = filament_geo.area_cross_section * delta_pos[E];

    if (volume_extruded_filament != 0.f && !m_exclude_e) {
        if (m_flushing) {
            m_used_filaments.update_flush_per_extruder(volume_extruded_filament, m_extruder_id);
        } else {
            m_used_filaments.increase_caches(
                volume_extruded_filament,
                m_extruder_id,
                filament_geo.area_cross_section * m_config.parking_pos_retraction,
                filament_geo.area_cross_section * m_config.extra_loading_move
            );
        }
    }

    MoveType type = detect_move_type(delta_pos, m_wiping);
    if (type == MoveType::Extrude) {
        float delta_xyz = std::sqrt(sqr(delta_pos[X]) + sqr(delta_pos[Y]) + sqr(delta_pos[Z]));
        float area_toolpath_cross_section = volume_extruded_filament / delta_xyz;

        // volume extruded filament / tool displacement = area toolpath cross section
        m_mm3_per_mm = area_toolpath_cross_section;

        if (m_height_from_tag > 0.0f)
            // use height coming from the gcode tags
            m_height = m_height_from_tag;
        else if (m_layer_id == 0) { // first layer
            if (m_end_position[Z] > 0.0f)
                // use the current (clamped) z, if greater than zero  
                m_height = std::min(m_end_position[Z], 2.0f);
            else
                // use the first layer height  
                m_height = m_config.first_layer_height + m_config.z_offset;
        }
        else if (origin == G1DiscretizationOrigin::G1) {
            if (m_end_position[Z] > m_extruded_last_z + float(EPSILON) && delta_pos[Z] == 0.0f)
                m_height = m_end_position[Z] - m_extruded_last_z;
        }

        if (m_height == 0.0f)
            m_height = DEFAULT_TOOLPATH_HEIGHT;

        if (origin == G1DiscretizationOrigin::G1)
            m_extruded_last_z = m_end_position[Z];
        m_options_z_corrector.update(m_height);

        if (m_width_from_tag > 0.0f)
            // use width coming from the gcode tags
            m_width = m_width_from_tag;
        else if (m_extrusion_role == GCodeExtrusionRole::ExternalPerimeter)
            // cross section: rectangle
            m_width = delta_pos[E] * PI * sqr(1.05f * filament_radius) / (delta_xyz * m_height);
        else if (m_extrusion_role == GCodeExtrusionRole::BridgeInfill || m_extrusion_role == GCodeExtrusionRole::None)
            // cross section: circle
            m_width = m_result.filament_diameters[m_extruder_id] * std::sqrt(delta_pos[E] / delta_xyz);
        else
            // cross section: rectangle + 2 semicircles
            m_width = delta_pos[E] * PI * sqr(filament_radius) / (delta_xyz * m_height) + (1.0f - 0.25f * PI) * m_height;

        if (m_width == 0.0f)
            m_width = DEFAULT_TOOLPATH_WIDTH;

        // clamp width to avoid artifacts which may arise from wrong values of m_height
        m_width = std::min(m_width, std::max(2.0f, 4.0f * m_height));
    }

    float distance = move_length(delta_pos);
    assert(distance != 0.0f);
    float inv_distance = (distance != 0.0f) ? 1.0f / distance : 0.0f;

    for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        if (!machine.enabled)
            continue;

        TimeMachineState& curr = machine.curr;
        TimeMachineState& prev = machine.prev;
        TimeBlocks& blocks = machine.blocks;

        curr.feedrate = (delta_pos[E] == 0.0f) ? m_time_processor.minimum_travel_feedrate(TimeMode(i), m_feedrate) :
            m_time_processor.minimum_feedrate(TimeMode(i), m_feedrate);

        TimeBlock block;
        block.move_type = type;
        block.role = m_extrusion_role;
        block.distance = distance;
        block.g1_line_id = m_g1_line_id;
        block.move_id = uint32_t(m_result.const_moves()->size());
        block.remaining_internal_g1_lines = remaining_internal_g1_lines.has_value() ? *remaining_internal_g1_lines : 0;
        block.layer_id = std::max<uint32_t>(1, m_layer_id);

        // calculates block cruise feedrate
        float min_feedrate_factor = 1.0f;
        for (uint8_t a = X; a <= E; ++a) {
            curr.axis_feedrate[a] = curr.feedrate * delta_pos[a] * inv_distance;
            if (a == E)
                curr.axis_feedrate[a] *= machine.extrude_factor_override_percentage;

            curr.abs_axis_feedrate[a] = std::abs(curr.axis_feedrate[a]);
            if (curr.abs_axis_feedrate[a] != 0.0f) {
                float axis_max_feedrate = m_time_processor.axis_max_feedrate(TimeMode(i), Axis(a));
                if (axis_max_feedrate != 0.0f)
                    min_feedrate_factor = std::min<float>(min_feedrate_factor, axis_max_feedrate / curr.abs_axis_feedrate[a]);
            }
        }

        block.feedrate_profile.cruise = min_feedrate_factor * curr.feedrate;

        if (min_feedrate_factor < 1.0f) {
            for (uint8_t a = X; a <= E; ++a) {
                curr.axis_feedrate[a] *= min_feedrate_factor;
                curr.abs_axis_feedrate[a] *= min_feedrate_factor;
            }
        }

        // calculates block acceleration
        float acceleration = (type == MoveType::Travel) ? m_time_processor.travel_acceleration(TimeMode(i)) :
            (is_extrusion_only_move(delta_pos) ? m_time_processor.retract_acceleration(TimeMode(i)) :
              m_time_processor.acceleration(TimeMode(i)));

        for (uint8_t a = X; a <= E; ++a) {
            float axis_max_acceleration = m_time_processor.axis_max_acceleration(TimeMode(i), Axis(a));
            float scale = std::abs(delta_pos[a]) * inv_distance;
            if (acceleration * scale > axis_max_acceleration)
                acceleration = axis_max_acceleration / scale;
        }

        block.acceleration = acceleration;

        static const float PREVIOUS_FEEDRATE_THRESHOLD = 0.0001f;

        float vmax_junction = 0.f; // Init entry speed to zero. Assume it starts from rest. Planner will correct this later.
        if (const float junction_deviation = m_time_processor.junction_deviation(TimeMode(i)); junction_deviation > 0.f) {
            // Junction deviation.

            curr.jd_unit_vec = Vec4f(
                static_cast<float>(delta_pos[X]) / block.distance,
                static_cast<float>(delta_pos[Y]) / block.distance,
                static_cast<float>(delta_pos[Z]) / block.distance,
                static_cast<float>(delta_pos[E]) / block.distance
            );

            // Skip first block or when previous_nominal_speed is used as a flag for homing and offset cycles.
            if (!blocks.empty() && prev.feedrate > PREVIOUS_FEEDRATE_THRESHOLD) {
                // Compute cosine of angle between previous and current path. (prev.jd_unit_vec is negative)
                // NOTE: Max junction velocity is computed without sin() or acos() by trig half angle identity.
                float junction_cos_theta = (-prev.jd_unit_vec).dot(curr.jd_unit_vec);

                // NOTE: Computed without any expensive trig, sin() or acos(), by trig half angle identity of cos(theta).
                // For a 0 degree acute junction, just set (already set before) minimum junction speed.
                if (junction_cos_theta <= 0.999999f) {
                    junction_cos_theta = std::max(junction_cos_theta, -0.999999f); // Check for numerical round-off to avoid divide by zero.

                    // Convert delta vector to unit vector
                    const Vec4f junction_unit_vec = normalized_junction_vector(curr.jd_unit_vec - prev.jd_unit_vec);

                    const float junction_acceleration = calc_junction_acceleration(block, junction_unit_vec, m_time_processor, TimeMode(i));
                    const float sin_theta_d2 = std::sqrt(0.5f * (1.f - junction_cos_theta)); // Trig half angle identity. Always positive.

                    float vmax_junction_sqr = (junction_acceleration * junction_deviation * sin_theta_d2) / (1.f - sin_theta_d2);

                    // For small moves with >135° junction (octagon) find speed for approximate arc
                    if (block.distance < 1 && junction_cos_theta < -0.7071067812f) {
                        // Fast acos(-t) approximation (max. error +-0.033rad = 1.89°)
                        // Based on MinMax polynomial published by W. Randolph Franklin, see
                        // https://wrf.ecse.rpi.edu/Research/Short_Notes/arcsin/onlyelem.html
                        //  acos( t) = pi / 2 - asin(x)
                        //  acos(-t) = pi - acos(t) ... pi / 2 + asin(x)

                        const float neg = junction_cos_theta < 0.f ? -1.f : 1.f;
                        const float t = neg * junction_cos_theta;
                        const float asinx = 0.032843707f + t * (-1.451838349f + t * (29.66153956f + t * (-131.1123477f + t * (262.8130562f + t * (-242.7199627f + t * (84.31466202f))))));
                        const float junction_theta = Slic3r::deg2rad(90.f) + neg * asinx; // acos(-t)

                        // NOTE: junction_theta bottoms out at 0.033 which avoids divide by 0.
                        const float limit_sqr = (block.distance * junction_acceleration) / junction_theta;
                        vmax_junction_sqr = std::min(vmax_junction_sqr, limit_sqr);
                    }

                    // Get the lowest speed
                    vmax_junction_sqr = std::min(vmax_junction_sqr, std::min(Slic3r::sqr(block.feedrate_profile.cruise), Slic3r::sqr(prev.feedrate)));
                    vmax_junction = std::sqrt(vmax_junction_sqr);
                }
            }
        } else {
            // Classic jerk.

            // calculates block exit feedrate
            curr.safe_feedrate = block.feedrate_profile.cruise;

            for (uint8_t a = X; a <= E; ++a) {
                const float axis_max_jerk = m_time_processor.axis_max_jerk(TimeMode(i), Axis(a));
                if (curr.abs_axis_feedrate[a] > axis_max_jerk)
                    curr.safe_feedrate = std::min(curr.safe_feedrate, axis_max_jerk);
            }

            block.feedrate_profile.exit = curr.safe_feedrate;

            // calculates block entry feedrate
            vmax_junction = curr.safe_feedrate;
            if (!blocks.empty() && prev.feedrate > PREVIOUS_FEEDRATE_THRESHOLD) {
                const bool prev_speed_larger = prev.feedrate > block.feedrate_profile.cruise;
                const float smaller_speed_factor = prev_speed_larger ? (block.feedrate_profile.cruise / prev.feedrate) : (prev.feedrate / block.feedrate_profile.cruise);
                // Pick the smaller of the nominal speeds. Higher speed shall not be achieved at the junction during coasting.
                vmax_junction = prev_speed_larger ? block.feedrate_profile.cruise : prev.feedrate;

                float v_factor = 1.0f;
                bool limited = false;

                for (uint8_t a = X; a <= E; ++a) {
                    // Limit an axis. We have to differentiate coasting from the reversal of an axis movement, or a full stop.
                    float v_exit = prev.axis_feedrate[a];
                    float v_entry = curr.axis_feedrate[a];

                    if (prev_speed_larger)
                        v_exit *= smaller_speed_factor;

                    if (limited) {
                        v_exit *= v_factor;
                        v_entry *= v_factor;
                    }

                    // Calculate the jerk depending on whether the axis is coasting in the same direction or reversing a direction.
                    const float jerk =
                      (v_exit > v_entry) ?
                      ((v_entry > 0.0f || v_exit < 0.0f) ?
                        // coasting
                        (v_exit - v_entry) :
                        // axis reversal
                        std::max(v_exit, -v_entry)) :
                      // v_exit <= v_entry
                      ((v_entry < 0.0f || v_exit > 0.0f) ?
                        // coasting
                        (v_entry - v_exit) :
                        // axis reversal
                        std::max(-v_exit, v_entry));

                    const float axis_max_jerk = m_time_processor.axis_max_jerk(TimeMode(i), Axis(a));
                    if (jerk > axis_max_jerk) {
                        v_factor *= axis_max_jerk / jerk;
                        limited = true;
                    }
                }

                if (limited)
                    vmax_junction *= v_factor;

                // Now the transition velocity is known, which maximizes the shared exit / entry velocity while
                // respecting the jerk factors, it may be possible, that applying separate safe exit / entry velocities will achieve faster prints.
                const float vmax_junction_threshold = vmax_junction * 0.99f;

                // Not coasting. The machine will stop and start the movements anyway, better to start the segment from start.
                if (prev.safe_feedrate > vmax_junction_threshold && curr.safe_feedrate > vmax_junction_threshold)
                    vmax_junction = curr.safe_feedrate;
            }
        }

        const float v_allowable = max_allowable_speed(-acceleration, curr.safe_feedrate, block.distance);
        block.feedrate_profile.entry = std::min(vmax_junction, v_allowable);

        block.max_entry_speed = vmax_junction;
        block.flags.nominal_length = (block.feedrate_profile.cruise <= v_allowable);
        block.flags.recalculate = true;
        block.safe_feedrate = curr.safe_feedrate;

        // calculates block trapezoid
        block.calculate_trapezoid();

        // updates previous
        prev = curr;

        blocks.push_back(block);
    }

    if (m_time_processor.machines[0].blocks.size() > TimeProcessorPlanner::refresh_threshold)
        calculate_time(TimeProcessorPlanner::queue_size);

    if (m_seams_detection_enabled && (
        type != MoveType::Extrude ||
        (
            m_extrusion_role != GCodeExtrusionRole::ExternalPerimeter &&
            m_extrusion_role != GCodeExtrusionRole::OverhangPerimeter
        )
    )) {
        auto get_position_xyz = [](const Vec4f& pos) {
            Vec3f ret = { pos[X], pos[Y], pos[Z] };
            return ret;
        };
        auto set_end_position_xyz = [this](const Vec3f& pos) {
            m_end_position[X] = pos[X]; m_end_position[Y] = pos[Y]; m_end_position[Z] = pos[Z];
        };

        Vec3f curr_pos = get_position_xyz(m_end_position);
        Vec3f new_pos = m_result.const_moves()->back().position - m_config.extruders.offsets[m_extruder_id];
        set_end_position_xyz(new_pos + m_config.z_offset * Vec3f::UnitZ());
        store_move(MoveType::Seam);
        set_end_position_xyz(curr_pos);
        m_seams_detection_enabled = false;
    }
    else if (type == MoveType::Extrude && m_extrusion_role == GCodeExtrusionRole::ExternalPerimeter)
        m_seams_detection_enabled = true;

    // store move
    store_move(type, origin == G1DiscretizationOrigin::G2G3);
}

static Vec4f adjust_target(const Vec4f& target, const Vec4f& prev_position, float area_filament_cross_section,
    PositioningType global_positioning_type, PositioningType e_local_positioning_type, bool use_volumetric_e, UnitsType units)
{
    Vec4f ret = target;
    if (global_positioning_type == PositioningType::Relative) {
        for (uint8_t a = X; a < E; ++a) {
            ret[a] -= prev_position[a];
        }
    }
    else if (e_local_positioning_type == PositioningType::Relative)
        ret[E] -= prev_position[E];

    if (use_volumetric_e)
        ret[E] *= area_filament_cross_section;

    for (uint8_t a = X; a <= E; ++a) {
        ret[a] = convert(ret[a], units, UnitsType::Millimeters);
    }
    return ret;
}

//
// see: 
// \src\libslic3r\Geometry\ArcWelder.hpp
// Calculate center point (center of a circle) of an arc given two points and a radius.
// positive radius: take shorter arc
// negative radius: take longer arc
// radius must NOT be zero!
//
static Vec2f arc_center(const Vec2f& start_pos, const Vec2f& end_pos, float radius, bool is_ccw)
{
    assert(radius != 0.0f);
    Vec2f v = end_pos - start_pos;
    float q2 = v.squaredNorm();
    assert(q2 > 0.0f);
    float t2 = sqr(radius) / q2 - 0.25f;
    // If the start_pos and end_pos are nearly antipodal, t2 may become slightly negative.
    // In that case return a centroid of start_point & end_point.
    float t = t2 > 0.0f ? std::sqrt(t2) : 0.0f;
    Vec2f mid = 0.5f * (start_pos + end_pos);
    Vec2f vp{ -v[Y] * t, v[X] * t };
    return (radius > 0.0f) == is_ccw ? (Vec2f)(mid + vp) : (Vec2f)(mid - vp);
}

//
// see: 
// \src\libslic3r\Geometry\ArcWelder.hpp
// Return number of linear segments necessary to interpolate arc of a given positive radius and positive angle to satisfy
// maximum deviation of an interpolating polyline from an analytic arc.
//
uint32_t arc_discretization_steps(float radius, float angle, float deviation)
{
    assert(radius > 0.0f);
    assert(angle > 0.0f);
    assert(angle <= 2.0f * float(PI));
    assert(deviation > 0.0f);

    float d = radius - deviation;
    return d < float(EPSILON) ?
        // Radius smaller than deviation.
        (   // Acute angle: a single segment interpolates the arc with sufficient accuracy.
          angle < PI ||
          // Obtuse angle: Test whether the furthest point (center) of an arc is closer than deviation to the center of a line segment.
          radius * (1.0f + cos(PI - 0.5f * angle)) < deviation ?
          // Single segment is sufficient
          1 :
          // Two segments are necessary, the middle point is at the center of the arc.
          2) :
        uint32_t(ceil(angle / (2.0f * acos(d / radius))));
}

void ProcessorImpl::process_G2_G3(const GCodeReader::GCodeLine& line, bool clockwise)
{
    enum class Fitting { None, IJ, R };
    std::string_view axis_pos_I;
    std::string_view axis_pos_J;
    Fitting fitting = Fitting::None;
    if (line.has('R'))
        fitting = Fitting::R;
    else {
        axis_pos_I = line.axis_pos('I');
        axis_pos_J = line.axis_pos('J');
        if (!axis_pos_I.empty() || !axis_pos_J.empty())
            fitting = Fitting::IJ;
    }

    if (fitting == Fitting::None)
        return;

    FilamentGeometry filament_geo = m_result.filament_geometry(m_extruder_id);

    Vec4f end_position = m_start_position;
    for (uint8_t a = X; a <= E; ++a) {
        std::optional<float> value;
        if (line.has(Axis(a)))
            value = line.value(Axis(a));
        end_position[a] = extract_absolute_position_on_axis(Axis(a), value, filament_geo.area_cross_section,
            m_global_positioning_type, m_e_local_positioning_type, m_units, m_config.use_volumetric_e,
            m_start_position, m_origin);
    }

    // relative center
    Vec3f rel_center = Vec3f::Zero();
#ifndef NDEBUG
    double radius = 0.0;
#endif // NDEBUG
    if (fitting == Fitting::R) {
        float r;
        if (!line.has_value('R', r) || r == 0.0f)
            return;
#ifndef NDEBUG
        radius = double(std::abs(r));
#endif // NDEBUG
        Vec2f start_pos = { m_start_position[X], m_start_position[Y] };
        Vec2f end_pos = { end_position[X], end_position[Y] };
        Vec2f c = arc_center(start_pos, end_pos, r, !clockwise);
        rel_center[X] = c[X] - m_start_position[X];
        rel_center[Y] = c[Y] - m_start_position[Y];
    }
    else {
        assert(fitting == Fitting::IJ);
        if (!axis_pos_I.empty() && !line.has_value(axis_pos_I, rel_center[X]))
            return;
        if (!axis_pos_J.empty() && !line.has_value(axis_pos_J, rel_center[Y]))
            return;
    }

    // scale center, if needed
    rel_center[X] = convert(rel_center[X], m_units, UnitsType::Millimeters);
    rel_center[Y] = convert(rel_center[Y], m_units, UnitsType::Millimeters);
    rel_center[Z] = convert(rel_center[Z], m_units, UnitsType::Millimeters);

    struct Arc
    {
        Vec3f start{ Vec3f::Zero()};
        Vec3f end{ Vec3f::Zero() };
        Vec3f center{ Vec3f::Zero() };

        float angle{ 0.0f };
        float delta_x() const { return end[X] - start[X]; }
        float delta_y() const { return end[Y] - start[Y]; }
        float delta_z() const { return end[Z] - start[Z]; }

        float arc_length() const { return angle * start_radius(); }
        float travel_length() const { return std::sqrt(sqr(arc_length()) + sqr(delta_z())); }
        float start_radius() const { return (start - center).norm(); }
        float end_radius() const { return (end - center).norm(); }

        Vec3f relative_start() const { return start - center; }
        Vec3f relative_end() const { return end - center; }

        bool is_full_circle() const { return std::abs(delta_x()) < float(EPSILON) && std::abs(delta_y()) < float(EPSILON); }
    };

    Arc arc;

    // arc start endpoint
    arc.start = Vec3f{ m_start_position[X], m_start_position[Y], m_start_position[Z] };

    // arc center
    arc.center = arc.start + rel_center;

    // arc end endpoint
    arc.end = Vec3f{ end_position[X], end_position[Y], end_position[Z] };

    // radii
    if (std::abs(arc.end_radius() - arc.start_radius()) > 0.001f) {
        // what to do ???
    }

    assert(fitting != Fitting::R || std::abs(radius - arc.start_radius()) < float(EPSILON));

    // updates feedrate from line
    std::optional<float> feedrate;
    if (line.has_f())
        feedrate = convert(m_feed_multiply.current * line.f(), UnitsType::MillimetersPerMinute, UnitsType::MillimetersPerSecond);

    // updates extrusion from line
    std::optional<float> extrusion;
    if (line.has_e())
        extrusion = end_position[E] - m_start_position[E];

    // relative arc endpoints
    Vec3f rel_arc_start = arc.relative_start();
    Vec3f rel_arc_end   = arc.relative_end();

    // arc angle
    if (arc.is_full_circle())
        arc.angle = 2.0f * PI;
    else {
        arc.angle = std::atan2(rel_arc_start[X] * rel_arc_end[Y] - rel_arc_start[Y] * rel_arc_end[X],
            rel_arc_start[X] * rel_arc_end[X] + rel_arc_start[Y] * rel_arc_end[Y]);
        if (arc.angle < 0.0f)
            arc.angle += 2.0f * PI;
        if (clockwise)
            arc.angle -= 2.0f * PI;
    }

    float travel_length = arc.travel_length();
    if (travel_length < 0.001f)
        return;

    auto internal_only_g1_line = [this](const Vec4f& target, bool has_z, const std::optional<float>& feedrate,
        const std::optional<float>& extrusion, const std::optional<uint32_t>& remaining_internal_g1_lines = std::nullopt) {
          std::array<std::optional<float>, 4> g1_axes = { target[X], target[Y], std::nullopt, std::nullopt };
          std::optional<float> g1_feedrate = std::nullopt;
          if (has_z)
              g1_axes[Z] = target[Z];
          if (extrusion.has_value())
              g1_axes[E] = target[E];
          if (feedrate.has_value())
              g1_feedrate = *feedrate;
          process_G1(g1_axes, g1_feedrate, G1DiscretizationOrigin::G2G3, remaining_internal_g1_lines);
    };

    if (m_config.flavor == GCodeFlavor::gcfMarlinFirmware
        || m_config.flavor == GCodeFlavor::gcfPrusaFirmwareBuddy)
    {
        // calculate arc segments
        // reference:
        // Prusa-Firmware-Buddy\lib\Marlin\Marlin\src\gcode\motion\G2_G3.cpp - plan_arc()
        // https://github.com/prusa3d/Prusa-Firmware-Buddy-Private/blob/private/lib/Marlin/Marlin/src/gcode/motion/G2_G3.cpp

        static const float MAX_ARC_DEVIATION = 0.02f;
        static const float MIN_ARC_SEGMENTS_PER_SEC = 50;
        static const float MIN_ARC_SEGMENT_MM = 0.1f;
        static const float MAX_ARC_SEGMENT_MM = 2.0f;
        float feedrate_mm_s = feedrate.has_value() ? *feedrate : m_feedrate;
        float radius_mm = rel_center.norm();
        float segment_mm = std::clamp(std::min(std::sqrt(8.0f * radius_mm * MAX_ARC_DEVIATION), feedrate_mm_s * (1.0f / MIN_ARC_SEGMENTS_PER_SEC)), MIN_ARC_SEGMENT_MM, MAX_ARC_SEGMENT_MM);
        float flat_mm = radius_mm * std::abs(arc.angle);
        uint32_t segments = std::max(uint32_t(flat_mm / segment_mm + 0.8f), uint32_t(1));

        Vec4f prev_target = m_start_position;

        if (segments > 1) {
            float inv_segments = 1.0f / float(segments);
            float theta_per_segment = float(arc.angle) * inv_segments;
            float cos_T = cos(theta_per_segment);
            float sin_T = sin(theta_per_segment);
            float z_per_segment = arc.delta_z() * inv_segments;
            float extruder_per_segment = (extrusion.has_value()) ? *extrusion * inv_segments : 0.0f;

            static const size_t N_ARC_CORRECTION = 25;
            size_t arc_recalc_count = N_ARC_CORRECTION;

            Vec2f rvec = { -rel_center[X], -rel_center[Y]};
            Vec4f arc_target = { 0.0f, 0.0f, m_start_position[Z], m_start_position[E] };
            for (uint32_t i = 1; i < segments; ++i) {
                if (--arc_recalc_count) {
                    // Apply vector rotation matrix to previous rvec.a / 1
                    float r_new_Y = rvec[X] * sin_T + rvec[Y] * cos_T;
                    rvec[X] = rvec[X] * cos_T - rvec[Y] * sin_T;
                    rvec[Y] = r_new_Y;
                }
                else {
                    arc_recalc_count = N_ARC_CORRECTION;
                    // Arc correction to radius vector. Computed only every N_ARC_CORRECTION increments.
                    // Compute exact location by applying transformation matrix from initial radius vector(=-offset).
                    // To reduce stuttering, the sin and cos could be computed at different times.
                    // For now, compute both at the same time.
                    const float Ti = i * theta_per_segment;
                    const float cos_Ti = cos(Ti);
                    const float sin_Ti = sin(Ti);
                    rvec[X] = -rel_center[X] * cos_Ti + rel_center[Y] * sin_Ti;
                    rvec[Y] = -rel_center[X] * sin_Ti - rel_center[Y] * cos_Ti;
                }

                // Update arc_target location
                arc_target[X] = arc.center[X] + rvec[X];
                arc_target[Y] = arc.center[Y] + rvec[Y];
                arc_target[Z] += z_per_segment;
                arc_target[E] += extruder_per_segment;

                m_start_position = m_end_position; // this is required because we are skipping the call to process_gcode_line()
                Vec4f adj_target = adjust_target(arc_target, prev_target, filament_geo.area_cross_section,
                    m_global_positioning_type, m_e_local_positioning_type, m_config.use_volumetric_e, m_units);
                internal_only_g1_line(adj_target, z_per_segment != 0.0, (i == 1) ? feedrate : std::nullopt, extrusion, segments - i);
                prev_target = arc_target;
            }
        }

        // Ensure last segment arrives at target location.
        m_start_position = m_end_position; // this is required because we are skipping the call to process_gcode_line()
        Vec4f adj_target = adjust_target(end_position, prev_target, filament_geo.area_cross_section,
            m_global_positioning_type, m_e_local_positioning_type, m_config.use_volumetric_e, m_units);
        internal_only_g1_line(adj_target, arc.delta_z() != 0.0, (segments == 1) ? feedrate : std::nullopt, extrusion);
    }
    else {
        // calculate arc segments
        // reference:
        // Prusa-Firmware\Firmware\motion_control.cpp - mc_arc()
        // https://github.com/prusa3d/Prusa-Firmware/blob/MK3/Firmware/motion_control.cpp

        // segments count
#if 0
        static const float MM_PER_ARC_SEGMENT = 1.0f;
        uint32_t segments = std::max(uint32_t(std::floor(travel_length / MM_PER_ARC_SEGMENT)), uint32_t(1));
#else
        static const float gcode_arc_tolerance = 0.0125f;
        const uint32_t segments = arc_discretization_steps(arc.start_radius(), std::abs(arc.angle), gcode_arc_tolerance);
#endif

        float inv_segment = 1.0f / float(segments);
        float theta_per_segment = arc.angle * inv_segment;
        float z_per_segment = arc.delta_z() * inv_segment;
        float extruder_per_segment = (extrusion.has_value()) ? *extrusion * inv_segment : 0.0f;
        float sq_theta_per_segment = sqr(theta_per_segment);
        float cos_T = 1.0f - 0.5f * sq_theta_per_segment;
        float sin_T = theta_per_segment - sq_theta_per_segment * theta_per_segment / 6.0f;

        Vec4f prev_target = m_start_position;
        Vec4f arc_target;

        // Initialize the linear axis
        arc_target[Z] = m_start_position[Z];

        // Initialize the extruder axis
        arc_target[E] = m_start_position[E];

        static const size_t N_ARC_CORRECTION = 25;
        Vec3f curr_rel_arc_start = arc.relative_start();
        size_t count = N_ARC_CORRECTION;

        for (uint32_t i = 1; i < segments; ++i) {
            if (count-- == 0) {
                float cos_Ti = cos(i * theta_per_segment);
                float sin_Ti = sin(i * theta_per_segment);
                curr_rel_arc_start[X] = -rel_center[X] * cos_Ti + rel_center[Y] * sin_Ti;
                curr_rel_arc_start[Y] = -rel_center[X] * sin_Ti - rel_center[Y] * cos_Ti;
                count = N_ARC_CORRECTION;
            }
            else {
                float r_axisi = curr_rel_arc_start[X] * sin_T + curr_rel_arc_start[Y] * cos_T;
                curr_rel_arc_start[X] = curr_rel_arc_start[X] * cos_T - curr_rel_arc_start[Y] * sin_T;
                curr_rel_arc_start[Y] = r_axisi;
            }

            // Update arc_target location
            arc_target[X] = arc.center[X] + curr_rel_arc_start[X];
            arc_target[Y] = arc.center[Y] + curr_rel_arc_start[Y];
            arc_target[Z] += z_per_segment;
            arc_target[E] += extruder_per_segment;

            m_start_position = m_end_position; // this is required because we are skipping the call to process_gcode_line()
            Vec4f adj_target = adjust_target(arc_target, prev_target, filament_geo.area_cross_section,
                m_global_positioning_type, m_e_local_positioning_type, m_config.use_volumetric_e, m_units);
            internal_only_g1_line(adj_target, z_per_segment != 0.0f, (i == 1) ? feedrate : std::nullopt, extrusion,segments - i);
            prev_target = arc_target;
        }

        // Ensure last segment arrives at target location.
        m_start_position = m_end_position; // this is required because we are skipping the call to process_gcode_line()
        Vec4f adj_target = adjust_target(end_position, prev_target, filament_geo.area_cross_section,
            m_global_positioning_type, m_e_local_positioning_type, m_config.use_volumetric_e, m_units);
        internal_only_g1_line(adj_target, arc.delta_z() != 0.0f, (segments == 1) ? feedrate : std::nullopt, extrusion);
    }
}

void ProcessorImpl::process_G10(const GCodeReader::GCodeLine& line)
{
    if (m_config.flavor == GCodeFlavor::gcfRepRapFirmware) {
        // similar to M104/M109
        float new_temp;
        if (line.has_value('S', new_temp)) {
            size_t id = m_extruder_id;
            float val;
            if (line.has_value('P', val)) {
                size_t eid = size_t(val);
                if (eid < m_extruder_temps.size())
                    id = eid;
            }

            m_extruder_temps[id] = new_temp;
            return;
        }
    }

    store_move(MoveType::Retract);
}

void ProcessorImpl::process_G28(const GCodeReader::GCodeLine& line)
{
    const std::string_view cmd = line.cmd();
    std::string new_line_raw = { cmd.data(), cmd.size() };
    bool found = false;
    if (line.has('X')) {
        new_line_raw += " X0";
        found = true;
    }
    if (line.has('Y')) {
        new_line_raw += " Y0";
        found = true;
    }
    if (line.has('Z')) {
        new_line_raw += " Z0";
        found = true;
    }
    if (!found)
        new_line_raw += " X0  Y0  Z0";

    GCodeReader::GCodeLine new_gline;
    GCodeReader reader;
    reader.parse_line(new_line_raw, [&](GCodeReader& reader, const GCodeReader::GCodeLine& gline) { new_gline = gline; });
    process_G1(new_gline);
}

void ProcessorImpl::process_G60(const GCodeReader::GCodeLine& line)
{
    if (m_config.flavor == GCodeFlavor::gcfMarlinLegacy
        || m_config.flavor == GCodeFlavor::gcfMarlinFirmware
        || m_config.flavor == GCodeFlavor::gcfPrusaFirmwareBuddy)
    {
        m_saved_position = m_end_position;
    }
}

void ProcessorImpl::process_G61(const GCodeReader::GCodeLine& line)
{
    if (m_config.flavor == GCodeFlavor::gcfMarlinLegacy
        || m_config.flavor == GCodeFlavor::gcfMarlinFirmware
        || m_config.flavor == GCodeFlavor::gcfPrusaFirmwareBuddy)
    {
        bool modified = false;
        if (line.has_x()) {
            m_end_position[X] = m_saved_position[X];
            modified = true;
        }
        if (line.has_y()) {
            m_end_position[Y] = m_saved_position[Y];
            modified = true;
        }
        if (line.has_z()) {
            m_end_position[Z] = m_saved_position[Z];
            modified = true;
        }
        if (line.has_e()) {
            m_end_position[E] = m_saved_position[E];
            modified = true;
        }
        if (line.has_f())
            m_feedrate = m_feed_multiply.current * line.f();

        if (!modified)
            m_end_position = m_saved_position;

        store_move(MoveType::Travel);
    }
}

void ProcessorImpl::process_G92(const GCodeReader::GCodeLine& line)
{
    bool any_found = false;
    if (line.has_x()) {
        m_origin[X] = m_end_position[X] - convert(line.x(), m_units, UnitsType::Millimeters);
        any_found = true;
    }

    if (line.has_y()) {
        m_origin[Y] = m_end_position[Y] - convert(line.y(), m_units, UnitsType::Millimeters);
        any_found = true;
    }

    if (line.has_z()) {
        m_origin[Z] = m_end_position[Z] - convert(line.z(), m_units, UnitsType::Millimeters);
        any_found = true;
    }

    if (line.has_e()) {
        // extruder coordinate can grow to the point where its float representation does not allow for proper addition with small increments,
        // we set the value taken from the G92 line as the new current position for it
        m_end_position[E] = convert(line.e(), m_units, UnitsType::Millimeters);
        any_found = true;
    }
    else
        simulate_st_synchronize();

    if (!any_found && !line.has_unknown_axis())
        // The G92 may be called for axes that PrusaSlicer does not recognize, for example see GH issue #3510, 
        // where G92 A0 B0 is called although the extruder axis is till E.
        m_origin = m_end_position;
}

void ProcessorImpl::process_M104(const GCodeReader::GCodeLine& line)
{
    float new_temp;
    if (line.has_value('S', new_temp)) {
        size_t id = m_extruder_id;
        float val;
        if (line.has_value('T', val)) {
            size_t eid = size_t(val);
            if (eid < m_extruder_temps.size())
                id = eid;
        }

        m_extruder_temps[id] = new_temp;
    }
}

void ProcessorImpl::process_M106(const GCodeReader::GCodeLine& line)
{
    if (!line.has('P')) {
        // The absence of P means the print cooling fan, so ignore anything else.
        float new_fan_speed;
        if (line.has_value('S', new_fan_speed))
            m_fan_speed = (100.0f / 255.0f) * new_fan_speed;
        else
            m_fan_speed = 100.0f;
    }
}

void ProcessorImpl::process_M108(const GCodeReader::GCodeLine& line)
{
    // These M-codes are used by Sailfish to change active tool.
    // They have to be processed otherwise toolchanges will be unrecognised
    // by the analyzer - see https://github.com/prusa3d/PrusaSlicer/issues/2566

    if (m_config.flavor != GCodeFlavor::gcfSailfish)
        return;

    std::string cmd = line.raw();
    size_t pos = cmd.find("T");
    if (pos != std::string::npos)
        process_T(cmd.substr(pos));
}

void ProcessorImpl::process_M109(const GCodeReader::GCodeLine& line)
{
    float new_temp;
    size_t id = (size_t)-1;
    if (line.has_value('R', new_temp)) {
        float val;
        if (line.has_value('T', val)) {
            const size_t eid = size_t(val);
            if (eid < m_extruder_temps.size())
                id = eid;
        }
        else
            id = m_extruder_id;
    }
    else if (line.has_value('S', new_temp))
        id = m_extruder_id;

    if (id != (size_t)-1)
        m_extruder_temps[id] = new_temp;
}

void ProcessorImpl::process_M132(const GCodeReader::GCodeLine& line)
{
    // This command is used by Makerbot to load the current home position from EEPROM
    // see: https://github.com/makerbot/s3g/blob/master/doc/GCodeProtocol.md
    // Using this command to reset the axis origin to zero helps in fixing: https://github.com/prusa3d/PrusaSlicer/issues/3082

    if (line.has('X')) m_origin[X] = 0.0f;
    if (line.has('Y')) m_origin[Y] = 0.0f;
    if (line.has('Z')) m_origin[Z] = 0.0f;
    if (line.has('E')) m_origin[E] = 0.0f;
}

void ProcessorImpl::process_M135(const GCodeReader::GCodeLine& line)
{
    // These M-codes are used by MakerWare to change active tool.
    // They have to be processed otherwise toolchanges will be unrecognised
    // by the analyzer - see https://github.com/prusa3d/PrusaSlicer/issues/2566

    if (m_config.flavor != GCodeFlavor::gcfMakerWare)
        return;

    const std::string cmd = line.raw();
    const size_t pos = cmd.find("T");
    if (pos != std::string::npos)
        process_T(cmd.substr(pos));
}

void ProcessorImpl::process_M201(const GCodeReader::GCodeLine& line)
{
    // see http://reprap.org/wiki/G-code#M201:_Set_max_printing_acceleration
    UnitsType units = UnitsType::Millimeters;
    if (m_units == UnitsType::Inches) {
        if (m_config.flavor != GCodeFlavor::gcfRepRapSprinter && m_config.flavor != GCodeFlavor::gcfRepRapFirmware)
            units = UnitsType::Inches;
    }

    for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
        if (TimeMode(i) == TimeMode::Normal || m_time_processor.machine_envelope_processing_enabled) {
            if (line.has_x())
                m_time_processor.machine_limits.max_acceleration_x[i] = convert(line.x(), units, UnitsType::Millimeters);
            if (line.has_y())
                m_time_processor.machine_limits.max_acceleration_y[i] = convert(line.y(), units, UnitsType::Millimeters);
            if (line.has_z())
                m_time_processor.machine_limits.max_acceleration_z[i] = convert(line.z(), units, UnitsType::Millimeters);
            if (line.has_e())
                m_time_processor.machine_limits.max_acceleration_e[i] = convert(line.e(), units, UnitsType::Millimeters);
        }
    }
}

void ProcessorImpl::process_M203(const GCodeReader::GCodeLine& line)
{
    // see http://reprap.org/wiki/G-code#M203:_Set_maximum_feedrate
    if (m_config.flavor == GCodeFlavor::gcfRepetier)
        return;

    // see http://reprap.org/wiki/G-code#M203:_Set_maximum_feedrate
    // http://smoothieware.org/supported-g-codes
    UnitsType units = UnitsType::MillimetersPerMinute;
    if (m_config.flavor == GCodeFlavor::gcfMarlinLegacy
        || m_config.flavor == GCodeFlavor::gcfMarlinFirmware
        || m_config.flavor == GCodeFlavor::gcfPrusaFirmwareBuddy
        || m_config.flavor == GCodeFlavor::gcfSmoothie)
        units = UnitsType::MillimetersPerSecond;

    for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
        if (TimeMode(i) == TimeMode::Normal || m_time_processor.machine_envelope_processing_enabled) {
            if (line.has_x())
                m_time_processor.machine_limits.max_feedrate_x[i] = convert(line.x(), units, UnitsType::MillimetersPerSecond);
            if (line.has_y())
                m_time_processor.machine_limits.max_feedrate_y[i] = convert(line.y(), units, UnitsType::MillimetersPerSecond);
            if (line.has_z())
                m_time_processor.machine_limits.max_feedrate_z[i] = convert(line.z(), units, UnitsType::MillimetersPerSecond);
            if (line.has_e())
                m_time_processor.machine_limits.max_feedrate_e[i] = convert(line.e(), units, UnitsType::MillimetersPerSecond);
        }
    }
}

void ProcessorImpl::process_M204(const GCodeReader::GCodeLine& line)
{
    float value;
    for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
        if (TimeMode(i) == TimeMode::Normal || m_time_processor.machine_envelope_processing_enabled) {
            TimeMachine& machine = m_time_processor.machines[i];
            if (line.has_value('S', value)) {
                // Legacy acceleration format. This format is used by the legacy Marlin, MK2 or MK3 firmware
                // It is also generated by PrusaSlicer to control acceleration per extrusion type
                // (perimeters, first layer etc) when 'Marlin (legacy)' flavor is used.
                machine.set_acceleration(value);
                machine.set_travel_acceleration(value);
                if (line.has_value('T', value))
                    machine.set_retract_acceleration(value);
            }
            else {
                // New acceleration format, compatible with the upstream Marlin.
                if (line.has_value('P', value))
                    machine.set_acceleration(value);
                if (line.has_value('R', value))
                    machine.set_retract_acceleration(value);
                if (line.has_value('T', value))
                    // Interpret the T value as the travel acceleration in the new Marlin format.
                    machine.set_travel_acceleration(value);
            }
        }
    }
}

void ProcessorImpl::process_M205(const GCodeReader::GCodeLine& line)
{
    for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
        if (TimeMode(i) == TimeMode::Normal || m_time_processor.machine_envelope_processing_enabled) {
            if (line.has_x()) {
                const float max_jerk = line.x();
                m_time_processor.machine_limits.max_jerk_x[i] = max_jerk;
                m_time_processor.machine_limits.max_jerk_y[i] = max_jerk;
            }
            if (line.has_y())
                m_time_processor.machine_limits.max_jerk_y[i] = line.y();
            if (line.has_z())
                m_time_processor.machine_limits.max_jerk_z[i] = line.z();
            if (line.has_e())
                m_time_processor.machine_limits.max_jerk_e[i] = line.e();

            float value;
            if (line.has_value('S', value))
                m_time_processor.machine_limits.min_extruding_rate[i] = value;
            if (line.has_value('T', value))
                m_time_processor.machine_limits.min_travel_rate[i] = value;
        }
    }
}

void ProcessorImpl::process_M220(const GCodeReader::GCodeLine& line)
{
    if (m_config.flavor != GCodeFlavor::gcfMarlinLegacy
        && m_config.flavor != GCodeFlavor::gcfMarlinFirmware
        && m_config.flavor != GCodeFlavor::gcfPrusaFirmwareBuddy
        && m_config.flavor != GCodeFlavor::gcfKlipper)
    {
        return;
    }

    if (line.has('B'))
        m_feed_multiply.saved = m_feed_multiply.current;
    float value;
    if (line.has_value('S', value))
        m_feed_multiply.current = value * 0.01f;
    if (line.has('R'))
        m_feed_multiply.current = m_feed_multiply.saved;
}

void ProcessorImpl::process_M221(const GCodeReader::GCodeLine& line)
{
    float value_s;
    float value_t;
    if (line.has_value('S', value_s) && !line.has_value('T', value_t)) {
        value_s *= 0.01f;
        for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
            m_time_processor.machines[i].extrude_factor_override_percentage = value_s;
        }
    }
}

void ProcessorImpl::process_M401(const GCodeReader::GCodeLine& line)
{
    if (m_config.flavor != GCodeFlavor::gcfRepetier)
        return;

    m_cached_position.position = m_start_position;
    m_cached_position.feedrate = m_feedrate;
}

void ProcessorImpl::process_M402(const GCodeReader::GCodeLine& line)
{
    if (m_config.flavor != GCodeFlavor::gcfRepetier)
        return;

    // see for reference:
    // https://github.com/repetier/Repetier-Firmware/blob/master/src/ArduinoAVR/Repetier/Printer.cpp
    // void Printer::GoToMemoryPosition(bool x, bool y, bool z, bool e, float feed)

    bool has_xyz = !(line.has('X') || line.has('Y') || line.has('Z'));

    float p = FLT_MAX;
    for (uint8_t a = X; a <= Z; ++a) {
        if (has_xyz || line.has(Axis(a))) {
            p = m_cached_position.position[a];
            if (p != FLT_MAX)
                m_start_position[a] = p;
        }
    }

    p = m_cached_position.position[E];
    if (p != FLT_MAX)
        m_start_position[E] = p;

    p = FLT_MAX;
    if (!line.has_value(4, p))
        p = m_cached_position.feedrate;

    if (p != FLT_MAX)
        m_feedrate = p;
}

void ProcessorImpl::process_M566(const GCodeReader::GCodeLine& line)
{
    for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
        if (line.has_x())
            m_time_processor.machine_limits.max_jerk_x[i] = convert(line.x(), UnitsType::MillimetersPerMinute, UnitsType::MillimetersPerSecond);
        if (line.has_y())
            m_time_processor.machine_limits.max_jerk_y[i] = convert(line.y(), UnitsType::MillimetersPerMinute, UnitsType::MillimetersPerSecond);
        if (line.has_z())
            m_time_processor.machine_limits.max_jerk_z[i] = convert(line.z(), UnitsType::MillimetersPerMinute, UnitsType::MillimetersPerSecond);
        if (line.has_e())
            m_time_processor.machine_limits.max_jerk_e[i] = convert(line.e(), UnitsType::MillimetersPerMinute, UnitsType::MillimetersPerSecond);
    }
}

void ProcessorImpl::process_M702(const GCodeReader::GCodeLine& line)
{
    if (line.has('C')) {
        // MK3 MMU2 specific M code:
        // // M702 C is expected to be sent by the custom end G-code when finalizing a print.
        // // The MK3 unit shall unload and park the active filament into the MMU2 unit.
        m_time_processor.extruder_unloaded = true;

        // Estimate the unload time as half of the toolchange time
        simulate_st_synchronize(m_config.filament_change_time / 2.0);
    }
}

void ProcessorImpl::process_T(const GCodeReader::GCodeLine& line)
{
    process_T(line.cmd());
}

#if __has_include(<charconv>)
template <typename T, typename = void>
struct is_from_chars_convertible : std::false_type {};
template <typename T>
struct is_from_chars_convertible<T, std::void_t<decltype(std::from_chars(std::declval<const char*>(), std::declval<const char*>(), std::declval<T&>()))>> : std::true_type {};
#endif // __has_include

// Returns true if the number was parsed correctly into out and the number spanned the whole input string.
template<typename T>
[[nodiscard]] static inline bool parse_number(const std::string_view sv, T& out)
{
    // https://www.bfilipek.com/2019/07/detect-overload-from-chars.html#example-stdfromchars
#if __has_include(<charconv>)
    // Visual Studio 19 supports from_chars all right.
    // OSX compiler that we use only implements std::from_chars just for ints.
    // GCC that we compile on does not provide <charconv> at all.
    if constexpr (is_from_chars_convertible<T>::value) {
        auto str_end = sv.data() + sv.size();
        auto [end_ptr, error_code] = std::from_chars(sv.data(), str_end, out);
        return error_code == std::errc() && end_ptr == str_end;
    } 
    else
#endif // __has_include
    {
        // Legacy conversion, which is costly due to having to make a copy of the string before conversion.
        try {
            assert(sv.size() < 1024);
            assert(sv.data() != nullptr);
            std::string str { sv };
            size_t read = 0;
            if constexpr (std::is_same_v<T, int>)
                out = std::stoi(str, &read);
            else if constexpr (std::is_same_v<T, long>)
                out = std::stol(str, &read);
            else if constexpr (std::is_same_v<T, float>)
                out = float(string_to_double_decimal_point(str, &read));
            else if constexpr (std::is_same_v<T, double>)
                out = string_to_double_decimal_point(str, &read);

            return str.size() == read;
        }
        catch (...) {
            return false;
        }
    }
}

void ProcessorImpl::process_T(const std::string_view command)
{
    if (command.length() > 1) {
        int eid = 0;
        if (!parse_number(command.substr(1), eid) || eid < 0 || eid > 255) {
            // Specific to the MMU2 V2 (see https://www.help.prusa3d.com/en/article/prusa-specific-g-codes_112173):
            if ((m_config.flavor == GCodeFlavor::gcfMarlinLegacy
                 || m_config.flavor == GCodeFlavor::gcfMarlinFirmware
                 || m_config.flavor == GCodeFlavor::gcfPrusaFirmwareBuddy)
                && (command == "Tx" || command == "Tc" || command == "T?"))
            {
                return;
            }

            // T-1 is a valid gcode line for RepRap Firmwares (used to deselects all tools) see https://github.com/prusa3d/PrusaSlicer/issues/5677
            if ((m_config.flavor != GCodeFlavor::gcfRepRapFirmware && m_config.flavor != GCodeFlavor::gcfRepRapSprinter) || eid != -1) {
                if (m_cb_log != nullptr)
                    m_cb_log("GCode::Processor encountered an invalid toolchange (" + std::string(command) + ").");
            }
        }
        else {
            uint8_t id = uint8_t(eid);
            if (m_extruder_id != id) {
                if (((m_config.producer == GCodeProducer::PrusaSlicer || m_config.producer == GCodeProducer::Slic3rPE || m_config.producer == GCodeProducer::Slic3r) && id >= m_result.extruders_count) ||
                    ((m_config.producer != GCodeProducer::PrusaSlicer && m_config.producer != GCodeProducer::Slic3rPE && m_config.producer != GCodeProducer::Slic3r) && id >= m_result.extruder_str_colors.size())) {
                    if (m_cb_log != nullptr)
                        m_cb_log("GCode::Processor encountered an invalid toolchange, maybe from a custom gcode (" + std::string(command) + ").");
                }
                else {
                    uint8_t old_extruder_id = m_extruder_id;
                    process_filaments(CustomGCode::Type::ToolChange);
                    m_extruder_id = id;
                    m_extruder_color.current = m_extruder_colors[id];
                    // Specific to the MK3 MMU2:
                    // The initial value of extruder_unloaded is set to true indicating
                    // that the filament is parked in the MMU2 unit and there is nothing to be unloaded yet.
                    float extra_time = m_time_processor.tool_change_time;
                    m_time_processor.extruder_unloaded = false;
                    if (m_config.producer == GCodeProducer::KISSlicer && m_config.flavor == GCodeFlavor::gcfMarlinLegacy)
                        extra_time += m_config.kisslicer_toolchange_time_correction;
                    simulate_st_synchronize(extra_time);

                    // specific to single extruder multi material, set the new extruder temperature
                    // to match the old one
                    if (m_config.single_extruder_multi_material)
                        m_extruder_temps[m_extruder_id] = m_extruder_temps[old_extruder_id];

                    m_result.extruders_count = std::max<uint8_t>(m_result.extruders_count, m_extruder_id + 1);
                }

                // store tool change move
                store_move(MoveType::ToolChange);
            }
        }
    }
}

GCodeExtrusionRole string_to_gcode_extrusion_role(const std::string_view role)
{
    if (role == "Perimeter")
        return GCodeExtrusionRole::Perimeter;
    else if (role == "External perimeter")
        return GCodeExtrusionRole::ExternalPerimeter;
    else if (role == "Overhang perimeter")
        return GCodeExtrusionRole::OverhangPerimeter;
    else if (role == "Internal infill")
        return GCodeExtrusionRole::InternalInfill;
    else if (role == "Solid infill")
        return GCodeExtrusionRole::SolidInfill;
    else if (role == "Top solid infill")
        return GCodeExtrusionRole::TopSolidInfill;
    else if (role == "Ironing")
        return GCodeExtrusionRole::Ironing;
    else if (role == "Bridge infill")
        return GCodeExtrusionRole::BridgeInfill;
    else if (role == "Gap fill")
        return GCodeExtrusionRole::GapFill;
    else if (role == "Skirt" || role == "Skirt/Brim") // "Skirt" is for backward compatibility with 2.3.1 and earlier
        return GCodeExtrusionRole::Skirt;
    else if (role == "Support material")
        return GCodeExtrusionRole::SupportMaterial;
    else if (role == "Support material interface")
        return GCodeExtrusionRole::SupportMaterialInterface;
    else if (role == "Wipe tower")
        return GCodeExtrusionRole::WipeTower;
    else if (role == "Custom")
        return GCodeExtrusionRole::Custom;
    else
        return GCodeExtrusionRole::None;
}

void ProcessorImpl::process_tags(const std::string_view comment)
{
    // producers tags
    if (m_config.producer != GCodeProducer::PrusaSlicer && 
        m_config.producer != GCodeProducer::AnkerMakeStudio &&
        m_config.producer != GCodeProducer::Slic3r &&
        m_config.producer != GCodeProducer::Slic3rPE &&
        m_config.producer != GCodeProducer::SuperSlicer &&
        m_config.producer != GCodeProducer::XDesktop) {
        process_producers_tags(comment);
        return;
    }

    // extrusion role tag
    std::string_view tag = reserved_tag(Tags::Role);
    size_t pos = comment.find(tag);
    if (pos == 0) {
        const std::string_view type = skip_whitespaces_both_sides(comment.substr(tag.length()));
        set_extrusion_role(string_to_gcode_extrusion_role(type));
        if (m_extrusion_role == GCodeExtrusionRole::ExternalPerimeter)
            m_seams_detection_enabled = true;
        return;
    }

    // wipe start tag
    tag = reserved_tag(Tags::Wipe_Start);
    pos = comment.find(tag);
    if (pos == 0) {
        m_wiping = true;
        return;
    }

    // wipe end tag
    tag = reserved_tag(Tags::Wipe_End);
    pos = comment.find(tag);
    if (pos == 0) {
        m_wiping = false;
        return;
    }

    // flush start tag
    if (comment.starts_with(custom_tag(CustomTags::Flush_Start))) {
        m_flushing = true;
        return;
    }

    // flush end tag
    if (comment.starts_with(custom_tag(CustomTags::Flush_End))) {
        m_flushing = false;
        return;
    }

    // exclude E start tag
    if (comment.starts_with(custom_tag(CustomTags::Exclude_E_Start))) {
        m_exclude_e = true;
        return;
    }

    // exclude E end tag
    if (comment.starts_with(custom_tag(CustomTags::Exclude_E_End))) {
        m_exclude_e = false;
        return;
    }

    // height tag
    tag = reserved_tag(Tags::Height);
    pos = comment.find(tag);
    if (pos == 0) {
        if (!parse_number(comment.substr(tag.size()), m_height_from_tag)) {
            if (m_cb_log != nullptr)
                m_cb_log("GCode::Processor encountered an invalid value for Height (" + std::string(comment) + ").");
        }
        return;
    }

    // width tag
    tag = reserved_tag(Tags::Width);
    pos = comment.find(tag);
    if (pos == 0) {
        if (!parse_number(comment.substr(tag.size()), m_width_from_tag)) {
            if (m_cb_log != nullptr)
                m_cb_log("GCode::Processor encountered an invalid value for Width (" + std::string(comment) + ").");
        }
        return;
    }

    // color change tag
    tag = reserved_tag(Tags::Color_Change);
    pos = comment.find(tag);
    if (pos == 0) {
        int8_t extruder_id = 0;
        std::string str_color = DEFAULT_COLOR_CHANGE_COLORS[0];

        std::vector<std::string> tokens;
        boost::split(tokens, comment, boost::is_any_of(","), boost::token_compress_on);
        if (tokens.size() > 1) {
            if (tokens[1][0] == 'T') {
                int eid;
                if (!parse_number(tokens[1].substr(1), eid) || eid < 0 || eid > 255) {
                    if (m_cb_log != nullptr)
                        m_cb_log("GCode::Processor encountered an invalid value for Color_Change (" + std::string(comment) + ").");
                    return;
                }
                extruder_id = int8_t(eid);
            }
        }
        if (tokens.size() > 2) {
            if (is_valid_color(tokens[2]))
                str_color = tokens[2];
        }
        else {
            str_color = DEFAULT_COLOR_CHANGE_COLORS[m_last_default_color_id];
            ++m_last_default_color_id;
            if (m_last_default_color_id == DEFAULT_COLOR_CHANGE_COLORS.size())
                m_last_default_color_id = 0;
        }

        if (extruder_id < int8_t(m_extruder_colors.size()))
            m_extruder_colors[extruder_id] = uint8_t(m_config.extruders.offsets.size()) + m_extruder_color.counter; // color_change position in list of color for preview
        ++m_extruder_color.counter;
        if (m_extruder_color.counter == UCHAR_MAX)
            m_extruder_color.counter = 0;

        const uint8_t curr_extruder_id = extruder_id;
        if (m_config.extruders.count > 1) {
            // For MMU printer this tag may be encountered before the tool change.
            // We temporary change the extruder id to match the one for the color change
            // to store it in the move generated by the call store_move()
            m_extruder_id = extruder_id;
        }

        if (m_extruder_id == extruder_id) {
            m_extruder_color.current = m_extruder_colors[extruder_id];
            store_move(MoveType::ColorChange);

            // For MMU printer this tag may be encountered before the tool change.
            // Reset proper extruder id
            if (m_config.extruders.count > 1)
                m_extruder_id = curr_extruder_id;

            CustomGCode::Item item;
            item.print_z = m_end_position[Z];
            item.type = CustomGCode::Type::ColorChange;
            item.extruder = int8_t(extruder_id + 1);
            item.color = str_color;
            m_result.custom_gcode_per_print_z.emplace_back(item);
            m_options_z_corrector.set();
            process_custom_gcode_time(CustomGCode::Type::ColorChange);
            process_filaments(CustomGCode::Type::ColorChange);
        }

        return;
    }

    // pause print tag
    tag = reserved_tag(Tags::Pause_Print);
    pos = comment.find(tag);
    if (pos == 0) {
        store_move(MoveType::PausePrint);
        CustomGCode::Item item;
        item.print_z  = m_end_position[Z];
        item.type     = CustomGCode::Type::PausePrint;
        item.extruder = int8_t(m_extruder_id + 1);
        m_result.custom_gcode_per_print_z.emplace_back(item);
        m_options_z_corrector.set();
        process_custom_gcode_time(CustomGCode::Type::PausePrint);
        return;
    }

    // custom code tag
    tag = reserved_tag(Tags::Custom_Code);
    pos = comment.find(tag);
    if (pos == 0) {
        store_move(MoveType::CustomGCode);
        CustomGCode::Item item;
        item.print_z  = m_end_position[Z];
        item.type     = CustomGCode::Type::Custom;
        item.extruder = int8_t(m_extruder_id + 1);
        m_result.custom_gcode_per_print_z.emplace_back(item);
        m_options_z_corrector.set();
        return;
    }

    // layer change tag
    tag = reserved_tag(Tags::Layer_Change);
    pos = comment.find(tag);
    if (pos == 0) {
        ++m_layer_id;
        return;
    }
}

void ProcessorImpl::process_producers_tags(const std::string_view comment)
{
    switch (m_config.producer)
    {
    case GCodeProducer::BambuStudio:     { process_bambustudio_tags(comment); break; }
    case GCodeProducer::Cura:            { process_cura_tags(comment); break; }
    case GCodeProducer::CraftWare:       { process_craftware_tags(comment); break; }
    case GCodeProducer::KISSlicer:       { process_kisslicer_tags(comment); break; }
    case GCodeProducer::ideaMaker:       { process_ideamaker_tags(comment); break; }
    case GCodeProducer::OrcaSlicer:      { process_orcaslicer_tags(comment); break; }
    case GCodeProducer::Simplify3D:      { process_simplify3d_tags(comment); break; }
    default:                             { break; }
    }
}

void ProcessorImpl::process_bambustudio_tags(const std::string_view comment)
{
    // updated to BambuStudio 1.9.7.52

    //
    // extrusion roles
    // see: BambuStudio-01.09.07.52\src\libslic3r\ExtrusionEntity.cpp
    //
    static const std::vector<std::pair<std::string_view, GCodeExtrusionRole>> ROLE_DICTIONARY = {
        { "Bottom surface"sv,        GCodeExtrusionRole::None },
        { "Bridge"sv,                GCodeExtrusionRole::BridgeInfill },
        { "Brim"sv,                  GCodeExtrusionRole::Skirt},
        { "Custom"sv,                GCodeExtrusionRole::Custom},
        { "Gap infill"sv,            GCodeExtrusionRole::GapFill },
        { "Inner wall"sv,            GCodeExtrusionRole::Perimeter },
        { "Internal solid infill"sv, GCodeExtrusionRole::SolidInfill },
        { "Ironing"sv,               GCodeExtrusionRole::Ironing },
        { "Multiple"sv,              GCodeExtrusionRole::None},
        { "Outer wall"sv,            GCodeExtrusionRole::ExternalPerimeter },
        { "Overhang wall"sv,         GCodeExtrusionRole::OverhangPerimeter },
        { "Prime tower"sv,           GCodeExtrusionRole::WipeTower},
        { "Skirt"sv,                 GCodeExtrusionRole::Skirt},
        { "Sparse infill"sv,         GCodeExtrusionRole::InternalInfill },
        { "Support"sv,               GCodeExtrusionRole::SupportMaterial},
        { "Support interface"sv,     GCodeExtrusionRole::SupportMaterialInterface},
        { "Support transition"sv,    GCodeExtrusionRole::None},
        { "Top surface"sv,           GCodeExtrusionRole::TopSolidInfill },
        { "Undefined"sv,             GCodeExtrusionRole::None },
    };

    // extrusion role tag
    std::string_view tag = "FEATURE:"sv;
    size_t pos = comment.find(tag);
    if (pos == 0) {
        std::string_view type = skip_whitespaces_both_sides(comment.substr(tag.length()));
        auto it = std::find_if(ROLE_DICTIONARY.begin(), ROLE_DICTIONARY.end(),
          [type](const std::pair<std::string_view, GCodeExtrusionRole>& item) { return item.first == type; });
        set_extrusion_role((it != ROLE_DICTIONARY.end()) ? it->second : GCodeExtrusionRole::None);

        if (m_extrusion_role == GCodeExtrusionRole::ExternalPerimeter)
            m_seams_detection_enabled = true;

        return;
    }

    // wipe start tag
    tag = reserved_tag(Tags::Wipe_Start);
    pos = comment.find(tag);
    if (pos == 0) {
        m_wiping = true;
        return;
    }

    // wipe end tag
    tag = reserved_tag(Tags::Wipe_End);
    pos = comment.find(tag);
    if (pos == 0) {
        m_wiping = false;
        return;
    }

    // height tag
    tag = "LAYER_HEIGHT"sv;
    pos = comment.find(tag);
    if (pos == 0) {
        if (!parse_number(comment.substr(tag.size()), m_height_from_tag)) {
            if (m_cb_log != nullptr)
                m_cb_log("GCode::Processor encountered an invalid value for Height (" + std::string(comment) + ").");
        }
        return;
    }

    // width tag
    tag = "LINE_WIDTH"sv;
    pos = comment.find(tag);
    if (pos == 0) {
        if (!parse_number(comment.substr(tag.size()), m_width_from_tag)) {
            if (m_cb_log != nullptr)
                m_cb_log("GCode::Processor encountered an invalid value for Width (" + std::string(comment) + ").");
        }
        return;
    }

    // layer tag
    tag = "CHANGE_LAYER"sv;
    pos = comment.find(tag);
    if (pos == 0) {
        ++m_layer_id;
        return;
    }
}

void ProcessorImpl::process_craftware_tags(const std::string_view comment)
{
    // extrusion role tag
    std::string_view tag = "@AreaBegin"sv;
    size_t pos = comment.find(tag);
    if (pos == 0) {
        std::string_view type = skip_whitespaces(comment.substr(tag.length()));
        pos = type.find("\"Bridge\""sv);
        if (pos == 0) {
            set_extrusion_role(GCodeExtrusionRole::BridgeInfill);
            return;
        }
        pos = type.find("\"Brim\""sv);
        if (pos == 0) {
            set_extrusion_role(GCodeExtrusionRole::Skirt);
            return;
        }
        pos = type.find("\"Infill\""sv);
        if (pos == 0) {
            set_extrusion_role(GCodeExtrusionRole::InternalInfill);
            return;
        }
        pos = type.find("\"Ironing\""sv);
        if (pos == 0) {
            set_extrusion_role(GCodeExtrusionRole::Ironing);
            return;
        }
        pos = type.find("\"Loop\""sv);
        if (pos == 0) {
            set_extrusion_role(GCodeExtrusionRole::None);
            return;
        }
        pos = type.find("\"Ooze\""sv);
        if (pos == 0) {
            set_extrusion_role(GCodeExtrusionRole::None);
            return;
        }
        pos = type.find("\"Perimeter\""sv);
        if (pos == 0) {
            set_extrusion_role(GCodeExtrusionRole::ExternalPerimeter);
            m_seams_detection_enabled = true;
            return;
        }
        pos = type.find("\"Pillar\""sv);
        if (pos == 0) {
            set_extrusion_role(GCodeExtrusionRole::WipeTower);
            m_seams_detection_enabled = true;
            return;
        }
        pos = type.find("\"Shell\""sv);
        if (pos == 0) {
            set_extrusion_role(GCodeExtrusionRole::None);
            return;
        }
        pos = type.find("\"Raft\""sv);
        if (pos == 0) {
            set_extrusion_role(GCodeExtrusionRole::Skirt);
            return;
        }
        pos = type.find("\"Skirt\""sv);
        if (pos == 0) {
            set_extrusion_role(GCodeExtrusionRole::Skirt);
            return;
        }
        pos = type.find("\"Support\""sv);
        if (pos == 0) {
            set_extrusion_role(GCodeExtrusionRole::SupportMaterial);
            return;
        }
        pos = type.find("\"SupportInterface\""sv);
        if (pos == 0) {
            set_extrusion_role(GCodeExtrusionRole::SupportMaterialInterface);
            return;
        }

        set_extrusion_role(GCodeExtrusionRole::None);
        return;
    }

    // layer tag
    tag = "@LayerBegin"sv;
    pos = comment.find(tag);
    if (pos == 0) {
        ++m_layer_id;
        return;
    }
}

void ProcessorImpl::process_cura_tags(const std::string_view comment)
{
    // extrusion role tag
    std::string_view tag = "TYPE:"sv;
    size_t pos = comment.find(tag);
    if (pos == 0) {
        std::string_view type = skip_whitespaces_both_sides(comment.substr(tag.length()));
        if      (type == "FILL"sv)              { set_extrusion_role(GCodeExtrusionRole::InternalInfill); }
        else if (type == "PRIME-TOWER"sv)       { set_extrusion_role(GCodeExtrusionRole::WipeTower); }
        else if (type == "SKIN"sv)              { set_extrusion_role(GCodeExtrusionRole::SolidInfill); }
        else if (type == "SKIRT"sv)             { set_extrusion_role(GCodeExtrusionRole::Skirt); }
        else if (type == "SUPPORT"sv)           { set_extrusion_role(GCodeExtrusionRole::SupportMaterial); }
        else if (type == "SUPPORT-INTERFACE"sv) { set_extrusion_role(GCodeExtrusionRole::SupportMaterialInterface); }
        else if (type == "WALL-OUTER"sv)        { set_extrusion_role(GCodeExtrusionRole::ExternalPerimeter); m_seams_detection_enabled = true; }
        else if (type == "WALL-INNER"sv)        { set_extrusion_role(GCodeExtrusionRole::Perimeter); }
        else                                    { set_extrusion_role(GCodeExtrusionRole::None); }
        return;
    }

    // layer tag
    tag = "LAYER:"sv;
    pos = comment.find(tag);
    if (pos == 0) {
        ++m_layer_id;
        return;
    }
}

void ProcessorImpl::process_kisslicer_tags(const std::string_view comment)
{
    //
    // extrusion role tags
    // 
     
    // ; 'Crown Path'
    size_t pos = comment.find("'Crown Path'"sv);
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::None);
        return;
    }
    // ; 'Destring/Wipe/Jump Path'
    pos = comment.find("'Destring/Wipe/Jump Path'"sv);
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::None);
        return;
    }
    // ; 'Loop Path'
    pos = comment.find("'Loop Path'"sv);
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::None);
        return;
    }
    // ; 'Perimeter Path'
    pos = comment.find("'Perimeter Path'"sv);
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::ExternalPerimeter);
        m_seams_detection_enabled = true;
        return;
    }
    // ; 'Pillar Path'
    pos = comment.find("'Pillar Path'"sv);
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::None);
        return;
    }
    // ; 'Prime Pillar Path'
    pos = comment.find("'Prime Pillar Path'"sv);
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::None);
        return;
    }
    // ; 'Raft Path'
    pos = comment.find("'Raft Path'"sv);
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::Skirt);
        return;
    }
    // ; 'Solid Path'
    pos = comment.find("'Solid Path'"sv);
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::None);
        return;
    }
    // ; 'Sparse Infill Path'
    pos = comment.find("'Sparse Infill Path'"sv);
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::SolidInfill);
        return;
    }
    // ; 'Stacked Sparse Infill Path'
    pos = comment.find("'Stacked Sparse Infill Path'"sv);
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::InternalInfill);
        return;
    }
    // ; 'Support (may Stack) Path'
    pos = comment.find("'Support (may Stack) Path'"sv);
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::SupportMaterial);
        return;
    }
    // ; 'Support Interface Path'
    pos = comment.find("'Support Interface Path'"sv);
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::SupportMaterialInterface);
        return;
    }
    // ; 'Travel/Ironing Path'
    pos = comment.find("'Travel/Ironing Path'"sv);
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::Ironing);
        return;
    }

    // layer tag
    pos = comment.find("BEGIN_LAYER_OBJECT"sv);
    if (pos == 0) {
        ++m_layer_id;
        return;
    }
}

void ProcessorImpl::process_ideamaker_tags(const std::string_view comment)
{
    // extrusion role tag
    std::string_view tag = "TYPE:"sv;
    size_t pos = comment.find(tag);
    if (pos == 0) {
        std::string_view type = skip_whitespaces_both_sides(comment.substr(tag.length()));
        if      (type == "BOTTOM-SURFACE"sv) { set_extrusion_role(GCodeExtrusionRole::None); }
        else if (type == "BRIDGE"sv)         { set_extrusion_role(GCodeExtrusionRole::BridgeInfill); }
        else if (type == "FILL"sv)           { set_extrusion_role(GCodeExtrusionRole::InternalInfill); }
        else if (type == "GAP-FILL"sv)       { set_extrusion_role(GCodeExtrusionRole::GapFill); }
        else if (type == "RAFT"sv)           { set_extrusion_role(GCodeExtrusionRole::Skirt); }
        else if (type == "SOLID-FILL"sv)     { set_extrusion_role(GCodeExtrusionRole::SolidInfill); }
        else if (type == "SKIRT"sv)          { set_extrusion_role(GCodeExtrusionRole::Skirt); }
        else if (type == "SUPPORT"sv)        { set_extrusion_role(GCodeExtrusionRole::SupportMaterial); }
        else if (type == "TOP-SURFACE"sv)    { set_extrusion_role(GCodeExtrusionRole::TopSolidInfill); }
        else if (type == "WALL-OUTER"sv)     { set_extrusion_role(GCodeExtrusionRole::ExternalPerimeter); m_seams_detection_enabled = true; }
        else if (type == "WALL-INNER"sv)     { set_extrusion_role(GCodeExtrusionRole::Perimeter); }
        else                                 { set_extrusion_role(GCodeExtrusionRole::None); }
        return;
    }

    // width tag
    tag = "WIDTH:"sv;
    pos = comment.find(tag);
    if (pos == 0) {
        if (!parse_number(comment.substr(tag.length()), m_width_from_tag)) {
            if (m_cb_log != nullptr)
                m_cb_log("GCode::Processor encountered an invalid value for Width (" + std::string(comment) + ").");
        }
        return;
    }

    // height tag
    tag = "HEIGHT:"sv;
    pos = comment.find(tag);
    if (pos == 0) {
        if (!parse_number(comment.substr(tag.length()), m_height_from_tag)) {
            if (m_cb_log != nullptr)
                m_cb_log("GCode::Processor encountered an invalid value for Height (" + std::string(comment) + ").");
        }
        return;
    }

    // layer tag
    pos = comment.find("LAYER:"sv);
    if (pos == 0) {
        ++m_layer_id;
        return;
    }
}

void ProcessorImpl::process_orcaslicer_tags(const std::string_view comment)
{
    // updated to OrcaSlicer 2.1.1

    //
    // extrusion roles
    // see: OrcaSlicer-2.1.1\src\libslic3r\ExtrusionEntity.cpp
    //
    static const std::vector<std::pair<std::string_view, GCodeExtrusionRole>> ROLE_DICTIONARY = {
        { "Bottom surface"sv,        GCodeExtrusionRole::None },
        { "Bridge"sv,                GCodeExtrusionRole::BridgeInfill },
        { "Brim"sv,                  GCodeExtrusionRole::Skirt},
        { "Custom"sv,                GCodeExtrusionRole::Custom},
        { "Gap infill"sv,            GCodeExtrusionRole::GapFill },
        { "Inner wall"sv,            GCodeExtrusionRole::Perimeter },
        { "Internal bridge"sv,       GCodeExtrusionRole::None },
        { "Internal solid infill"sv, GCodeExtrusionRole::SolidInfill },
        { "Ironing"sv,               GCodeExtrusionRole::Ironing },
        { "Multiple"sv,              GCodeExtrusionRole::None},
        { "Outer wall"sv,            GCodeExtrusionRole::ExternalPerimeter },
        { "Overhang wall"sv,         GCodeExtrusionRole::OverhangPerimeter },
        { "Prime tower"sv,           GCodeExtrusionRole::WipeTower},
        { "Skirt"sv,                 GCodeExtrusionRole::Skirt},
        { "Sparse infill"sv,         GCodeExtrusionRole::InternalInfill },
        { "Support"sv,               GCodeExtrusionRole::SupportMaterial},
        { "Support interface"sv,     GCodeExtrusionRole::SupportMaterialInterface},
        { "Support transition"sv,    GCodeExtrusionRole::None},
        { "Top surface"sv,           GCodeExtrusionRole::TopSolidInfill },
        { "Undefined"sv,             GCodeExtrusionRole::None },
    };

    // extrusion role tag
    std::string_view tag = "TYPE:"sv;
    size_t pos = comment.find(tag);
    if (pos == 0) {
        std::string_view type = skip_whitespaces_both_sides(comment.substr(tag.length()));
        auto it = std::find_if(ROLE_DICTIONARY.begin(), ROLE_DICTIONARY.end(),
          [type](const std::pair<std::string_view, GCodeExtrusionRole>& item) { return item.first == type; });
        set_extrusion_role((it != ROLE_DICTIONARY.end()) ? it->second : GCodeExtrusionRole::None);

        if (m_extrusion_role == GCodeExtrusionRole::ExternalPerimeter)
            m_seams_detection_enabled = true;

        return;
    }

    // wipe start tag
    tag = reserved_tag(Tags::Wipe_Start);
    pos = comment.find(tag);
    if (pos == 0) {
        m_wiping = true;
        return;
    }

    // wipe end tag
    tag = reserved_tag(Tags::Wipe_End);
    pos = comment.find(tag);
    if (pos == 0) {
        m_wiping = false;
        return;
    }

    // height tag
    tag = reserved_tag(Tags::Height);
    pos = comment.find(tag);
    if (pos == 0) {
        if (!parse_number(comment.substr(tag.size()), m_height_from_tag)) {
            if (m_cb_log != nullptr)
                m_cb_log("GCode::Processor encountered an invalid value for Height (" + std::string(comment) + ").");
        }
        return;
    }

    // width tag
    tag = reserved_tag(Tags::Width);
    pos = comment.find(tag);
    if (pos == 0) {
        if (!parse_number(comment.substr(tag.size()), m_width_from_tag)) {
            if (m_cb_log != nullptr)
                m_cb_log("GCode::Processor encountered an invalid value for Width (" + std::string(comment) + ").");
        }
        return;
    }

    // layer tag
    tag = reserved_tag(Tags::Layer_Change);
    pos = comment.find(tag);
    if (pos == 0) {
        ++m_layer_id;
        return;
    }
}

void ProcessorImpl::process_simplify3d_tags(const std::string_view comment)
{
    // extrusion role tag
    std::string_view tag = "feature"sv;
    size_t pos = comment.find(tag);
    if (pos == 0) {
        std::string_view type = skip_whitespaces_both_sides(comment.substr(tag.length()));
        if      (type == "bridge"sv)                    { set_extrusion_role(GCodeExtrusionRole::BridgeInfill); }
        else if (type == "dense support"sv)             { set_extrusion_role(GCodeExtrusionRole::SupportMaterialInterface); }
        else if (type == "gap fill"sv)                  { set_extrusion_role(GCodeExtrusionRole::GapFill); }
        else if (type == "infill"sv)                    { set_extrusion_role(GCodeExtrusionRole::SolidInfill); }
        else if (type == "inner perimeter"sv)           { set_extrusion_role(GCodeExtrusionRole::Perimeter); }
        else if (type == "internal single extrusion"sv) { set_extrusion_role(GCodeExtrusionRole::None); }
        else if (type == "ooze shield"sv)               { set_extrusion_role(GCodeExtrusionRole::None); }
        else if (type == "outer perimeter"sv)           { set_extrusion_role(GCodeExtrusionRole::ExternalPerimeter); m_seams_detection_enabled = true; }
        else if (type == "prime pillar"sv)              { set_extrusion_role(GCodeExtrusionRole::WipeTower); }
        else if (type == "raft"sv)                      { set_extrusion_role(GCodeExtrusionRole::Skirt); }
        else if (type == "skirt"sv)                     { set_extrusion_role(GCodeExtrusionRole::Skirt); }
        else if (type == "solid layer"sv)               { set_extrusion_role(GCodeExtrusionRole::SolidInfill); }
        else if (type == "support"sv)                   { set_extrusion_role(GCodeExtrusionRole::SupportMaterial); }
        else                                            { set_extrusion_role(GCodeExtrusionRole::None); }
        return;
    }

    tag = "tool "sv;
    pos = comment.find(tag);
    if (pos == 0) {
        std::string_view data = skip_whitespaces_both_sides(comment.substr(tag.length()));
        size_t h_begin = comment.find("H"sv);
        size_t h_end = comment.find_first_of(' ', h_begin);
        size_t w_begin = comment.find("W"sv);
        size_t w_end = comment.find_first_of(' ', w_begin);
        if (h_begin != data.npos) {
            if (!parse_number(comment.substr(h_begin + 1, (h_end != data.npos) ? h_end - h_begin - 1 : h_end), m_height_from_tag)) {
                if (m_cb_log != nullptr)
                    m_cb_log("GCode::Processor encountered an invalid value for Height (" + std::string(comment) + ").");
            }
        }
        if (w_begin != data.npos) {
            if (!parse_number(comment.substr(w_begin + 1, (w_end != data.npos) ? w_end - w_begin - 1 : w_end), m_width_from_tag)) {
                if (m_cb_log != nullptr)
                    m_cb_log("GCode::Processor encountered an invalid value for Width (" + std::string(comment) + ").");
            }
        }
    }

    // layer tag
    tag = "layer"sv;
    pos = comment.find(tag);
    if (pos == 0) {
        // skip layer 1 and layer end
        if (comment.find("layer 1"sv) == std::string_view::npos &&
            comment.find("layer end"sv) == std::string_view::npos) {
            ++m_layer_id;
            return;
        }
    }
}

void ProcessorImpl::process_custom_gcode_time(CustomGCode::Type code)
{
    //FIXME this simulates st_synchronize! is it correct?
    // The estimated time may be longer than the real print time.
    simulate_st_synchronize();
    for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        if (!machine.enabled)
            continue;

        CustomGCodeTime& gcode_time = machine.gcode_time;
        gcode_time.needed = true;
        if (gcode_time.cache != 0.0f) {
            gcode_time.times.push_back({ code, gcode_time.cache });
            gcode_time.cache = 0.0f;
        }
    }
}

void ProcessorImpl::process_filaments(CustomGCode::Type code)
{
    switch (code) {
    case CustomGCode::Type::ColorChange: {
        m_used_filaments.process_color_change_cache();
        break;
    }
    case CustomGCode::Type::ToolChange: {
        m_used_filaments.process_role_cache(m_result, m_extruder_id, m_extrusion_role);
        m_used_filaments.process_extruder_cache(m_extruder_id);
        break;
    }
    default: {
        break;
    }
    }
}

void ProcessorImpl::set_extrusion_role(GCodeExtrusionRole role)
{
    m_used_filaments.process_role_cache(m_result, m_extruder_id, m_extrusion_role);
    m_extrusion_role = role;
}

void ProcessorImpl::reset()
{
    m_extruder_id = 0;
    m_wiping = false;
    m_flushing = false;
    m_exclude_e = false;
    m_seams_detection_enabled = false;
    m_global_positioning_type  = PositioningType::Absolute;
    m_e_local_positioning_type = PositioningType::Absolute;
    m_extrusion_role = GCodeExtrusionRole::None;
    m_units = UnitsType::Millimeters;
    m_line_id = 0;
    m_last_line_id = 0;
    m_g1_line_id = 0;
    m_layer_id = 0;
    m_last_default_color_id = 0;
    m_feedrate = 0.0f;
    m_fan_speed = 0.0f;
    m_width = 0.0f;
    m_height = 0.0f;
    m_width_from_tag = 0.0f;
    m_height_from_tag = 0.0f;
    m_mm3_per_mm = 0.0f;
    m_extruded_last_z = 0.0f;
    m_extruder_temps = std::vector<float>(MIN_EXTRUDERS_COUNT, 0.0f);
    m_extruder_colors.resize(MIN_EXTRUDERS_COUNT);
    for (uint8_t i = 0; i < uint8_t(MIN_EXTRUDERS_COUNT); ++i) {
        m_extruder_colors[i] = i;
    }
    m_start_position = Vec4f::Zero();
    m_end_position   = Vec4f::Zero();
    m_saved_position = Vec4f::Zero();
    m_origin         = Vec4f::Zero();

    m_feed_multiply.reset();
    m_config.reset();
    m_result.reset();
    m_result.gcode().clear();
    m_result.moves().emplace_back(MoveVertex());
    m_time_processor.reset();
    m_used_filaments.reset();
    m_extruder_color.reset();
    m_options_z_corrector.reset();
    m_cached_position.reset();
}

void ProcessorImpl::simulate_st_synchronize(float additional_time)
{
    calculate_time(0, additional_time);
}

void ProcessorImpl::store_move(MoveType type, bool internal_only)
{
    m_last_line_id = (type == MoveType::ColorChange || type == MoveType::PausePrint || type == MoveType::CustomGCode) ? m_line_id + 1 :
                     (type == MoveType::Seam) ? m_last_line_id : m_line_id;

    // Extrusions between FLUSH_START/FLUSH_END are stored with a dedicated move type, so they can be
    // hidden from the G-code preview.
    const MoveType stored_type = m_flushing ? MoveType::Flush : type;

    const Vec3f& extruder_offset = m_config.extruders.offsets[m_extruder_id];

    MoveVertex move;
    move.type            = stored_type;
    move.extrusion_role  = m_extrusion_role;
    move.extruder_id     = m_extruder_id;
    move.cp_color_id     = m_extruder_color.current;
    move.gcode_id        = m_last_line_id;
    move.layer_id        = std::max<uint32_t>(1, m_layer_id) - 1;
    move.internal_only   = internal_only;
    move.delta_extruder  = m_end_position[E] - m_start_position[E];
    move.feedrate        = m_feedrate;
    move.actual_feedrate = 0.0f;
    move.width           = m_width;
    move.height          = m_height;
    move.mm3_per_mm      = m_mm3_per_mm;
    move.fan_speed       = m_fan_speed;
    move.temperature     = m_extruder_temps[m_extruder_id];
    move.mass            = 0.0f; // mass set in finalize() method
    move.position        = Vec3f{
                             m_end_position[X] + extruder_offset[X],
                             m_end_position[Y] + extruder_offset[Y],
                             m_end_position[Z] + extruder_offset[Z] - m_config.z_offset };
    move.time            = {};

    m_result.moves().emplace_back(move);

    // stores stop time placeholders for later use
    if (type == MoveType::ColorChange || type == MoveType::PausePrint) {
        for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
            TimeMachine& machine = m_time_processor.machines[i];
            if (machine.enabled)
                machine.stop_times.push_back({ m_g1_line_id, 0.0f });
        }
    }
}

void ProcessorImpl::calculate_time(size_t keep_last_n_blocks, float additional_time)
{
    MoveVertices& moves = m_result.moves();

    // calculate times
    ActualSpeedMoves actual_speed_moves;
    for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        const TimeMode mode = TimeMode(i);
        machine.calculate_time(m_result, mode, keep_last_n_blocks, additional_time);
        if (mode == TimeMode::Normal)
            actual_speed_moves = std::move(machine.actual_speed_moves);
    }

    // insert actual speed moves into the move list. We will do this in two stages (to avoid inserting in the middle of
    // result.moves repeatedly). First, we create individual vectors of MoveVertices, and store them along with their
    // required index in the result.moves vector after they are all inserted. Then we go through the destination
    // vector once and move all the elements where we want them in one go.
    std::vector<std::pair<uint32_t, MoveVertices>> moves_to_insert = { std::make_pair(0, MoveVertices{}) };
    uint32_t inserted_count = 0;
    std::vector<std::pair<uint32_t, uint32_t>> id_map;
    id_map.reserve(actual_speed_moves.size());

    for (auto it = actual_speed_moves.begin(); it != actual_speed_moves.end(); ++it) {
        const uint32_t base_id_old = it->move_id;
        if (it->position.has_value()) {
            // insert actual speed move into the move list
            // clone from existing move
            MoveVertex new_move = moves[base_id_old];
            // override modified parameters
            new_move.actual_feedrate = it->actual_feedrate;
            new_move.position        = *it->position;
            new_move.width           = *it->width;
            new_move.height          = *it->height;
            new_move.time            = {};
            new_move.feedrate        = *it->feedrate;
            new_move.delta_extruder  = *it->delta_extruder;
            new_move.mm3_per_mm      = *it->mm3_per_mm;
            new_move.fan_speed       = *it->fan_speed;
            new_move.temperature     = *it->temperature;
            new_move.internal_only   = true;
            moves_to_insert.back().second.emplace_back(new_move);
        }
        else {
            moves_to_insert.back().first = base_id_old + inserted_count; // Save required position of this range in the NEW vector.
            id_map.emplace_back(base_id_old, base_id_old + inserted_count); // Remember where the old element will end up.
            inserted_count += uint32_t(moves_to_insert.back().second.size());      // Increase the number of moves that are already planned to be added.

            moves[base_id_old].actual_feedrate = it->actual_feedrate; // update move actual speed
            
            // synchronize seams actual speed
            if (size_t(base_id_old) + 1 < moves.size()) {
                MoveVertex& move = moves[base_id_old + 1];
                if (move.type == MoveType::Seam)
                    move.actual_feedrate = it->actual_feedrate;
            }
            moves_to_insert.emplace_back(std::make_pair(0, MoveVertices{}));
        }
    }

    // Now actually do the insertion of the ranges into the destination vector.
    size_t offset = inserted_count;    
    moves.resize(moves.size() + offset); // grow the vector to its final size   
    size_t last_pos = moves.size() - 1;  // index of the last element that still needs to be moved
    for (auto it = moves_to_insert.rbegin(); it != moves_to_insert.rend(); ++it) {
        const auto& [new_pos, new_moves] = *it;
        if (new_moves.empty())
            continue;
        for (size_t i = last_pos; i >= new_pos + new_moves.size(); --i) {
            // Move the elements to their final place.
            moves[i] = moves[i - offset];
        }
        std::copy(new_moves.begin(), new_moves.end(), moves.begin() + new_pos);
        last_pos = new_pos - 1;
        offset -= new_moves.size();
    }
    assert(offset == 0);

    // synchronize blocks' move_ids after insertion of moves for actual speed 
    for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
        for (TimeBlock& block : m_time_processor.machines[i].blocks) {
            auto it = std::lower_bound(id_map.begin(), id_map.end(), block.move_id, [](const auto& a, uint32_t b) { return a.first < b; });
            block.move_id = (it != id_map.end() && it->first == block.move_id) ? it->second : block.move_id + inserted_count;
        }
    }
}

void ProcessorImpl::update_basic_statistics()
{
    auto* basic_print_stats{
        std::get_if<Domain::BasicPrintStatistics>(&m_result.print_statistics)
    };
    ASSERT(basic_print_stats);

    TimeStatistics& normal_time =
        basic_print_stats->normal_mode_time;
    normal_time.time = float(m_time_processor.machines[size_t(TimeMode::Normal)].time);
    normal_time.first_layer_time = float(m_time_processor.machines[size_t(TimeMode::Normal)].first_layer_time);
    normal_time.custom_gcode_times = m_time_processor.custom_gcode_times(TimeMode::Normal, true);
    if (m_time_processor.machines[size_t(TimeMode::Stealth)].enabled) {
        TimeStatistics silent_mode_time;
        silent_mode_time.time = float(m_time_processor.machines[size_t(TimeMode::Stealth)].time);
        silent_mode_time.first_layer_time = float(m_time_processor.machines[size_t(TimeMode::Stealth)].first_layer_time);
        silent_mode_time.custom_gcode_times =
            m_time_processor.custom_gcode_times(TimeMode::Stealth, true);
        basic_print_stats->silent_mode_time = silent_mode_time;
    }

    basic_print_stats->volumes_per_color_change = m_used_filaments.volumes_per_color_change;
    basic_print_stats->volumes_per_extruder     = m_used_filaments.volumes_per_extruder;
    basic_print_stats->wipe_tower_volumes_per_extruder =
        m_used_filaments.wipe_tower_volumes_per_extruder;
    basic_print_stats->flush_volumes_per_extruder = m_used_filaments.flush_volumes_per_extruder;
    basic_print_stats->used_filaments_per_role    = m_used_filaments.filaments_per_role;
}

} // namespace Slic3r::Biz::libpgcode
