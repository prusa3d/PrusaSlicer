
#include "Slic3r/Biz/libpgcode/PostProcessorConfig.hpp"

namespace Slic3r::Biz::libpgcode {

void TimeMachineData::reset()
{
    enabled = false;
    time = 0.0f;
    first_layer_time = 0.0f;
    line_m73_main_mask.clear();
    line_m73_stop_mask.clear();
    g1_times_cache.clear();
    stop_times.clear();
}

void PostProcessorConfig::reset()
{
    export_remaining_time_enabled = false;
    do_M104_backtrace = false;
    std::for_each(time_machines.begin(), time_machines.end(), [](TimeMachineData& m) { m.reset(); });
    extruder_temps_config.clear();
    extruder_temps_first_layer_config.clear();
}

} // namespace Slic3r::Biz::libpgcode
