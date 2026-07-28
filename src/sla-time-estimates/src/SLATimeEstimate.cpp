///|/ Copyright (c) Prusa Research 2019 - 2023 Tomáš Mészáros @tamasmeszaros, Oleksandra Iushchenko @YuSanka, Pavel Mikuš @Godrak, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "sla-time-estimates/SLATimeEstimate.hpp"

#include <algorithm>
#include <string>

namespace Slic3r {

// Values must stay in sync with SLAMaterialSpeed in PrintConfig.hpp.
static const int kSpeedSlow          = 0; // slamsSlow
static const int kSpeedFast          = 1; // slamsFast
static const int kSpeedHighViscosity = 2; // slamsHighViscosity

// Constant values from FW
static int tiltHeight_sl1          = 4959;  // ustep
static int tiltHeight_slx          = 12096; // ustep
static int tower_microstep_size_nm = 1250;  // nm
static int first_extra_slow_layers = 3;

static int Ms(int s)
{
    return s;
}

static int nm_to_tower_microsteps(int nm)
{
    return nm / tower_microstep_size_nm;
}

static int count_move_time(const std::string& axis_name, double length, int steprate)
{
    if (length <= 0 || steprate <= 0)
        return 0;
    // sla - fw checks every 0.1 s if axis is still moving. See: Axis._wait_to_stop_delay. Additional 0.021 s is
    // measured average delay of the system. Thus, the axis movement time is always quantized by this value.
    double delay = 0.121;

    // Both axes use linear ramp movements. This factor compensates the tilt acceleration and deceleration time.
    double tilt_comp_factor = 0.1;

    // Both axes use linear ramp movements. This factor compensates the tower acceleration and deceleration time.
    int tower_comp_factor = 20000;

    int l = int(length);
    return axis_name == "tower" ? Ms((int(l / (steprate * delay) + (steprate + l) / tower_comp_factor) + 1) * (delay * 1000)) :
                                  Ms((int(l / (steprate * delay) + tilt_comp_factor) + 1) * (delay * 1000));
}

static int layer_peel_move_time(int layer_height_nm, const ExposureProfile& p, bool is_slx)
{
    int profile_change_delay = Ms(20);  // propagation delay of sending profile change command to MC
    int sleep_delay = Ms(2);            // average delay of the Linux system sleep function
    int tiltHeight = is_slx ? tiltHeight_slx : tiltHeight_sl1;

    int tilt = Ms(0);
    if (p.use_tilt) {
        tilt += profile_change_delay;
        // initial down movement
        tilt += count_move_time(
            "tilt",
            p.tilt_down_offset_steps,
            p.tilt_down_initial_speed);
        // initial down delay (old printers only)
        if (!is_slx)
            tilt += p.tilt_down_offset_delay_ms + sleep_delay;
        // profile change delay if down finish profile is different from down initial
        tilt += profile_change_delay;
        // cycle down movement
        if (is_slx)
            tilt += count_move_time("tilt", tiltHeight - p.tilt_down_offset_steps, p.tilt_down_finish_speed);
        else {
            tilt += p.tilt_down_cycles * count_move_time(
                "tilt",
                int((tiltHeight - p.tilt_down_offset_steps) / p.tilt_down_cycles),
                p.tilt_down_finish_speed);
            // cycle down delay (old printers only)
            tilt += p.tilt_down_cycles * (p.tilt_down_delay_ms + sleep_delay);
        }

        // profile change delay if up initial profile is different from down finish
        tilt += profile_change_delay;
        // initial up movement
        tilt += count_move_time(
            "tilt",
            tiltHeight - p.tilt_up_offset_steps,
            p.tilt_up_initial_speed);
        // initial up delay (old printers only)
        if (!is_slx)
            tilt += p.tilt_up_offset_delay_ms + sleep_delay;
        // profile change delay if up initial profile is different from down finish
        tilt += profile_change_delay;
        // finish up movement
        if (is_slx)
            tilt += count_move_time("tilt", p.tilt_up_offset_steps, p.tilt_up_finish_speed);
        else {
            tilt += p.tilt_up_cycles * count_move_time(
                "tilt",
                int(p.tilt_up_offset_steps / p.tilt_up_cycles),
                p.tilt_up_finish_speed);
            // cycle up delay (old printers only)
            tilt += p.tilt_up_cycles * (p.tilt_up_delay_ms + sleep_delay);
        }
    }

    // delay to reflood resin in the vat (new printers only)
    if (is_slx && p.delay_to_reflood_ms)
        tilt += p.delay_to_reflood_ms + sleep_delay;

    int tower = Ms(0);
    if (p.tower_hop_height_nm > 0) {
        tower += count_move_time(
            "tower",
            nm_to_tower_microsteps(int(p.tower_hop_height_nm) + layer_height_nm),
            p.tower_speed);
        tower += count_move_time(
            "tower",
            nm_to_tower_microsteps(int(p.tower_hop_height_nm)),
            p.tower_speed);
        tower += profile_change_delay;
    }
    else {
        tower += count_move_time(
            "tower",
            nm_to_tower_microsteps(layer_height_nm),
            p.tower_speed);
        tower += profile_change_delay;
    }
    return int(tilt + tower);
}

// Returns pair of (layer_time, is_fast_layer)
std::pair<double, bool> calculate_layer_time(const SLATimeEstimateInput& in)
{
    const int    first_slow_layers = in.fade_layers_cnt + first_extra_slow_layers;
    const double delta_fade_time   = (in.init_exp_time - in.exp_time) / (in.fade_layers_cnt + 1);

    // Calculation of the printing time
    // + Calculation of the slow and fast layers to the future controlling those values on FW
    double layer_times = 0.0;
    bool is_fast_layer = false;

    if (in.is_prusa_print) {
        // Enforce slow layers for the first layers. Other layers obey area fill
        is_fast_layer = int(in.layer_idx) >= first_slow_layers && in.layer_area <= in.display_area * in.area_fill;
        const int l_height_nm = 1000000 * in.layer_height;

        const int exposure_delay = in.is_slx ? 0 : (is_fast_layer ? in.below : in.above).delay_after_exposure_ms;

        layer_times = layer_peel_move_time(l_height_nm, is_fast_layer ? in.below : in.above, in.is_slx) +
                            (is_fast_layer ? in.below : in.above).delay_before_exposure_ms +
                            exposure_delay +
                            124;   // Magical constant to compensate remaining computation delay in exposure thread

        layer_times *= 0.001; // All before calculations are made in ms, but we need it in s
    }
    else {
        is_fast_layer = in.layer_area <= in.display_area * in.area_fill;
        const double tilt_time = in.material_print_speed == kSpeedSlow           ? in.slow_tilt :
                                 in.material_print_speed == kSpeedHighViscosity   ? in.hv_tilt   :
                                 is_fast_layer ? in.fast_tilt : in.slow_tilt;

        layer_times += tilt_time;

        //// Per layer times (magical constants calculated from FW)
        static double exposure_safe_delay_before{ 3.0 };
        static double exposure_high_viscosity_delay_before{ 3.5 };
        static double exposure_slow_move_delay_before{ 1.0 };

        if (in.material_print_speed == kSpeedSlow)
            layer_times += exposure_safe_delay_before;
        else if (in.material_print_speed == kSpeedHighViscosity)
            layer_times += exposure_high_viscosity_delay_before;
        else if (!is_fast_layer)
            layer_times += exposure_slow_move_delay_before;

        // Increase layer time for "magic constants" from FW
        layer_times += (
            in.layer_height * 5  // tower move
            + 120 / 1000  // Magical constant to compensate remaining computation delay in exposure thread
        );
    }

    // We are done with tilt time, but we haven't added the exposure time yet.
    layer_times += std::max(in.exp_time, in.init_exp_time - in.layer_idx * delta_fade_time);

    return std::make_pair(layer_times, is_fast_layer);
}

} // namespace Slic3r
