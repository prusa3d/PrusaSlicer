#pragma once
#include <vector>

namespace Slic3r::Domain::SLA {

/**
@brief Statistic data about slicing
Estimated print time, material consumed, etc.
*/
struct PrintStatistics
{
    double estimated_print_time = 0.;
    double estimated_print_time_tolerance = 0.;
    double objects_used_material = 0.;
    double support_used_material = 0.;
    size_t slow_layers_count = 0;
    size_t fast_layers_count = 0;
    double total_cost = 0.;
    double total_weight = 0.;
    std::vector<double> layers_times_running_total;
    std::vector<double> layers_areas;

    int count_faded_layers = 0;
    bool hollowing_enable = false; // exist object with enabled hollowing
};

} // namespace Slic3r::Domain::SLA
