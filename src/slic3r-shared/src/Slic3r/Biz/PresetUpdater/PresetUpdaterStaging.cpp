#include "Slic3r/Biz/PresetUpdater/PresetUpdaterStaging.hpp"

#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"

#include <boost/filesystem/directory.hpp>
#include <boost/filesystem/operations.hpp>

#include <algorithm>
#include <ctime>
#include <optional>

namespace fs = boost::filesystem;

namespace Slic3r::Biz::PresetUpdater {

namespace {

constexpr std::time_t k_max_staging_age_seconds = 2 * 24 * 60 * 60;

std::optional<std::time_t> newest_write_time(const fs::path& path)
{
    boost::system::error_code ec;
    std::time_t newest = fs::last_write_time(path, ec);
    if (ec) {
        return std::nullopt;
    }
    const bool is_directory = fs::is_directory(path, ec);
    if (ec) {
        return std::nullopt;
    }
    if (!is_directory) {
        return newest;
    }

    fs::recursive_directory_iterator it(path, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const std::time_t written = fs::last_write_time(it->path(), ec);
        if (ec) {
            return std::nullopt;
        }
        newest = std::max(newest, written);
    }
    if (ec) {
        return std::nullopt;
    }
    return newest;
}

void remove_logged(const fs::path& path)
{
    boost::system::error_code ec;
    fs::remove_all(path, ec);
    if (ec) {
        SPDLOG_ERROR("Failed to remove {}: {}", path.string(), ec.message());
    }
}

} // namespace

StagingArea::StagingArea() :
    m_root(Slic3r::updater_staging_dir() / fs::unique_path())
{}

StagingArea::~StagingArea()
{
    remove_logged(m_root);
}

fs::path StagingArea::vendor_path(
    const std::string& repo_id,
    const std::string& vendor_id,
    const std::string& version
) const
{
    return m_root / repo_id / vendor_id / version;
}

void sweep_stale_staging()
{
    const fs::path staging = Slic3r::updater_staging_dir();
    boost::system::error_code ec;
    if (!fs::is_directory(staging, ec) || ec) {
        return;
    }

    const std::time_t now = std::time(nullptr);
    fs::directory_iterator it(staging, ec);
    for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const fs::path entry = it->path();
        const std::optional<std::time_t> newest = newest_write_time(entry);
        if (!newest || now - *newest < k_max_staging_age_seconds) {
            continue;
        }
        SPDLOG_INFO("Dropping abandoned preset download {}", entry.string());
        remove_logged(entry);
    }
    if (ec) {
        SPDLOG_ERROR("Failed to list {}: {}", staging.string(), ec.message());
    }
}

} // namespace Slic3r::Biz::PresetUpdater
