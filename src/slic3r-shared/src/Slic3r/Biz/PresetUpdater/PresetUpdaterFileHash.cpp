#include "PresetUpdaterFileHash.hpp"

#include <iomanip>

#include "Slic3r/Biz/SHA256.hpp"

#include "Slic3r/Exception.hpp"
#include "Slic3r/Log.hpp"

#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/system/error_code.hpp>
#include <boost/nowide/fstream.hpp>
#include <fmt/format.h>

namespace fs = boost::filesystem;

namespace Slic3r::Biz::PresetUpdater {

PresetUpdaterFileHash::PresetUpdaterFileHash(const std::string hash)
{
    std::stringstream ss_upper, ss_lower;

    ss_upper << std::hex << std::uppercase << std::setfill('0');
    ss_lower << std::hex << std::setfill('0'); // Lowercase is the default for std::hex

    for (const unsigned char c : hash) {
        const auto val = static_cast<int>(c);
        ss_upper << std::setw(2) << val;
        ss_lower << std::setw(2) << val;
    }

    m_upper_case = ss_upper.str();
    m_lower_case = ss_lower.str();
}

bool PresetUpdaterFileHash::compare(const std::string& hash_string) const
{
    return m_lower_case == hash_string || m_upper_case == hash_string;
}

PresetUpdaterFileHash file_hash(const fs::path& path, PresetUpdaterProcessStatus* process_status)
{
    boost::nowide::ifstream file(path.string(), std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        std::string msg = fmt::format(
            "Failed to open file {} when calculating file hash.",
            path.string()
        );
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::DataUnreadable);
        return PresetUpdaterFileHash({});
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    std::string hash;
    try {
        hash = sha256(content);
    } catch (const std::exception& e) {
        std::string msg = fmt::format(
            "Failed to calculate file hash of {}: {}",
            path.string(),
            e.what()
        );
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::DataCorrupted);
        return PresetUpdaterFileHash({});
    }

    return PresetUpdaterFileHash(hash);
}
} // namespace Slic3r::Biz::PresetUpdater
