#pragma once

#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"
namespace Slic3r::Biz::UserAccount::ConnectUtils {

bool config_from_json(const std::string& json, PhysicalPrinter::PhysicalPrinterConfig& config, std::string& filename, std::string& body_json);

std::string filename_from_json(const std::string& json_str); 

} //namespace Slic3r::Biz::UserAccount