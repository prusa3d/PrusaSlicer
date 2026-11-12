#pragma once

#include <Slic3r/Biz/PrintHost/IPrintHost.hpp>
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"
#include "Slic3r/Biz/PrintHost/PrintHostJobData.hpp"

namespace Slic3r::Biz::PrintHost {  

std::unique_ptr<IPrintHost> create_print_host(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data);  

} // namespace Slic3r::Biz::PrintHost