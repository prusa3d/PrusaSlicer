
#include "Slic3r/Biz/libpgcode/ProcessorConfig.hpp"
#include "Slic3r/Biz/libpgcode/Utils.hpp"
#include "Slic3r/Biz/GCodeReader/GCodeReader.hpp"
#include "Slic3r/Assert.hpp"

#include <LocalesUtils.hpp>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/case_conv.hpp>

namespace Slic3r::Biz::libpgcode {
using namespace Domain;
using GCodeReader::GCodeReader;

std::vector<std::string> MachineLimitsConfig::validate()
{
    std::vector<std::string> ret;

    if (max_acceleration_x.empty()) {
        max_acceleration_x = { 9000.0f, 1000.0f };
        ret.push_back("max acceleration x");
    }
    if (max_acceleration_y.empty()) {
        max_acceleration_y = { 9000.0f, 1000.0f };
        ret.push_back("max acceleration y");
    }
    if (max_acceleration_z.empty()) {
        max_acceleration_z = { 500.0f,  200.0f };
        ret.push_back("max acceleration z");
    }
    if (max_acceleration_e.empty()) {
        max_acceleration_e = { 10000.0f, 5000.0f };
        ret.push_back("max acceleration e");
    }
    if (max_feedrate_x.empty()) {
        max_feedrate_x = { 500.0f, 200.0f };
        ret.push_back("max feedrate x");
    }
    if (max_feedrate_y.empty()) {
        max_feedrate_y = { 500.0f, 200.0f };
        ret.push_back("max feedrate y");
    }
    if (max_feedrate_z.empty()) {
        max_feedrate_z = { 12.0f,  12.0f };
        ret.push_back("max feedrate z");
    }
    if (max_feedrate_e.empty()) {
        max_feedrate_e = { 120.0f, 120.0f };
        ret.push_back("max feedrate e");
    }
    if (max_jerk_x.empty()) {
        max_jerk_x = { 10.0f, 10.0f };
        ret.push_back("max jerk x");
    }
    if (max_jerk_y.empty()) {
        max_jerk_y = { 10.0f, 10.0f };
        ret.push_back("max jerk y");
    }
    if (max_jerk_z.empty()) {
        max_jerk_z = { 0.2f,  0.4f };
        ret.push_back("max jerk z");
    }
    if (max_jerk_e.empty()) {
        max_jerk_e = { 2.5f,  2.5f };
        ret.push_back("max jerk e");
    }
    if (max_acceleration_extruding.empty()) {
        max_acceleration_extruding = { 1500.0f, 1250.0f };
        ret.push_back("max acceleration extruding");
    }
    if (max_acceleration_retracting.empty()) {
        max_acceleration_retracting = { 1500.0f, 1250.0f };
        ret.push_back("max acceleration retracting");
    }
    if (max_acceleration_travel.empty()) {
        max_acceleration_travel = { 1500.0f, 1250.0f };
        ret.push_back("max acceleration travel");
    }
    if (max_junction_deviation.empty()) {
        max_junction_deviation = {0.0f, 0.0f};
        ret.push_back("max junction deviation");
    }
    if (min_travel_rate.empty()) {
        min_travel_rate = { 0.0f, 0.0f };
        ret.push_back("min travel rate");
    }
    if (min_extruding_rate.empty()) {
        min_extruding_rate = { 0.0f, 0.0f };
        ret.push_back("min extruding rate");
    }

    return ret;
}

void MachineLimitsConfig::reset()
{
    max_acceleration_x.clear();
    max_acceleration_y.clear();
    max_acceleration_z.clear();
    max_acceleration_e.clear();
    max_feedrate_x.clear();
    max_feedrate_y.clear();
    max_feedrate_z.clear();
    max_feedrate_e.clear();
    max_jerk_x.clear();
    max_jerk_y.clear();
    max_jerk_z.clear();
    max_jerk_e.clear();
    max_acceleration_extruding.clear();
    max_acceleration_retracting.clear();
    max_acceleration_travel.clear();
    max_junction_deviation.clear();
    min_travel_rate.clear();
    min_extruding_rate.clear();
}

void FilamentsConfig::reset()
{
    diameters.clear();
    densities.clear();
    costs.clear();
}

void ExtrudersConfig::reset()
{
    count = MIN_EXTRUDERS_COUNT;
    offsets.clear();
    str_colors.clear();
    temps_config.clear();
    temps_first_layer_config.clear();
}

void ProcessorConfig::reset()
{
    producer = GCodeProducer::Unknown;
    flavor = GCodeFlavor::gcfRepRapSprinter;
    use_volumetric_e = false;
    export_remaining_time_enabled = false;
    stealth_time_estimator_enabled = false;
    spiral_vase_enabled = false;
    sequential_print = false;
    single_extruder_multi_material = false;
    z_offset = 0.0f;
    max_print_height = 0.0f;
    first_layer_height = 0.0f;
    parking_pos_retraction = 0.0f;
    extra_loading_move = 0.0f;
    kisslicer_toolchange_time_correction = 0.0f;
    bed_shape.clear();
    filaments.reset();
    extruders.reset();
    machine_limits.reset();
    print_settings.reset();
}

enum class KeyType : uint8_t
{
    Boolean,
    Float,
    String,
    Vector_of_floats,
    Vector_of_integers,
    Vector_of_points,
    Vector_of_strings,
};

static const std::vector<std::pair<std::string_view, KeyType>> KEYS = {
    { "bed_shape"sv,                           KeyType::Vector_of_points },
    { "complete_objects"sv,                    KeyType::Boolean },
    { "extra_loading_move"sv,                  KeyType::Float },
    { "extruder_offset"sv,                     KeyType::Vector_of_points },
    { "extruder_colour"sv,                     KeyType::Vector_of_strings },
    { "filament_colour"sv,                     KeyType::Vector_of_strings },
    { "filament_cost"sv,                       KeyType::Vector_of_floats },
    { "filament_density"sv,                    KeyType::Vector_of_floats },
    { "filament_diameter"sv,                   KeyType::Vector_of_floats },
    { "filament_load_time"sv,                  KeyType::Vector_of_floats },
    { "filament_settings_id"sv,                KeyType::Vector_of_strings },
    { "filament_unload_time"sv,                KeyType::Vector_of_floats },
    { "first_layer_height"sv,                  KeyType::Float },
    { "first_layer_temperature",               KeyType::Vector_of_integers },
    { "gcode_flavor"sv,                        KeyType::String },
    { "max_print_height"sv,                    KeyType::Float },
    { "nozzle_diameter"sv,                     KeyType::Vector_of_floats },
    { "parking_pos_retraction"sv,              KeyType::Float },
    { "print_settings_id"sv,                   KeyType::String },
    { "printer_notes"sv,                       KeyType::String },
    { "printer_settings_id"sv,                 KeyType::String },
    { "silent_mode",                           KeyType::Boolean },
    { "single_extruder_multi_material"sv,      KeyType::Boolean },
    { "spiral_vase"sv,                         KeyType::Boolean },
    { "temperature",                           KeyType::Vector_of_integers },
    { "use_volumetric_e"sv,                    KeyType::Boolean },
    { "wipe_tower"sv,                          KeyType::Boolean },
    { "z_offset"sv,                            KeyType::Float },
    { "machine_limits_usage"sv,                KeyType::String },
    { "machine_max_acceleration_x"sv,          KeyType::Vector_of_floats },
    { "machine_max_acceleration_y"sv,          KeyType::Vector_of_floats },
    { "machine_max_acceleration_z"sv,          KeyType::Vector_of_floats },
    { "machine_max_acceleration_e"sv,          KeyType::Vector_of_floats },
    { "machine_max_feedrate_x"sv,              KeyType::Vector_of_floats },
    { "machine_max_feedrate_y"sv,              KeyType::Vector_of_floats },
    { "machine_max_feedrate_z"sv,              KeyType::Vector_of_floats },
    { "machine_max_feedrate_e"sv,              KeyType::Vector_of_floats },
    { "machine_max_jerk_x"sv,                  KeyType::Vector_of_floats },
    { "machine_max_jerk_y"sv,                  KeyType::Vector_of_floats },
    { "machine_max_jerk_z"sv,                  KeyType::Vector_of_floats },
    { "machine_max_jerk_e"sv,                  KeyType::Vector_of_floats },
    { "machine_max_acceleration_extruding"sv,  KeyType::Vector_of_floats },
    { "machine_max_acceleration_retracting"sv, KeyType::Vector_of_floats },
    { "machine_max_acceleration_travel"sv,     KeyType::Vector_of_floats },
    { "machine_max_junction_deviation"sv,      KeyType::Vector_of_floats },
    { "machine_min_extruding_rate"sv,          KeyType::Vector_of_floats },
    { "machine_min_travel_rate"sv,             KeyType::Vector_of_floats },
    { "color_change_gcode"sv,                  KeyType::String },
    { "pause_print_gcode"sv,                   KeyType::String },
    { "template_custom_gcode"sv,               KeyType::String },
};

// updated to BambuStudio 1.9.7.52
static const std::vector<std::pair<std::string_view, std::string_view>> KEYS_BAMBUSTUDIO_DICTIONARY = {
    { "spiral_vase"sv, "spiral_mode"sv },
    { "max_print_height"sv, "max_z_height"sv },
    { "machine_max_feedrate_x"sv, "machine_max_speed_x"sv },
    { "machine_max_feedrate_y"sv, "machine_max_speed_y"sv },
    { "machine_max_feedrate_z"sv, "machine_max_speed_z"sv },
    { "machine_max_feedrate_e"sv, "machine_max_speed_e"sv },
    //{ "use_volumetric_e"sv, ""sv },
    //{ "wipe_tower"sv, ""sv },
    //{ "extra_loading_move"sv, ""sv },
    //{ "first_layer_height"sv, ""sv },
    //{ "parking_pos_retraction"sv, ""sv },
    //{ "z_offset"sv, ""sv },
    //{ "machine_limits_usage"sv, ""sv },
    //{ "filament_load_time"sv, ""sv },
    //{ "filament_unload_time"sv, ""sv },
};

// updated to Orcaslicer 2.1.1
static const std::vector<std::pair<std::string_view, std::string_view>> KEYS_ORCASLICER_DICTIONARY = {
    { "spiral_vase"sv, "spiral_mode"sv },
    { "max_print_height"sv, "max_z_height"sv },
    { "machine_max_feedrate_x"sv, "machine_max_speed_x"sv },
    { "machine_max_feedrate_y"sv, "machine_max_speed_y"sv },
    { "machine_max_feedrate_z"sv, "machine_max_speed_z"sv },
    { "machine_max_feedrate_e"sv, "machine_max_speed_e"sv },
    //{ "use_volumetric_e"sv, ""sv },
    //{ "wipe_tower"sv, ""sv },
    //{ "first_layer_height"sv, ""sv },
    //{ "machine_limits_usage"sv, ""sv },
};

static bool extract_boolean(std::string_view data)
{
    data = skip_whitespaces_both_sides(data);
    return data == "1";
}

static float extract_float(std::string_view data)
{
    data = skip_whitespaces_both_sides(data);
    return float(string_to_double_decimal_point(data, nullptr));
}

static std::vector<float> extract_vector_of_floats(std::string_view data)
{
    data = skip_whitespaces_both_sides(data);
    std::vector<float> ret;
    std::vector<std::string> values;
    boost::split(values, std::string(data), boost::is_any_of(","));
    for (size_t i = 0; i < values.size(); ++i) {
        ret.push_back(extract_float(values[i]));
    }
    return ret;
}

static std::vector<int> extract_vector_of_integers(std::string_view data)
{
    data = skip_whitespaces_both_sides(data);
    std::vector<int> ret;
    std::vector<std::string> values;
    boost::split(values, std::string(data), boost::is_any_of(","));
    for (size_t i = 0; i < values.size(); ++i) {
        try
        {
            const int v = std::stoi(values[i]);
            ret.push_back(v);
        }
        catch (...)
        {
            ret.clear();
            return ret;
        }
    }
    return ret;
}

static std::vector<Vec2f> extract_vector_of_points(std::string_view data)
{
    data = skip_whitespaces_both_sides(data);
    std::vector<Vec2f> ret;
    std::vector<std::string> values;
    boost::split(values, std::string(data), boost::is_any_of(","));
    for (size_t i = 0; i < values.size(); ++i) {
        std::vector<std::string> coords;
        boost::split(coords, values[i], boost::is_any_of("x"));
        if (coords.size() == 2) {
            Vec2f point = Vec2f::Zero();
            for (size_t j = 0; j < coords.size(); ++j) {
                point[j] = extract_float(coords[j]);
            }
            ret.push_back(point);
        }
    }
    return ret;
}

static std::vector<std::string> extract_vector_of_strings(std::string_view data)
{
    data = skip_whitespaces_both_sides(data);
    std::vector<std::string> ret;
    if (data == "\"\"")
        ret.push_back("");
    else {
        std::vector<std::string> values;
        boost::split(values, std::string(data), boost::is_any_of(";"));
        for (size_t i = 0; i < values.size(); ++i) {
            std::string v = skip_whitespaces_both_sides(values[i]);
            size_t begin_quote = v.find('\"');
            if (begin_quote != std::string::npos) {
                size_t end_quote = v.find('\"', begin_quote + 1);
                if (end_quote != std::string::npos)
                    v = v.substr(begin_quote + 1, end_quote - begin_quote - 1);
                else {
                    ret.clear();
                    return ret;
                }
            }
            ret.push_back(v);
        }
    }
    return ret;
}

GCodeFlavor flavor_from_string(std::string_view str)
{
    if      (str == "reprap"sv)         { return GCodeFlavor::gcfRepRapSprinter; }
    else if (str == "reprapfirmware"sv) { return GCodeFlavor::gcfRepRapFirmware; }
    else if (str == "repetier"sv)       { return GCodeFlavor::gcfRepetier; }
    else if (str == "teacup"sv)         { return GCodeFlavor::gcfTeacup; }
    else if (str == "makerware"sv)      { return GCodeFlavor::gcfMakerWare; }
    else if (str == "marlin"sv)         { return GCodeFlavor::gcfMarlinLegacy; }
    else if (str == "marlin2"sv)        { return GCodeFlavor::gcfMarlinFirmware; }
    else if (str == "prusabuddy"sv)     { return GCodeFlavor::gcfPrusaFirmwareBuddy; }
    else if (str == "klipper"sv)        { return GCodeFlavor::gcfKlipper; }
    else if (str == "sailfish"sv)       { return GCodeFlavor::gcfSailfish; }
    else if (str == "smoothie"sv)       { return GCodeFlavor::gcfSmoothie; }
    else if (str == "mach3"sv)          { return GCodeFlavor::gcfMach3; }
    else if (str == "machinekit"sv)     { return GCodeFlavor::gcfMachinekit; }
    else if (str == "no-extrusion"sv)   { return GCodeFlavor::gcfNoExtrusion; }
    else                                { assert(false); return GCodeFlavor::gcfRepRapSprinter; }
}

static std::string replace_double_slash_n(const std::string& str)
{
    std::string ret = str;
    std::string word = "\\n";
    std::string replace_by = "\n";
    size_t pos = ret.find(word);
    while (pos != std::string::npos) {
        ret.replace(pos, word.size(), replace_by);
        pos = ret.find(word, pos + replace_by.size());
    }
    return ret;
}

float sum(const std::vector<float>& values) {
    float total = 0.0f;
    for (const float& val : values) {
        total += val;
    }
    return total;
}

ProcessorConfig extract_processor_config_from_prusaslicer_gcode_internal(const std::string& gcode,
    const std::string& data_separators = "=", const std::vector<std::pair<std::string_view, std::string_view>>& dictionary = {})
{
    ProcessorConfig ret;

    bool has_wipe_tower = false;
    bool has_silent_mode = false;
    float extra_loading_move = 0.0f;
    float parking_pos_retraction = 0.0f;
    std::string printer_notes;
    std::vector<std::string> extruders_colors;
    std::vector<std::string> filaments_colors;
    MachineLimitsConfig machine_limits;

    GCodeReader parser;
    parser.parse_buffer(gcode, [&ret, &dictionary, &data_separators , &has_silent_mode, &has_wipe_tower, &extra_loading_move,
        &parking_pos_retraction, &printer_notes, &machine_limits, &extruders_colors, &filaments_colors](GCodeReader&, const GCodeReader::GCodeLine& line) {
        const std::string& raw = line.raw();
        std::string_view sv_raw = skip_whitespaces(std::string_view(raw));
        if (sv_raw.length() > 0 && sv_raw.front() == ';') {
            std::string_view cmt = line.comment();
            std::vector<std::string> tokens;
            boost::split(tokens, std::string(cmt), boost::is_any_of(data_separators));
            if (tokens.size() == 2 && !tokens.back().empty()) {
                std::string label = skip_whitespaces_both_sides(tokens.front());
                auto dic_it = std::find_if(dictionary.begin(), dictionary.end(),
                  [&label](std::pair<std::string_view, std::string_view> key) { return key.second == std::string_view(label); });
                if (dic_it != dictionary.end())
                    label = dic_it->first;

                auto key_it = std::find_if(KEYS.begin(), KEYS.end(),
                  [label](std::pair<std::string_view, KeyType> key) { return key.first == std::string_view(label); });

                if (key_it != KEYS.end()) {
                    switch (key_it->second)
                    {
                    case KeyType::Boolean:
                    {
                        bool value = extract_boolean(tokens.back());
                        if      (key_it->first == "silent_mode"sv)                    has_silent_mode = value;
                        else if (key_it->first == "single_extruder_multi_material"sv) ret.single_extruder_multi_material = value;
                        else if (key_it->first == "spiral_vase"sv)                    ret.spiral_vase_enabled = value;
                        else if (key_it->first == "complete_objects"sv)               ret.sequential_print = value;
                        else if (key_it->first == "use_volumetric_e"sv)               ret.use_volumetric_e = value;
                        else if (key_it->first == "wipe_tower"sv)                     has_wipe_tower = value;
                        break;
                    }
                    case KeyType::Float:
                    {
                        float value = extract_float(tokens.back());
                        if      (key_it->first == "extra_loading_move"sv)     extra_loading_move = value;
                        else if (key_it->first == "first_layer_height"sv)     ret.first_layer_height = value;
                        else if (key_it->first == "max_print_height"sv)       ret.max_print_height = value;
                        else if (key_it->first == "parking_pos_retraction"sv) parking_pos_retraction = value;
                        else if (key_it->first == "z_offset"sv)               ret.z_offset = value;
                        else if (key_it->first == "filament_change_time"sv)   ret.filament_change_time = value;
                        break;
                    }
                    case KeyType::String:
                    {
                        std::string value = skip_whitespaces_both_sides(tokens.back());
                        if      (key_it->first == "gcode_flavor"sv)          ret.flavor = flavor_from_string(value);
                        else if (key_it->first == "print_settings_id"sv)     ret.print_settings.print = value;
                        else if (key_it->first == "printer_notes"sv)         printer_notes = value;
                        else if (key_it->first == "printer_settings_id"sv)   ret.print_settings.printer = value;
                        else if (key_it->first == "color_change_gcode"sv)    ret.color_change_gcode = value;
                        else if (key_it->first == "pause_print_gcode"sv)     ret.pause_print_gcode = value;
                        else if (key_it->first == "template_custom_gcode"sv) ret.template_custom_gcode = value;
                        else if (key_it->first == "machine_limits_usage"sv) {
                            if      (value == "emit_to_gcode")      machine_limits.usage = MachineLimitsUsageType::EmitToGCode;
                            else if (value == "time_estimate_only") machine_limits.usage = MachineLimitsUsageType::TimeEstimateOnly;
                            else                                    machine_limits.usage = MachineLimitsUsageType::Ignore;
                        }
                        break;
                    }
                    case KeyType::Vector_of_floats:
                    {
                        std::vector<float> values = extract_vector_of_floats(tokens.back());
                        if (!values.empty()) {
                            if      (key_it->first == "filament_cost"sv)                       ret.filaments.costs = values;
                            else if (key_it->first == "filament_density"sv)                    ret.filaments.densities = values;
                            else if (key_it->first == "filament_diameter"sv)                   ret.filaments.diameters = values;
                            else if (key_it->first == "filament_load_time"sv)                  ret.filament_change_time += sum(values);
                            else if (key_it->first == "filament_unload_time"sv)                ret.filament_change_time += sum(values);
                            else if (key_it->first == "nozzle_diameter"sv)                     ret.extruders.count = uint8_t(values.size());
                            else if (key_it->first == "machine_max_acceleration_x"sv)          machine_limits.max_acceleration_x = values;
                            else if (key_it->first == "machine_max_acceleration_y"sv)          machine_limits.max_acceleration_y = values;
                            else if (key_it->first == "machine_max_acceleration_z"sv)          machine_limits.max_acceleration_z = values;
                            else if (key_it->first == "machine_max_acceleration_e"sv)          machine_limits.max_acceleration_e = values;
                            else if (key_it->first == "machine_max_feedrate_x"sv)              machine_limits.max_feedrate_x = values;
                            else if (key_it->first == "machine_max_feedrate_y"sv)              machine_limits.max_feedrate_y = values;
                            else if (key_it->first == "machine_max_feedrate_z"sv)              machine_limits.max_feedrate_z = values;
                            else if (key_it->first == "machine_max_feedrate_e"sv)              machine_limits.max_feedrate_e = values;
                            else if (key_it->first == "machine_max_jerk_x"sv)                  machine_limits.max_jerk_x = values;
                            else if (key_it->first == "machine_max_jerk_y"sv)                  machine_limits.max_jerk_y = values;
                            else if (key_it->first == "machine_max_jerk_z"sv)                  machine_limits.max_jerk_z = values;
                            else if (key_it->first == "machine_max_jerk_e"sv)                  machine_limits.max_jerk_e = values;
                            else if (key_it->first == "machine_max_acceleration_extruding"sv)  machine_limits.max_acceleration_extruding = values;
                            else if (key_it->first == "machine_max_acceleration_retracting"sv) machine_limits.max_acceleration_retracting = values;
                            else if (key_it->first == "machine_max_acceleration_travel"sv)     machine_limits.max_acceleration_travel = values;
                            else if (key_it->first == "machine_max_junction_deviation"sv)      machine_limits.max_junction_deviation = values;
                            else if (key_it->first == "machine_min_extruding_rate"sv)          machine_limits.min_extruding_rate = values;
                            else if (key_it->first == "machine_min_travel_rate"sv)             machine_limits.min_travel_rate = values;
                        }
                        break;
                    }
                    case KeyType::Vector_of_integers:
                    {
                        std::vector<int> values = extract_vector_of_integers(tokens.back());
                        if (!values.empty()) {
                            if      (key_it->first == "first_layer_temperature"sv) ret.extruders.temps_first_layer_config = values;
                            else if (key_it->first == "temperature"sv)             ret.extruders.temps_config = values;
                        }
                        break;
                    }

                    case KeyType::Vector_of_points:
                    {
                        std::vector<Vec2f> values = extract_vector_of_points(tokens.back());
                        if (!values.empty()) {
                            if      (key_it->first == "bed_shape"sv) ret.bed_shape = values;
                            else if (key_it->first == "extruder_offset"sv) {
                                std::transform(values.begin(), values.end(), std::back_inserter(ret.extruders.offsets),
                                    [](const Vec2f& in) { const Vec3f v = { in[0], in[1], 0.0f }; return v; });
                            }
                        }
                        break;
                    }
                    case KeyType::Vector_of_strings:
                    {
                        std::vector<std::string> values = extract_vector_of_strings(tokens.back());
                        if (!values.empty()) {
                            if      (key_it->first == "extruder_colour"sv)      extruders_colors = values;
                            else if (key_it->first == "filament_colour"sv)      filaments_colors = values;
                            else if (key_it->first == "filament_settings_id"sv) ret.print_settings.filament = values;
                        }
                        break;
                    }
                    }
                }
            }
        }
    });

    if (machine_limits.usage != MachineLimitsUsageType::Ignore &&
        (ret.flavor == GCodeFlavor::gcfMarlinLegacy ||
         ret.flavor == GCodeFlavor::gcfMarlinFirmware ||
         ret.flavor == GCodeFlavor::gcfPrusaFirmwareBuddy ||
         ret.flavor == GCodeFlavor::gcfRepRapFirmware ||
         ret.flavor == GCodeFlavor::gcfKlipper)) {
        ret.machine_limits = std::move(machine_limits);
        // Legacy Marlin and Klipper don't have separate travel acceleration, they use the 'extruding' value instead.
        if (ret.flavor == GCodeFlavor::gcfMarlinLegacy || ret.flavor == GCodeFlavor::gcfKlipper)
            ret.machine_limits.max_acceleration_travel = ret.machine_limits.max_acceleration_extruding;
        if (ret.flavor == GCodeFlavor::gcfRepRapFirmware) {
            // RRF does not support setting min feedrates. Set to zero.
            ret.machine_limits.min_extruding_rate.assign(ret.machine_limits.min_extruding_rate.size(), 0.0f);
            // RRF does not support setting min feedrates. Set to zero.
            ret.machine_limits.min_travel_rate.assign(ret.machine_limits.min_travel_rate.size(), 0.0f);
        }
    }
    else
        ret.machine_limits.usage = MachineLimitsUsageType::Ignore;

    if (ret.single_extruder_multi_material && has_wipe_tower && ret.extruders.count > 1) {
        ret.extra_loading_move = extra_loading_move;
        ret.parking_pos_retraction = parking_pos_retraction;
    }

    ret.extruders.str_colors = extruders_colors;
    // try to replace missing values with filament colors
    if (filaments_colors.size() == extruders_colors.size()) {
        for (size_t i = 0; i < ret.extruders.str_colors.size(); ++i) {
            if (ret.extruders.str_colors[i].empty())
                ret.extruders.str_colors[i] = filaments_colors[i];
        }
    }

    // No Klipper here, it does not support silent mode.
    if (has_silent_mode
        && (ret.flavor == GCodeFlavor::gcfMarlinLegacy
            || ret.flavor == GCodeFlavor::gcfMarlinFirmware
            || ret.flavor == GCodeFlavor::gcfPrusaFirmwareBuddy)
        && ret.machine_limits.max_acceleration_x.size() > 1)
    {
        ret.stealth_time_estimator_enabled = true;
    }

    if (boost::algorithm::contains(printer_notes, "PRINTER_VENDOR_PRUSA3D") &&
        boost::algorithm::contains(printer_notes, "PRINTER_MODEL_XL"))
        ret.do_M104_backtrace = true;
    PANIC("Must load hw config!");

    ret.producer = GCodeProducer::PrusaSlicer;
    ret.color_change_gcode = replace_double_slash_n(ret.color_change_gcode);
    ret.pause_print_gcode = replace_double_slash_n(ret.pause_print_gcode);
    ret.template_custom_gcode = replace_double_slash_n(ret.template_custom_gcode);
    return ret;
}

ProcessorConfig extract_processor_config_from_prusaslicer_gcode(const std::string& gcode)
{
    return extract_processor_config_from_prusaslicer_gcode_internal(gcode);
}

ProcessorConfig extract_processor_config_from_ankermakestudio_gcode(const std::string& gcode)
{
    ProcessorConfig ret = extract_processor_config_from_prusaslicer_gcode_internal(gcode, "=:");
    ret.producer = GCodeProducer::AnkerMakeStudio;
    return ret;
}

ProcessorConfig extract_processor_config_from_bambustudio_gcode(const std::string& gcode)
{
    ProcessorConfig ret = extract_processor_config_from_prusaslicer_gcode_internal(gcode, "=:", KEYS_BAMBUSTUDIO_DICTIONARY);
    ret.producer = GCodeProducer::BambuStudio;
    return ret;
}

ProcessorConfig extract_processor_config_from_craftware_gcode(const std::string& gcode)
{
    ProcessorConfig ret;
    ret.producer = GCodeProducer::CraftWare;
    return ret;
}

// updated to Cura 5.8.1
static const std::vector<std::pair<std::string_view, GCodeFlavor>> CURA_FLAVORS = {
    { "BFB"sv,                GCodeFlavor::gcfMarlinLegacy },
    { "Mach3"sv,              GCodeFlavor::gcfMach3 },
    { "Makerbot"sv,           GCodeFlavor::gcfMakerWare },
    { "UltiGCode"sv,          GCodeFlavor::gcfMarlinLegacy },
    { "Marlin(Volumetric)"sv, GCodeFlavor::gcfMarlinLegacy },
    { "Griffin"sv,            GCodeFlavor::gcfMarlinLegacy },
    { "Repetier"sv,           GCodeFlavor::gcfRepetier },
    { "RepRap"sv,             GCodeFlavor::gcfRepRapFirmware },
    { "Marlin"sv,             GCodeFlavor::gcfMarlinLegacy },
};

ProcessorConfig extract_processor_config_from_cura_gcode(const std::string& gcode)
{
    ProcessorConfig ret;

    static const std::string& DATA_SEPARATORS = ":";

    GCodeReader parser;
    parser.parse_buffer(gcode, [&ret](GCodeReader&, const GCodeReader::GCodeLine& line) {
        const std::string& raw = line.raw();
        std::string_view sv_raw = skip_whitespaces(std::string_view(raw));
        if (sv_raw.length() > 0 && sv_raw.front() == ';') {
            std::string_view cmt = line.comment();
            std::vector<std::string> tokens;
            boost::split(tokens, std::string(cmt), boost::is_any_of(DATA_SEPARATORS));
            if (tokens.size() == 2 && !tokens.back().empty()) {
                const std::string& label = skip_whitespaces_both_sides(tokens.front());
                const std::string& value = skip_whitespaces_both_sides(tokens.back());
                if (label == "FLAVOR") {
                    auto it = std::find_if(CURA_FLAVORS.begin(), CURA_FLAVORS.end(),
                        [&value](const std::pair<std::string_view, GCodeFlavor>& item) { return item.first == value; });
                    if (it != CURA_FLAVORS.end())
                        ret.flavor = it->second;
                }
                else if (label == "TARGET_MACHINE.NAME")
                    ret.print_settings.printer = value;
            }
        }
    });

    ret.producer = GCodeProducer::Cura;
    return ret;
}

ProcessorConfig extract_processor_config_from_kisslicer_gcode(const std::string& gcode)
{
    ProcessorConfig ret;

    static const std::string& DATA_SEPARATORS = "=";

    GCodeReader parser;
    parser.parse_buffer(gcode, [&ret](GCodeReader& parser, const GCodeReader::GCodeLine& line) {
        const std::string& raw = line.raw();
        std::string_view sv_raw = skip_whitespaces(std::string_view(raw));
        if (sv_raw.length() > 0 && sv_raw.front() == ';') {
            std::string_view cmt = line.comment();
            std::vector<std::string> tokens;
            boost::split(tokens, std::string(cmt), boost::is_any_of(DATA_SEPARATORS));
            if (tokens.size() == 2 && !tokens.back().empty()) {
                const std::string& label = skip_whitespaces_both_sides(tokens.front());
                const std::string& value = skip_whitespaces_both_sides(tokens.back());
                if (label == "printer_name") {
                    ret.print_settings.printer = value;
                    std::string upper = boost::to_upper_copy(value);
                    if (boost::contains(upper, "MK2.5") || boost::contains(upper, "MK3"))
                        ret.kisslicer_toolchange_time_correction = 18.0f; // MMU2
                    else if (boost::contains(upper, "MK2"))
                        ret.kisslicer_toolchange_time_correction = 5.0f; // MMU
                    parser.quit_parsing();
                }
            }
        }
    });

    ret.flavor = GCodeFlavor::gcfMarlinLegacy;
    ret.producer = GCodeProducer::KISSlicer;
    return ret;
}

ProcessorConfig extract_processor_config_from_ideamaker_gcode(const std::string& gcode)
{
    ProcessorConfig ret;
    ret.producer = GCodeProducer::ideaMaker;
    return ret;
}

ProcessorConfig extract_processor_config_from_orcaslicer_gcode(const std::string& gcode)
{
    ProcessorConfig ret = extract_processor_config_from_prusaslicer_gcode_internal(gcode, "=:", KEYS_ORCASLICER_DICTIONARY);
    ret.producer = GCodeProducer::OrcaSlicer;
    return ret;
}

ProcessorConfig extract_processor_config_from_simplify3d_gcode(const std::string& gcode)
{
    ProcessorConfig ret;
    ret.producer = GCodeProducer::Simplify3D;
    return ret;
}

ProcessorConfig extract_processor_config_from_superslicer_gcode(const std::string& gcode)
{
    ProcessorConfig ret = extract_processor_config_from_prusaslicer_gcode_internal(gcode);
    ret.producer = GCodeProducer::SuperSlicer;
    return ret;
}

ProcessorConfig extract_processor_config_from_xdesktop_gcode(const std::string& gcode)
{
    ProcessorConfig ret = extract_processor_config_from_prusaslicer_gcode_internal(gcode);
    ret.producer = GCodeProducer::XDesktop;
    return ret;
}

} // namespace Slic3r::Biz::libpgcode
