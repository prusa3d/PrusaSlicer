#include "libslic3r/ConfigUtils.hpp"
#include <boost/algorithm/string/predicate.hpp>
#include "Slic3r/Domain/GCodeFlavor.hpp"

namespace Slic3r {
std::string get_extrusion_axis(const PrintConfigView& cfg)
{
    using Domain::GCodeFlavor;
    return ((cfg.get<GCodeFlavor>("gcode_flavor") == GCodeFlavor::gcfMach3)
            || (cfg.get<GCodeFlavor>("gcode_flavor") == GCodeFlavor::gcfMachinekit)) ?
        "A" :
        (cfg.get<GCodeFlavor>("gcode_flavor") == GCodeFlavor::gcfNoExtrusion) ? "" :
                                                                                "E";
}
} // namespace Slic3r
