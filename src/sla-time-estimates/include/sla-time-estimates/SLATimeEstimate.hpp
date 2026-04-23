#pragma once

#include <cstddef>
#include <utility>

namespace Slic3r {

// Exposure profile for one class of layers (fast/slow).
// All time values are in milliseconds, distances in nanometres, speeds in usteps/s.
struct ExposureProfile {
    int     delay_before_exposure_ms    { 0 };
    int     delay_after_exposure_ms     { 0 };
    int     tilt_down_offset_delay_ms   { 0 };
    int     tilt_down_delay_ms          { 0 };
    int     tilt_up_offset_delay_ms     { 0 };
    int     tilt_up_delay_ms            { 0 };
    int     tower_hop_height_nm         { 0 };
    int     tilt_down_offset_steps      { 0 };
    int     tilt_down_cycles            { 0 };
    int     tilt_up_offset_steps        { 0 };
    int     tilt_up_cycles              { 0 };
    bool    use_tilt                    { true };
    int     tower_speed                 { 0 };
    int     tilt_down_initial_speed     { 0 };
    int     tilt_down_finish_speed      { 0 };
    int     tilt_up_initial_speed       { 0 };
    int     tilt_up_finish_speed        { 0 };
    int     delay_to_reflood_ms         { 0 };
};

struct SLATimeEstimateInput {
    double           area_fill;           // material area_fill * 0.01
    double           fast_tilt;           // printer fast_tilt_time
    double           slow_tilt;           // printer slow_tilt_time
    double           hv_tilt;             // printer high_viscosity_tilt_time
    double           init_exp_time;       // material initial_exposure_time
    double           exp_time;            // material exposure_time
    int              fade_layers_cnt;     // object faded_layers
    bool             is_slx;
    bool             is_prusa_print;
    double           display_area;        // display_width * display_height (unscaled)
    int              material_print_speed; // cast from SLAMaterialSpeed: 0=Slow, 1=Fast, 2=HighViscosity
    ExposureProfile  below;              // exposure profile for bottom/fast layers
    ExposureProfile  above;              // exposure profile for top/slow layers
    size_t           layer_idx;
    double           layer_height;
    double           layer_area;         // unscaled !
};

// Returns (layer_time_s, is_fast_layer).
std::pair<double, bool> calculate_layer_time(const SLATimeEstimateInput& in);

} // namespace Slic3r
