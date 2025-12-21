///|/ Copyright (c) Prusa Research 2024
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_SLMPrintConfig_hpp_
#define slic3r_SLMPrintConfig_hpp_

// Must include PrintConfig.hpp to get the PRINT_CONFIG_CLASS_DEFINE macro
#include "PrintConfig.hpp"

namespace Slic3r {

// Forward declarations
class SLMPrintConfig;
class SLMPrintObjectConfig;
class SLMPrinterConfig;

// Enums for SLM configuration options
// These are declared here but the static maps are defined in PrintConfig.cpp
enum class SLMHatchPatternType : int {
    slmhpGrid,
    slmhpStripe,
    slmhpConcentric,
    slmhpCustom,
};

enum class SLMScanModeType : int {
    slmsmContourFirst,
    slmsmHatchFirst,
};

enum class SLMExportFormatType : int {
    slmefSLM,      // SLM Solutions (.slm)
    slmefMTT,      // Renishaw (.mtt)
    slmefSLI,      // EOS (.sli)
    slmefCLI,      // Common Layer Interface (.cli)
    slmefREA,      // DMG Mori Realizer (.rea)
};

// SLMPrintConfig class is now defined in PrintConfig.hpp
// (moved there to avoid macro #undef issues)

} // namespace Slic3r

#endif /* slic3r_SLMPrintConfig_hpp_ */

