#include "Slic3r/Domain/ConfigBoxesFDM.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"

namespace Slic3r::Domain {

PrintSettings::PrintSettings() : ConfigBox(get_defs_fdm(), FDMConfigLocation::Print) {}
FilamentSettings::FilamentSettings() : ConfigBox(get_defs_fdm(), FDMConfigLocation::Filament) {}
PrinterSettings::PrinterSettings() : ConfigBox(get_defs_fdm(), FDMConfigLocation::Printer) {}
ToolPrintSettings::ToolPrintSettings() : ConfigBox(get_defs_fdm(), FDMConfigLocation::Tool) {}
ObjectSettings::ObjectSettings() : ConfigBox(get_defs_fdm(), FDMConfigLocation::Object) {}
VolumeSettings::VolumeSettings() : ConfigBox(get_defs_fdm(), FDMConfigLocation::Volume) {}
ProjectSettings::ProjectSettings() : ConfigBox(get_defs_fdm(), FDMConfigLocation::Project) {}


} // namespace Slic3r::Domain
