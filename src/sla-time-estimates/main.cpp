#include <sla-time-estimates/SLATimeEstimate.hpp>

#include <nlohmann/json.hpp>

#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using json = nlohmann::json;
using namespace Slic3r;

// --- Speed profile name parsers ---

// "layer{N}" → N * 800 usteps/s
// Tower speed profiles encode the layer number, not the speed directly.
static int parse_tower_speed(const std::string& profile)
{
    const std::string prefix = "layer";
    if (profile.substr(0, prefix.size()) == prefix)
        return std::stoi(profile.substr(prefix.size())) * 800;
    throw std::runtime_error("Unknown tower profile: '" + profile + "'");
}

// "layer{N}" or "move{N}" → N usteps/s
// Tilt speed profiles encode the speed directly in the name.
static int parse_tilt_speed(const std::string& profile)
{
    if (profile.substr(0, 5) == "layer") return std::stoi(profile.substr(5));
    if (profile.substr(0, 4) == "move")  return std::stoi(profile.substr(4));
    throw std::runtime_error("Unknown tilt profile: '" + profile + "'");
}

// --- ExposureProfile from JSON ---

static ExposureProfile parse_exposure_profile(const json& j)
{
    ExposureProfile ep;
    ep.delay_before_exposure_ms  = j.at("delay_before_exposure_ms").get<int>();
    ep.delay_after_exposure_ms   = j.at("delay_after_exposure_ms").get<int>();
    ep.delay_to_reflood_ms       = j.at("delay_to_reflood_ms").get<int>();
    ep.tower_hop_height_nm       = j.at("tower_hop_height_nm").get<int>();
    ep.use_tilt                  = j.at("use_tilt").get<bool>();
    ep.tilt_down_offset_steps    = j.at("tilt_down_offset_steps").get<int>();
    ep.tilt_down_offset_delay_ms = j.at("tilt_down_offset_delay_ms").get<int>();
    ep.tilt_down_cycles          = j.at("tilt_down_cycles").get<int>();
    ep.tilt_down_delay_ms        = j.at("tilt_down_delay_ms").get<int>();
    ep.tilt_up_offset_steps      = j.at("tilt_up_offset_steps").get<int>();
    ep.tilt_up_offset_delay_ms   = j.at("tilt_up_offset_delay_ms").get<int>();
    ep.tilt_up_cycles            = j.at("tilt_up_cycles").get<int>();
    ep.tilt_up_delay_ms          = j.at("tilt_up_delay_ms").get<int>();
    ep.tower_speed             = parse_tower_speed(j.at("tower_profile").get<std::string>());
    ep.tilt_down_initial_speed = parse_tilt_speed(j.at("tilt_down_initial_profile").get<std::string>());
    ep.tilt_down_finish_speed  = parse_tilt_speed(j.at("tilt_down_finish_profile").get<std::string>());
    ep.tilt_up_initial_speed   = parse_tilt_speed(j.at("tilt_up_initial_profile").get<std::string>());
    ep.tilt_up_finish_speed    = parse_tilt_speed(j.at("tilt_up_finish_profile").get<std::string>());
    return ep;
}

// --- main ---

int main(int argc, char* argv[])
{
    double layer_height = -1.0;
    long   layer_idx    = -1;
    double layer_area   = -1.0;
    double display_area = -1.0;
    std::string json_file;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--layer-height") == 0 && i + 1 < argc)
            layer_height = std::stod(argv[++i]);
        else if (std::strcmp(argv[i], "--layer-idx") == 0 && i + 1 < argc)
            layer_idx = std::stol(argv[++i]);
        else if (std::strcmp(argv[i], "--layer-area") == 0 && i + 1 < argc)
            layer_area = std::stod(argv[++i]);
        else if (std::strcmp(argv[i], "--display-area") == 0 && i + 1 < argc)
            display_area = std::stod(argv[++i]);
        else if (argv[i][0] != '-')
            json_file = argv[i];
    }

    bool bad = false;
    if (layer_height < 0) { std::cerr << "Error: --layer-height <mm> is required\n";    bad = true; }
    if (layer_idx    < 0) { std::cerr << "Error: --layer-idx <N> is required\n";        bad = true; }
    if (layer_area   < 0) { std::cerr << "Error: --layer-area <mm2> is required\n";     bad = true; }
    if (json_file.empty()){ std::cerr << "Error: JSON config file argument is required\n"; bad = true; }
    if (bad) {
        std::cerr << "Usage: " << argv[0]
                  << " --layer-height <mm> --layer-idx <N> --layer-area <mm2>"
                     " [--display-area <mm2>] <config.json>\n";
        return 1;
    }

    // --- Parse JSON ---
    json j;
    {
        std::ifstream f(json_file);
        if (!f) {
            std::cerr << "Error: cannot open '" << json_file << "'\n";
            return 1;
        }
        try {
            j = json::parse(f);
        } catch (const json::parse_error& e) {
            std::cerr << "Error: JSON parse error: " << e.what() << "\n";
            return 1;
        }
    }

    // --- Construct SLATimeEstimateInput and run ---
    try {
        // display_area: CLI takes priority, then JSON field, then error
        if (display_area < 0) {
            if (j.contains("display_area"))
                display_area = j["display_area"].get<double>();
            else
                throw std::runtime_error(
                    "display_area is required but not found in JSON. "
                    "Add a 'display_area' field (mm^2) to the JSON or use --display-area <mm2>");
        }

        const json& ep_json = j.at("exposure_profile");

        SLATimeEstimateInput in;
        in.layer_height    = layer_height;
        in.layer_idx       = static_cast<size_t>(layer_idx);
        in.layer_area      = layer_area;
        in.exp_time        = j.at("expTime").get<double>();
        in.init_exp_time   = j.at("expTimeFirst").get<double>();
        in.fade_layers_cnt = j.at("numFade").get<int>();
        in.is_slx          = j.at("printerModel").get<std::string>() == "SLX";
        in.is_prusa_print  = true;
        in.display_area    = display_area;
        in.area_fill       = ep_json.at("area_fill").get<double>() * 0.01;
        in.below           = parse_exposure_profile(ep_json.at("below_area_fill"));
        in.above           = parse_exposure_profile(ep_json.at("above_area_fill"));
        // fast_tilt / slow_tilt / hv_tilt / material_print_speed are unused when is_prusa_print = true

        // --- Run ---
        const auto [layer_time, is_fast] = calculate_layer_time(in);

        std::cout << "layer_time    = " << layer_time << " s\n";
        std::cout << "is_fast_layer = " << (is_fast ? "true" : "false") << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
