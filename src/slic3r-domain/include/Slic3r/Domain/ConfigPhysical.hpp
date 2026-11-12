#pragma once

#include <string>

namespace Slic3r::Domain {

// Physical printer enums. The physical printer is modelled as a plain struct
// (Biz::PhysicalPrinter::PhysicalPrinterConfig) rather than through the ConfigBox
// infrastructure; only these shared enums live here.

enum class PrintHostAuthType {
    None,
    ApiKey,
    Digest
};

enum PrintHostType {
    PrusaLink,
    PrusaLinkStorage,
    SL1Host,
    OctoPrint,
    Moonraker,
    Duet,
    FlashAir,
    AstroBox,
    Repetier,
    MKS
};

std::string print_host_type_to_string(PrintHostType type);

} // namespace Slic3r::Domain
