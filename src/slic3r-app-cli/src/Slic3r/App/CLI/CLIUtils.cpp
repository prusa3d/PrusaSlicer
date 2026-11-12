#include "Slic3r/Domain/ConfigPack.hpp"

using Slic3r::Domain::ConfigPack;
using Slic3r::Domain::ConfigPackFDM;
using Slic3r::Domain::ConfigPackSLA;

namespace Slic3r::App::CLI {

static double min_object_distance([[maybe_unused]] const ConfigPackSLA& config_pack)
{
    return 6.;
}

static double min_object_distance(const ConfigPackFDM& config_pack)
{
    const double extruder_clearance_radius =
        config_pack.printer.items.opt("extruder_clearance_radius").get<double>();
    const double duplicate_distance =
        6.; // TODO: duplicate_distance was removed in the new configs.
    const bool complete_objects = config_pack.print.items.opt("complete_objects").get<bool>();

    // min object distance is max(duplicate_distance, clearance_radius)
    return (complete_objects && extruder_clearance_radius > duplicate_distance) ?
        extruder_clearance_radius :
        duplicate_distance;
}

double min_object_distance(const ConfigPack& config_pack)
{
    if (std::holds_alternative<ConfigPackFDM>(config_pack)) {
        return min_object_distance(std::get<ConfigPackFDM>(config_pack));
    } else if (std::holds_alternative<ConfigPackSLA>(config_pack)) {
        return min_object_distance(std::get<ConfigPackSLA>(config_pack));
    } else {
        PANIC("Unexpected config type!");
    }
}

} // namespace Slic3r::App::CLI
