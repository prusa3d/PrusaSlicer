#pragma once

#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"

#include <nlohmann/json_fwd.hpp>
#include <tl/expected.hpp>
#include <string>

namespace Slic3r::Biz::PhysicalPrinter {

// Serialization of a persisted physical printer. Only host-upload printers
// (PrinterUpload payload) are ever stored on disk; the synthetic filesystem /
// connect entries are recreated at runtime.
//
// The on-disk key names match the legacy ConfigBox schema
// (physical_printer_*), so files written by the previous implementation load
// without migration.

nlohmann::ordered_json printer_to_json(const PhysicalPrinterConfig& printer);

tl::expected<PhysicalPrinterConfig, std::string>
printer_from_json(const nlohmann::ordered_json& json);

} // namespace Slic3r::Biz::PhysicalPrinter
