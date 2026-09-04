#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"

namespace Slic3r::Domain {

SLAPrintSettings::SLAPrintSettings() : ConfigBox(get_defs_sla(), SLAConfigLocation::Print) {}

SLAMaterialSettings::SLAMaterialSettings() : ConfigBox(get_defs_sla(), SLAConfigLocation::Material)
{}

SLAPrinterSettings::SLAPrinterSettings() : ConfigBox(get_defs_sla(), SLAConfigLocation::Printer) {}

SLAObjectSettings::SLAObjectSettings() : ConfigBox(get_defs_sla(), SLAConfigLocation::Object) {}

} // namespace Slic3r::Domain
