#pragma once

#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"

#include <string>
#include <map>

namespace Slic3r::Biz::PhysicalPrinter {

using UuidPrinterMap = std::map<std::string, PhysicalPrinterConfig>;

/// Persists host-upload physical printers as one JSON file per uuid under {data_dir}/physical_printer.
class PhysicalPrinterStorage
{
public:
    PhysicalPrinterStorage();
    ~PhysicalPrinterStorage();

    void remove_one(const std::string& uuid);

    const UuidPrinterMap& all_printers() const;

    const Biz::PhysicalPrinter::PhysicalPrinterConfig& save_or_create(
        const Biz::PhysicalPrinter::PhysicalPrinterConfig& printer
    );

private:
    void load_all();
    void save_all();
    void save_one(const std::string& uuid);
    /// Deletes stored files whose uuid is no longer held in memory.
    void consolidate_files();

private:
    UuidPrinterMap m_map;
};

} // namespace Slic3r::Biz::PhysicalPrinter
