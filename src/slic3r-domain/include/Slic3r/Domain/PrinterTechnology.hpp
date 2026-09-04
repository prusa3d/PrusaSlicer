#pragma once

#include <cstdint>

namespace Slic3r::Domain {

enum class PrinterTechnology : uint8_t
{
    FFF = 0,
    SLA = 1
};

}
