#pragma once

#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"

#include <string>
#include <map>

namespace Slic3r::Domain::Preset {
struct HwPrinterConfig;
} // namespace Slic3r::Domain::Preset

namespace Slic3r::Biz::PhysicalPrinter {

using UuidPrinterMap = std::map<std::string, PhysicalPrinterConfig>;

/// Persists host-upload physical printers as one JSON file per uuid under {data_dir}/physical_printer.
class PhysicalPrinterStorage
{
public:
    PhysicalPrinterStorage();
    ~PhysicalPrinterStorage();

    void load_all();
    void save_all();
    void save_one(const std::string& uuid);
    void remove_one(const std::string& uuid);

    const UuidPrinterMap& all_printers() const;
    UuidPrinterMap& all_printers();

    /// Scratch printer edited before a new one is persisted.
    PhysicalPrinterConfig& dummy();
    const PhysicalPrinterConfig& dummy() const;

    /// Persists the dummy under a fresh uuid with @p hw_config, resets it, and returns the uuid.
    std::string create_from_dummy(const Domain::Preset::HwPrinterConfig& hw_config);

private:
    /// Deletes stored files whose uuid is no longer held in memory.
    void consolidate_files();

private:
    UuidPrinterMap m_map;
    PhysicalPrinterConfig m_dummy;
};

} // namespace Slic3r::Biz::PhysicalPrinter
