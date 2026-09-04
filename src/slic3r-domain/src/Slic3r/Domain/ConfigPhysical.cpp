#include "Slic3r/Domain/ConfigPhysical.hpp"

namespace Slic3r::Domain {

std::string print_host_type_to_string(PrintHostType type) {
    switch (type) {
        case PrusaLink:         return "PrusaLink";
        case SL1Host:           return "SL1Host";
        case OctoPrint:         return "OctoPrint";
        case Moonraker:         return "Moonraker";
        case Duet:              return "Duet";
        case FlashAir:          return "FlashAir";
        case AstroBox:          return "AstroBox";
        case Repetier:          return "Repetier";
        case MKS:               return "MKS";
        default:                return "Unknown";
    }
}

} // namespace Slic3r::Domain
