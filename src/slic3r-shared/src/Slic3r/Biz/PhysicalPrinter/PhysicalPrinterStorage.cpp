#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterStorage.hpp"

#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterJson.hpp"
#include "Slic3r/Domain/ConfigPhysical.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"

#include <nlohmann/json.hpp>
#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>
#include <boost/nowide/fstream.hpp>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace fs = boost::filesystem;

namespace Slic3r::Biz::PhysicalPrinter {

namespace {
inline fs::path storage_dir()
{
    static const fs::path dir = fs::path{data_dir()} / "physical_printer";
    return dir;
}

PhysicalPrinterConfig default_new_printer()
{
    PrinterUpload up;
    up.type      = Domain::PrusaLink;
    up.auth_type = Domain::PrintHostAuthType::ApiKey;

    PhysicalPrinterConfig printer;
    printer.payload = std::move(up);
    return printer;
}
} // namespace

PhysicalPrinterStorage::PhysicalPrinterStorage() :
    m_dummy(default_new_printer())
{
    load_all();
}

PhysicalPrinterStorage::~PhysicalPrinterStorage()
{
}

void PhysicalPrinterStorage::load_all()
{
    const fs::path dir_path = storage_dir();
    boost::system::error_code ec;
    fs::directory_iterator it(dir_path, ec);

    if (ec) {
        SPDLOG_ERROR("Failed to load Physical Printer Configurations: {}", ec.message());
        return;
    }

    for (const auto& entry : it) {
        boost::nowide::ifstream file(entry.path().string());
        if (!file.is_open()) continue;

        nlohmann::ordered_json json;
        try {
            file >> json;
        } catch (...) {
            SPDLOG_ERROR("Failed to read physical printer from json file: {}", entry.path().string());
            continue;
        }

        auto printer = printer_from_json(json);
        if (!printer) {
            SPDLOG_ERROR("Failed to parse physical printer {}: {}", entry.path().string(), printer.error());
            continue;
        }
        if (printer->uuid.empty()) {
            SPDLOG_ERROR("Physical printer configuration was not loaded due missing uuid: {}", entry.path().string());
            continue;
        }

        const std::string uuid = printer->uuid;
        m_map[uuid] = std::move(printer.value());
    }

    consolidate_files();
}

void PhysicalPrinterStorage::consolidate_files()
{
    const fs::path dir_path = storage_dir();
    boost::system::error_code ec;
    fs::directory_iterator it(dir_path, ec);

    if (ec) {
        SPDLOG_ERROR("Failed to consolidate Physical Printer Configurations: {}", ec.message());
        return;
    }

    bool needs_save = false;
    for (const auto& entry : it) {
        std::string current_stem = entry.path().stem().string();
        if (m_map.find(current_stem) == m_map.end()) {
            fs::remove(entry.path(), ec);
            needs_save = true;
        }
    }
    if (needs_save) {
        save_all();
    }
}

void PhysicalPrinterStorage::save_one(const std::string& uuid)
{
    auto it = m_map.find(uuid);
    if (it == m_map.end()) {
        SPDLOG_ERROR("Attempted to save non-existent physical printer uuid: {}", uuid);
        return;
    }

    const fs::path dir_path = storage_dir();
    boost::system::error_code ec;
    fs::create_directories(dir_path, ec);
    if (ec) {
        SPDLOG_ERROR("Failed to create directory for Physical Printers: {}", ec.message());
        return;
    }

    fs::path full_path = dir_path / (uuid + ".json");
    boost::nowide::ofstream out(full_path.string());

    if (!out.is_open()) {
        SPDLOG_ERROR("Failed to open file for writing: {}", full_path.string());
        return;
    }
    try {
        nlohmann::ordered_json json = printer_to_json(it->second);
        out << json.dump(2);
    } catch (...) {
        SPDLOG_ERROR("Failed to write physical printer in json file: {}", full_path.string());
    }
}

void PhysicalPrinterStorage::remove_one(const std::string& uuid)
{
    auto it = m_map.find(uuid);
    if (it == m_map.end()) {
        SPDLOG_ERROR("Attempted to remove non-existent physical printer uuid: {}", uuid);
        return;
    }

    m_map.erase(it);

    const fs::path file_path = storage_dir() / (uuid + ".json");
    boost::system::error_code ec;

    fs::remove(file_path, ec);
    if (ec) {
        SPDLOG_ERROR("Failed to delete physical printer file {}: {}", file_path.string(), ec.message());
    }
}

void PhysicalPrinterStorage::save_all()
{
    for (const auto& [uuid, printer] : m_map) {
        save_one(uuid);
    }
}

const UuidPrinterMap& PhysicalPrinterStorage::all_printers() const
{
    return m_map;
}

UuidPrinterMap& PhysicalPrinterStorage::all_printers()
{
    return m_map;
}

PhysicalPrinterConfig& PhysicalPrinterStorage::dummy()
{
    return m_dummy;
}

const PhysicalPrinterConfig& PhysicalPrinterStorage::dummy() const
{
    return m_dummy;
}

std::string PhysicalPrinterStorage::create_from_dummy(const Domain::Preset::HwPrinterConfig& hw_config)
{
    std::string uuid{boost::uuids::to_string(boost::uuids::random_generator()())};
    m_dummy.uuid      = uuid;
    m_dummy.hw_config = hw_config;
    m_map[uuid]       = std::move(m_dummy);
    m_dummy           = default_new_printer();
    save_one(uuid);
    return uuid;
}

} //namespace Slic3r::Biz::PhysicalPrinter
