#include "PresetUpdaterRepositorySync.hpp"

#include "PresetUpdaterProcessStatus.hpp"
#include "PresetUpdaterRepository.hpp"
#include "PresetUpdaterIndex.hpp"
#include "PresetUpdaterFileHash.hpp"
#include "PresetUpdaterUtils.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Biz/Preset/IO/HwConfigLoader.hpp"

#include "Slic3r/Exception.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Directories.hpp"

#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp"

#include <fmt/format.h>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/directory.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>

namespace fs = boost::filesystem;

namespace Slic3r::Biz::PresetUpdater {

using Algorithms::open_zip_reader;
using Algorithms::close_zip_reader;

constexpr size_t BATCH_DOWNLOAD_TRESHOLD = 20;

namespace {
fs::path create_temp_dir()
{
    boost::uuids::random_generator generator;
    const std::string dirname = boost::uuids::to_string(generator());
    fs::path temp_dir         = Slic3r::temp_dir() / dirname;

    boost::system::error_code ec;

    if (fs::exists(temp_dir, ec) && !ec && fs::is_directory(temp_dir, ec) && !ec) {
        SPDLOG_ERROR(
            "Temp directory {} already exists.",
            temp_dir.string()
        ); // Something is off. This should never happen.
        throw Slic3r::RuntimeError(
            "Failed to create temp directory " + temp_dir.string() + ". " + ec.message()
        );
    }

    if (!fs::create_directory(temp_dir, ec)) {
        throw Slic3r::RuntimeError(
            "Failed to create temp directory " + temp_dir.string() + ". " + ec.message()
        );
    }
    return temp_dir;
}

void warn_about_repo(
    PresetUpdaterProcessStatus* process_status,
    const std::string& repo_id,
    const std::string& msg)
{
    process_status->set_warning_target(repo_id);
    process_status->set_warning(msg, PresetUpdaterReason::SourceUnreachable);
    process_status->clear_warning_target();
}

void remove_file(
    const fs::path& path,
    PresetUpdaterProcessStatus* process_status,
    const char* func_name)
{
    boost::system::error_code ec;
    bool failed = (!fs::remove(path, ec) && ec);
    
    if (failed) {
        process_status->set_warning(fmt::format("{}: Failed to remove file {}: {}", func_name, path.string(), ec.message()), PresetUpdaterReason::LocalStorageFailed);
    }
}

void remove_directory(
    const fs::path& path,
    PresetUpdaterProcessStatus* process_status,
    const char* func_name)
{
    boost::system::error_code ec;
    bool failed = (!fs::remove_all(path, ec) && ec);
    
    if (failed) {
        process_status->set_warning(fmt::format("{}: Failed to remove directory {}: {}", func_name, path.string(), ec.message()), PresetUpdaterReason::LocalStorageFailed);
    }
}

bool prepare_temp_vendor_dir(
    const boost::filesystem::path& temp_vendor_dir_path,
    const std::string& vendor_name,
    PresetUpdaterProcessStatus* process_status,
    const char* func_name)
{
    boost::system::error_code ec;
    if (boost::filesystem::create_directories(temp_vendor_dir_path, ec) || !ec) {
        for (boost::filesystem::directory_iterator it(temp_vendor_dir_path); it != boost::filesystem::directory_iterator(); ++it) {
            remove_directory(it->path(), process_status, func_name);
        }
        return true;
    }

    std::string msg = fmt::format(
        "Failed to create target directory {} for vendor {}: {}. Staging update has failed.",
        temp_vendor_dir_path.string(), vendor_name, ec.message());
    SPDLOG_ERROR(msg);
    process_status->set_warning(msg, PresetUpdaterReason::LocalStorageFailed);
    return false;
}

bool ensure_vendor_dir_exists(
    const boost::filesystem::path& target_dir,
    const std::string& vendor_name,
    PresetUpdaterProcessStatus* process_status,
    const char* func_name)
{
    boost::system::error_code ec;
    if (!boost::filesystem::create_directories(target_dir, ec) && ec) {
        std::string msg = fmt::format(
            "{}: Failed to create target directory {} for vendor {}: {}. Staging update has failed.",
            func_name, target_dir.string(), vendor_name, ec.message());
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::LocalStorageFailed);
        return false;
    }
    return true;
}

bool is_vendor_installed(const std::string& vendor_id, const std::string& repo_id)
{
    const fs::path vendor_folder_path    = local_vendor_path(repo_id, vendor_id);
    boost::system::error_code ec;

    if (!fs::exists(vendor_folder_path, ec) || ec) {
        return false;
    }
    if (!fs::is_directory(vendor_folder_path, ec) || ec) {
        return false;
    }
    // lets take presence of vendor.yaml file as proof of installation
    fs::path vendor_index_path = vendor_folder_path / "vendor.yaml";
    if (!fs::exists(vendor_index_path, ec) || ec) {
        return false;
    }
    return true;
}

bool check_resouces_vendor_sanity(
    const boost::filesystem::path& source_dir,
    const AbstractPresetUpdaterRepository* repo,
    PresetUpdaterProcessStatus* process_status,
    const PresetUpdaterIndex& source_index
)
{
    const fs::path source_vendor_dir  = source_dir / source_index.vendor();
    const fs::path source_vendor_yaml = source_vendor_dir / "vendor.yaml";

    const PresetUpdaterIndex::const_iterator recommended = source_index.recommended();
    PresetUpdaterIndex update_sync_index;
    if (recommended == source_index.end()) {
        process_status->set_warning(
            fmt::format(
                "No recommended version for vendor: {}, Index file might be corrupted.",
                source_index.vendor()
            )
        , PresetUpdaterReason::NoUsableVersion);
        return false;
    }

    Domain::Preset::VendorData source_vendor_data;
    try {
        Preset::IO::HwConfigLoader hw_config_loader;
        source_vendor_data = hw_config_loader.load(source_vendor_yaml.string());
        Semver version = Semver(source_vendor_data.info.version);
        if (version != recommended->config_version) {
            process_status->set_warning(
                fmt::format("Vendor data in resources failed sanity check. Its version is not recommended by its index. Vendor: {}. This installation of app is probably corrupted.", source_index.vendor())
            , PresetUpdaterReason::DataInconsistent);
            return false;
        }
    } catch (const std::exception& e) {
        process_status->set_warning(
            fmt::format("Failed to load vendor file {}: {}. This installation of app is probably corrupted.", source_vendor_yaml.string(), e.what())
        , PresetUpdaterReason::DataInconsistent);
        return false;
    }

    return true;
}

class ScopedZipReader {
public:
    ScopedZipReader() {
        mz_zip_zero_struct(&archive_);
    }

    ~ScopedZipReader() {
        if (opened_) {
            close_zip_reader(&archive_);
        }
    }

    bool open(const std::string& path) {
        opened_ = open_zip_reader(&archive_, path);
        return opened_;
    }

    mz_zip_archive* get() { return &archive_; }

    ScopedZipReader(const ScopedZipReader&) = delete;
    ScopedZipReader& operator=(const ScopedZipReader&) = delete;
    ScopedZipReader(ScopedZipReader&&) = delete;
    ScopedZipReader& operator=(ScopedZipReader&&) = delete;

private:
    mz_zip_archive archive_;
    bool opened_{false};
};

bool unzip_files_from_bundle_zip(
    const boost::filesystem::path& zip_file,
    const boost::filesystem::path& dest_dir,
    const AbstractPresetUpdaterRepository* repo,
    PresetUpdaterProcessStatus* process_status,
    const PresetUpdaterIndex& source_index
)
{
    ScopedZipReader archive;

    if (!archive.open(zip_file.string())) {
        std::string msg = fmt::format("Couldn't open zipped bundle: {}", zip_file.string());
        SPDLOG_ERROR(msg);
        if (process_status) process_status->set_warning(msg, PresetUpdaterReason::DataCorrupted);
        return false;
    }

    mz_uint num_entries = mz_zip_reader_get_num_files(archive.get());
    mz_zip_archive_file_stat stat;
    bool success = true;

    for (mz_uint i = 0; i < num_entries; ++i) {
        if (process_status && process_status->get_canceled()) {
            success = false;
            break;
        }

        if (!mz_zip_reader_file_stat(archive.get(), i, &stat)) {
            continue;
        }

        if (mz_zip_reader_is_file_a_directory(archive.get(), i)) {
            continue;
        }

        fs::path target_path = dest_dir / stat.m_filename;
        boost::system::error_code ec;

        if (!fs::create_directories(target_path.parent_path(), ec) && ec) {
            std::string msg = fmt::format("Failed to create directory {}: {}", target_path.parent_path().string(), ec.message());
            SPDLOG_ERROR(msg);
            if (process_status) process_status->set_warning(msg, PresetUpdaterReason::LocalStorageFailed);
            success = false;
            continue;
        }

        if (stat.m_uncomp_size > 0) {
            auto buffer = std::make_unique_for_overwrite<char[]>((size_t)stat.m_uncomp_size);
            if (!mz_zip_reader_extract_to_mem(archive.get(), stat.m_file_index, buffer.get(), (size_t)stat.m_uncomp_size, 0)) {
                std::string msg = fmt::format("Failed to unzip {}", stat.m_filename);
                SPDLOG_ERROR(msg);
                if (process_status) process_status->set_warning(msg, PresetUpdaterReason::DataCorrupted);
                success = false;
                continue;
            }

            boost::nowide::fstream file(target_path, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!file) {
                std::string msg = fmt::format("Failed to open file for writing: {}", target_path.string());
                SPDLOG_ERROR(msg);
                if (process_status) process_status->set_warning(msg, PresetUpdaterReason::LocalStorageFailed);
                success = false;
                continue;
            }

            file.write(buffer.get(), (std::streamsize)stat.m_uncomp_size);
            if (!file) {
                std::string msg = fmt::format("Failed to write data to: {}", target_path.string());
                SPDLOG_ERROR(msg);
                if (process_status) process_status->set_warning(msg, PresetUpdaterReason::LocalStorageFailed);
                success = false;
                continue;
            }
        }
    }

    return success;
}

bool move_file(
    const fs::path& source,
    const fs::path& dest,
    PresetUpdaterProcessStatus* process_status,
    const char* func_name)
{
    auto result = safe_move(source, dest);
    if (!result) {
        std::string msg = fmt::format("{}: Failed to move file {}: {}", func_name, source.string(), result.error());
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::LocalStorageFailed);
        return false;
    }
    return true;
}

bool download_and_stage_vendor_files(
    const std::map<std::string, std::string>& files_to_download,
    const AbstractPresetUpdaterRepository* repo,
    const PresetUpdaterIndex& index,
    const PresetUpdaterIndex::const_iterator& recommended,
    const fs::path& temp_vendor_dir_path,
    const fs::path& temp_path,
    const fs::path& update_sync_vendor_dir_path,
    PresetUpdaterProcessStatus* process_status)
{
    boost::system::error_code ec;

    // Download whole version zip or partial files
    // Whole version zip is possible only for online repo
    bool download_whole = files_to_download.size() > BATCH_DOWNLOAD_TRESHOLD;
    download_whole &= repo->descriptor().unzipped_data_path.empty();
    
    if (download_whole) {
        // We download whole vendor-version zip, that is available 
        // at URL /v2/repos/<repo_id>/<repo_id>-<vendor_id>-<vendor_version>.zip.
        const std::string zip_filename = fmt::format(
            "{}-{}-{}.zip",
            repo->descriptor().id,
            index.vendor(),
            recommended->config_version.to_string()
        );
        const fs::path target_path(temp_vendor_dir_path / zip_filename);

        if (!ensure_vendor_dir_exists(target_path.parent_path(), index.vendor(), process_status, __FUNCTION__)){
            return false;
        }

        if (!repo->get_file(zip_filename, target_path, process_status)) {
            std::string msg = fmt::format(
                "{}: Failed to get file {}. Staging update has failed.", 
                std::string(__FUNCTION__), zip_filename);
            SPDLOG_ERROR(msg); process_status->set_warning(msg, PresetUpdaterReason::DataUnreadable); return false;
        }

        // Unzip to parent dir (dir named with uuid in temp) so the unzipped tree matches temp_vendor_dir_path structure
        if(!unzip_files_from_bundle_zip(target_path, temp_path, repo, process_status, index)) {
            std::string msg = fmt::format(
                "{}: Failed to unzip bundle {}. Staging update has failed.", 
                std::string(__FUNCTION__), target_path.string());
            SPDLOG_ERROR(msg); process_status->set_warning(msg, PresetUpdaterReason::DataCorrupted); return false;
        }
    } else {
        // Download each file individually
        for (const auto& [name, hash] : files_to_download) {
            const fs::path target_path(temp_vendor_dir_path / recommended->config_version.to_string() / name);
            if (!ensure_vendor_dir_exists(target_path.parent_path(), index.vendor(), process_status, __FUNCTION__)){
                return false;
            }

            const std::string source_subpath = fmt::format("{}/{}/{}", index.vendor(), recommended->config_version.to_string(), name);
            if (!repo->get_file(source_subpath, target_path, process_status)) {
                std::string msg = fmt::format(
                    "{}: Failed to get file {}. Staging update has failed.", 
                    std::string(__FUNCTION__), source_subpath);
                SPDLOG_ERROR(msg); process_status->set_warning(msg, PresetUpdaterReason::DataUnreadable); return false;
            }
        }
    }

    // Move files from temp to update_sync
    for (const auto& [name, hash] : files_to_download) {
        const fs::path target_path(temp_vendor_dir_path / recommended->config_version.to_string() / name);

        if (!fs::exists(target_path, ec) || ec) {
            std::string msg = fmt::format(
                "{}: Failed to find file {}. Error: {}. Bundle is corrupted. Staging update has failed.", 
                std::string(__FUNCTION__), target_path.string(), ec.message());
            SPDLOG_ERROR(msg); process_status->set_warning(msg, PresetUpdaterReason::DataCorrupted); return false;
        }

        // Check file hash - it was not done during download
        if (PresetUpdaterFileHash file_hash = PresetUpdater::file_hash(target_path, process_status); !process_status->ignore_file_hash() && file_hash != hash) {
            std::string msg = fmt::format(
                "File {} has failed file hash test after download. File is probably corrupted.", 
                target_path.string());
            SPDLOG_ERROR(msg); process_status->set_warning(msg, PresetUpdaterReason::DataCorrupted); return false;
        }

        // move to update_sync
        const fs::path dest_path(update_sync_vendor_dir_path / name);
        if (!ensure_vendor_dir_exists(dest_path.parent_path(), index.vendor(), process_status, __FUNCTION__)){
            return false;
        }
        move_file(target_path, dest_path, process_status, __FUNCTION__);
    }

    return true;
}

void delete_staged_files(
    const std::vector<std::string>& files_to_delete,
    const boost::filesystem::path& update_sync_vendor_dir_path,
    PresetUpdaterProcessStatus* process_status)
{
    boost::system::error_code ec;
    
    // Delete selected files in update_sync
    for (const std::string& filename : files_to_delete) {
        fs::path path(update_sync_vendor_dir_path / filename);
        if (!fs::remove(path, ec) || ec) {
            std::string msg = fmt::format(
                "{}: Failed to remove file {}",
                std::string(__FUNCTION__),
                path.string()
            );
            SPDLOG_ERROR(msg);
            process_status->set_warning(msg, PresetUpdaterReason::LocalStorageFailed);
        }
    }
}

bool copy_vendor_from_resources_to_sync(
    const boost::filesystem::path& update_sync_vendor_dir,
    const boost::filesystem::path& update_sync_index_path,
    const boost::filesystem::path& source_vendor_dir,
    const PresetUpdaterIndex& source_index,
    PresetUpdaterProcessStatus* process_status)
{
    boost::system::error_code ec;

    // Copy source to update_sync
    if (!fs::create_directories(update_sync_vendor_dir, ec) && ec) {
        process_status->set_warning(
            "Failed to create vendor dir " + update_sync_vendor_dir.string() + ". " + ec.message()
        , PresetUpdaterReason::LocalStorageFailed);
        return false;
    }

    for (const auto& entry : fs::recursive_directory_iterator(source_vendor_dir, ec)) {
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }
        const fs::path source  = entry.path();
        fs::path relative_path = fs::relative(source, source_vendor_dir);
        const fs::path target  = update_sync_vendor_dir / relative_path;

        if (!ensure_vendor_dir_exists(target.parent_path(), target.parent_path().string(), process_status, __FUNCTION__)){
            return false;
        }

        if (!copy_file_wrapper(source, target, process_status)) {
            return false;
        }
    }
    
    if (ec) {
        std::string msg = fmt::format(
            "{}: Error traversing directory {}: {}",
            std::string(__FUNCTION__),
            source_vendor_dir.string(),
            ec.message()
        );
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::LocalStorageFailed);
        return false;
    }

    // The index is the commit marker: copy it only once the payload is whole.
    if (!copy_file_wrapper(source_index.path(), update_sync_index_path, process_status)) {
        return false;
    }

    return true;
}

bool should_keep_staged_version(
    const boost::filesystem::path& update_sync_index_path,
    const boost::filesystem::path& update_sync_vendor_yaml,
    const PresetUpdaterIndex::const_iterator& recommended,
    Preset::IO::HwConfigLoader& hw_config_loader,
    PresetUpdaterProcessStatus* process_status)
{
    boost::system::error_code ec;

    // Check version staged in update_sync
    if (fs::exists(update_sync_index_path, ec) && !ec && fs::exists(update_sync_vendor_yaml, ec) && !ec) {
        PresetUpdaterIndex update_sync_index;
        bool loaded_update_sync_index = false;
        try {
            update_sync_index.load(update_sync_index_path);
            loaded_update_sync_index = true;
        } catch (const std::runtime_error&) {
            process_status->set_warning(
                "Failed to load index " + update_sync_index_path.string()
            , PresetUpdaterReason::IndexUnreadable);
        }
        
        if (loaded_update_sync_index) {
            const PresetUpdaterIndex::const_iterator update_sync_recommended = update_sync_index.recommended();
            if (update_sync_recommended->config_version > recommended->config_version) {
                // Update sync has some more recent data - nothing to do here
                return true;
            }
            
            // read version of staged
            Domain::Preset::VendorData update_sync_vendor_data;
            try {
                update_sync_vendor_data = hw_config_loader.load(update_sync_vendor_yaml.string());
                Semver update_sync_version = Semver(update_sync_vendor_data.info.version);
                if (update_sync_version == recommended->config_version) {
                    // staged version is recommended - nothing to do here
                    return true;
                }
            } catch (const std::exception& e) {
                SPDLOG_ERROR(
                    "Failed to load vendor file {}: {}",
                    update_sync_vendor_yaml.string(),
                    e.what()
                );
            }
        }
    }
    
    return false;
}

std::map<std::string, PresetUpdaterFileHash> get_directory_file_hashes(
    const fs::path& dir_path,
    PresetUpdaterProcessStatus* process_status,
    const char* func_name)
{
    std::map<std::string, PresetUpdaterFileHash> hashes;
    boost::system::error_code ec;
    
    for (const auto& entry : fs::recursive_directory_iterator(dir_path, ec)) {
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }
        const fs::path& path = entry.path();
        hashes.emplace(fs::relative(path, dir_path).string(), file_hash(path, process_status));
    }
    
    if (ec) {
        std::string msg = fmt::format("{}: Error traversing directory {}: {}", func_name, dir_path.string(), ec.message());
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::LocalStorageFailed);
    }
    return hashes;
}

bool download_version_manifest(
    const AbstractPresetUpdaterRepository* repo,
    const PresetUpdaterIndex& index,
    const PresetUpdaterIndex::const_iterator& recommended,
    const fs::path& temp_manifest_path,
    PresetUpdaterProcessStatus* process_status,
    const char* func_name)
{
    const std::string source_subpath = fmt::format("{}/{}/manifest.json", index.vendor(), recommended->config_version.to_string());
    
    if (!repo->get_version_manifest(source_subpath, temp_manifest_path, process_status)) {
        std::string msg = fmt::format("{}: Failed to get file {}. Staging update has failed.", func_name, source_subpath);
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::DataUnreadable);
        return false;
    }
    return true;
}

struct SyncOperations {
    std::vector<std::string> files_to_delete;
    std::map<std::string, std::string> files_to_download;
};

SyncOperations calculate_sync_operations(
    const std::map<std::string, std::string>& requested_files,
    const std::map<std::string, PresetUpdaterFileHash>& existing_files)
{
    SyncOperations ops;
    for (const auto& [name, hash] : requested_files) {
        auto it = existing_files.find(name);
        if (it == existing_files.end()) {
            // Only in requested_files -> download
            ops.files_to_download.emplace(name, hash);
        } else if (it->second != hash) {
            // In both requested_files and existing_files and different hash -> delete and download
            ops.files_to_delete.push_back(name);
            ops.files_to_download.emplace(name, hash);
        }
        // In both requested_files and existing_files and same hash = do nothing
    }
    for (const auto& [name, hash] : existing_files) {
        if (!requested_files.contains(name)) {
            // Only in existing_files -> delete
            ops.files_to_delete.push_back(name);
        }
    }
    return ops;
}

} // namespace

void PresetUpdaterRepositorySync::sync(
    const SharedRepositoryVector& repositories,
    bool online_allowed,
    PresetUpdaterProcessStatus* process_status
) const
{
    ASSERT(
        process_status,
        "Every step of the sync reports through it, so there is nothing sensible to do without one."
    );

    // Create workspace in OS temp folder.
    fs::path temp_dir;
    const fs::path resources_dir = fs::path(Slic3r::resources_dir()) / "presets";

    try {
        temp_dir = create_temp_dir();
    } catch (const Slic3r::RuntimeError& e) {
        process_status->set_error(std::string("Preset Archive Sync has failed. ") + e.what(), PresetUpdaterReason::DataDirUnusable);
        return;
    }

    for (const AbstractPresetUpdaterRepository* repo : repositories) {
        if (process_status->get_canceled()) {
            return;
        }
        if (!repo->is_selected()) {
            process_status->set_warning_target(repo->descriptor().id);
            remove_directory(
                fs::path(data_dir()) / "update_sync" / repo->descriptor().id,
                process_status,
                __FUNCTION__
            );
            process_status->clear_warning_target();
        }
        stage_rencofigurations_from_resources(resources_dir / repo->descriptor().id, repo, process_status);
    }

    // perform sync on every repository
    for (const AbstractPresetUpdaterRepository* repo : repositories) {
        if (process_status->get_canceled()) {
            break;
        }
        if (!repo->is_selected()) {
            SPDLOG_INFO(
                "Source {} is turned off, so only what the installation carries is staged for it.",
                repo->descriptor().id
            );
            continue;
        }
        if (repo->is_online() && !online_allowed) {
            SPDLOG_INFO(
                "Preset updates over the internet are turned off. Skipping source {}.",
                repo->descriptor().id
            );
            continue;
        }
        this->sync_repository(temp_dir / repo->descriptor().id, repo, process_status);

        // remove empty directories
        const fs::path update_sync_repo_dir = fs::path(data_dir()) / "update_sync" / repo->descriptor().id;
        boost::system::error_code ec;
        if (fs::is_directory(update_sync_repo_dir, ec)) {
            if (!fs::remove(update_sync_repo_dir, ec) && ec) {
                if (ec != boost::system::errc::directory_not_empty) {
                    process_status->set_warning(
                        "Failed to remove empty repo dir " + update_sync_repo_dir.string() + ". " + ec.message()
                    , PresetUpdaterReason::LocalStorageFailed);
                    return;
                }
            }
        }
    }

    remove_directory(temp_dir, process_status, __FUNCTION__);
}

void PresetUpdaterRepositorySync::stage_rencofigurations_from_resources(
    const boost::filesystem::path& source_dir,
    const AbstractPresetUpdaterRepository* repo,
    PresetUpdaterProcessStatus* process_status
) const
{
    boost::system::error_code ec;
    // Each repo has its own subdir. It does not have to be in resouces.
    if (!fs::exists(source_dir) || ec) {
        SPDLOG_INFO(
            "{} Directory does not exists {}: {}. Skipping.",
            std::string(__FUNCTION__),
            source_dir.string(),
            ec.message()
        );
        return;
    }

    const fs::path update_sync_repo_dir = fs::path(data_dir()) / "update_sync" / repo->descriptor().id;
    if (!fs::create_directory(update_sync_repo_dir, ec) && ec) {
        warn_about_repo(
            process_status,
            repo->descriptor().id,
            "Failed to create repo dir " + update_sync_repo_dir.string() + ". " + ec.message()
        );
        return;
    }

    std::vector<PresetUpdaterIndex> index_db;
    try {
        index_db = load_vendors_db(source_dir);
    } catch (const std::exception& e) {
        std::string msg = fmt::format(
            "Loading index db of {} has failed {}",
            source_dir.string(),
            e.what()
        );
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::IndexUnreadable);
        return;
    }

    for (const PresetUpdaterIndex& source_index : index_db) {
        process_status->set_warning_target(repo->descriptor().id, source_index.vendor());
        if (process_status->get_canceled()) {
            return;
        }
        if (!check_resouces_vendor_sanity(source_dir, repo, process_status, source_index)) {
            continue;
        }
        if (!is_vendor_installed(source_index.vendor(), repo->descriptor().id)) {
            stage_not_installed_vendor_from_resources(source_dir, repo, process_status, source_index);
            continue;
        }
        stage_installed_vendor_from_resources(source_dir, repo, process_status, source_index);
    }
    process_status->clear_warning_target();
}

void PresetUpdaterRepositorySync::stage_not_installed_vendor_from_resources(
    const boost::filesystem::path& source_dir,
    const AbstractPresetUpdaterRepository* repo,
    PresetUpdaterProcessStatus* process_status,
    const PresetUpdaterIndex& source_index
) const
{
    const fs::path update_sync_vendor_dir = fs::path(data_dir())
        / "update_sync"
        / repo->descriptor().id
        / source_index.vendor();
    const fs::path update_sync_index_path = fs::path(data_dir())
        / "update_sync"
        / repo->descriptor().id
        / (source_index.vendor() + ".idx"); // index is outside vendor folder.
    const fs::path update_sync_vendor_yaml = update_sync_vendor_dir / "vendor.yaml";

    const fs::path source_vendor_dir  = source_dir / source_index.vendor();

    boost::system::error_code ec;

    // Recommended version of vendor
    const PresetUpdaterIndex::const_iterator recommended = source_index.recommended();
    PresetUpdaterIndex update_sync_index; // recommneded is an iterator - once its index object stops existing it ivalidates.
    if (recommended == source_index.end()) {
        process_status->set_warning(
            fmt::format(
                "No recommended version for vendor: {}, Index file might be corrupted.",
                source_index.vendor()
            )
        , PresetUpdaterReason::NoUsableVersion);
        return;
    }

    // Check version staged in update_sync
    Preset::IO::HwConfigLoader hw_config_loader;
    if (should_keep_staged_version(update_sync_index_path, update_sync_vendor_yaml, recommended, hw_config_loader, process_status)) {
        return;
    }

    // Now we know that data in update_sync are not the recommended version
    // We also know that data in source (resources) are usable data - they came with the installation of this binary.
    // We simply delete data in update_sync and replace them with source.
    remove_file(update_sync_index_path, process_status, __FUNCTION__);
    remove_directory(update_sync_vendor_dir, process_status, __FUNCTION__);

    // Copy source to update_sync
    if (!copy_vendor_from_resources_to_sync(
            update_sync_vendor_dir,
            update_sync_index_path,
            source_vendor_dir,
            source_index,
            process_status)) 
    {
        return;
    }
}

void PresetUpdaterRepositorySync::stage_installed_vendor_from_resources(
    const boost::filesystem::path& source_dir,
    const AbstractPresetUpdaterRepository* repo,
    PresetUpdaterProcessStatus* process_status,
    const PresetUpdaterIndex& source_index
) const
{
    const fs::path installed_vendor_dir =
        local_vendor_path(repo->descriptor().id, source_index.vendor());
    const fs::path installed_vendor_yaml = installed_vendor_dir / "vendor.yaml";

    const fs::path update_sync_vendor_dir = fs::path(data_dir())
        / "update_sync"
        / repo->descriptor().id
        / source_index.vendor();
    const fs::path update_sync_index_path = fs::path(data_dir())
        / "update_sync"
        / repo->descriptor().id
        / (source_index.vendor() + ".idx"); // index is outside vendor folder.
    const fs::path update_sync_vendor_yaml = update_sync_vendor_dir / "vendor.yaml";

    const fs::path source_vendor_dir  = source_dir / source_index.vendor();

    boost::system::error_code ec;
    Preset::IO::HwConfigLoader hw_config_loader;

    // Recommended version of vendor
    const PresetUpdaterIndex::const_iterator recommended = source_index.recommended();
    PresetUpdaterIndex installed_index; // recommneded is an iterator - once its index object stops existing it ivalidates.
    PresetUpdaterIndex update_sync_index;
    if (recommended == source_index.end()) {
        process_status->set_warning(
            fmt::format(
                "No recommended version for vendor: {}, Index file might be corrupted.",
                source_index.vendor()
            )
        , PresetUpdaterReason::NoUsableVersion);
        return;
    }

    // Check if installed version is recommended
    try {
        Domain::Preset::VendorData installed_vendor_data;
        installed_vendor_data = hw_config_loader.load(installed_vendor_yaml.string());
        Semver installed_version = Semver(installed_vendor_data.info.version);
        if (installed_version == recommended->config_version) {
            // installed version is recommended - nothing to do here
            // Check version staged in update_sync - if its not higher than index recommended - delete it.
            if (fs::exists(update_sync_vendor_yaml, ec) && !ec) {
                Domain::Preset::VendorData update_sync_vendor_data;
                try {
                    update_sync_vendor_data = hw_config_loader.load(update_sync_vendor_yaml.string());
                    Semver update_sync_version = Semver(update_sync_vendor_data.info.version);
                    if (update_sync_version > recommended->config_version) {
                        // staged version is recommended - nothing to do here
                        return;
                    }
                } catch (const std::exception& e) {
                    SPDLOG_ERROR(
                        "Failed to load vendor file {}: {}",
                        update_sync_vendor_yaml.string(),
                        e.what()
                    );
                }
                remove_file(update_sync_index_path, process_status, __FUNCTION__);
                remove_directory(update_sync_vendor_dir, process_status, __FUNCTION__);
            }

            return;
        }
    } catch (const std::exception& e) {
        std::string msg = fmt::format(
            "Failed to load vendor file {}: {}",
            installed_vendor_yaml.string(),
            e.what()
        );
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::DataInconsistent);
    }


    // Check version staged in update_sync
    if (should_keep_staged_version(update_sync_index_path, update_sync_vendor_yaml, recommended, hw_config_loader, process_status)) {
        return;
    }

    // Now we know that data in update_sync are not the recommended version
    // We also know that data in source (resources) are usable data - they came with the installation of this binary.
    // We simply delete data in update_sync and replace them with source. 
    remove_file(update_sync_index_path, process_status, __FUNCTION__);
    remove_directory(update_sync_vendor_dir, process_status, __FUNCTION__);

    // Copy source to update_sync
    if (!copy_vendor_from_resources_to_sync(
            update_sync_vendor_dir,
            update_sync_index_path,
            source_vendor_dir,
            source_index,
            process_status)) 
    {
        return;
    }
}

void PresetUpdaterRepositorySync::sync_repository(
    const boost::filesystem::path& temp_dir,
    const AbstractPresetUpdaterRepository* repo,
    PresetUpdaterProcessStatus* process_status
) const
{
    // SPDLOG_INFO(__FUNCTION__);
    process_status->set_download_target(repo->descriptor().id + " repository");

    boost::system::error_code ec;

    // Each repo has its own subdir.
    if (!fs::create_directory(temp_dir, ec) && ec) {
        std::string msg = fmt::format(
            "Failed to check updates for source {}. Failed to create temp directory {}: {}.",
            repo->descriptor().id,
            temp_dir.string(),
            ec.message()
        );
        SPDLOG_ERROR(msg);
        warn_about_repo(process_status, repo->descriptor().id, msg);
        return;
    }

    fs::path archive_path(temp_dir / "vendor_indices.zip");
    // Download profiles repo zip
    if (!repo->get_archive(archive_path, process_status)) {
        std::string msg = fmt::format(
            "Failed to check updates for source {}. Failed to download vendor profiles archive zip file.",
            repo->descriptor().id
        );
        SPDLOG_ERROR(msg);
        warn_about_repo(process_status, repo->descriptor().id, msg);
        return;
    }
    if (process_status->get_canceled()) {
        return;
    }
    /*
    enum class VendorStatus
    {
        Unknown, // default state after unzipping index
        InTemp, // Vendor not installed and its recommended version is to be downloaded.
        //IN_RESOURCES, // Vendor not installed and its recommended version is in resources.
        Installed, // Vendor is installed and it is its recommended version.
        NewVersion, // Vendor is installed, but recommended version is different, it is to be downloaded.
    };
    */
    std::vector<std::string> vendors_list;

    // desired temp_dir appearance
    // temp_dir
    // |- vendor1 # directory of vendor1 if needed
    // |- data # all data needed
    // |- vendor3 # directory of vendor3 if needed (vendor2 apparently needs nothing to download)
    // |-vendor_indices.zip # zip file that changes for each repository
    // |-vendor1.idx
    // |-vendor2.idx
    // |-vendor3.idx
    //

    // Unzip archive to temp_dir
    ScopedZipReader archive;
    if (!archive.open(archive_path.string())) {
        std::string msg = fmt::format(
            "Failed to check updates for source {}. Couldn't open zipped bundle.",
            repo->descriptor().id
        );
        SPDLOG_ERROR(msg);
        warn_about_repo(process_status, repo->descriptor().id, msg);
        return;
    } else {
        mz_uint num_entries = mz_zip_reader_get_num_files(archive.get());
        mz_zip_archive_file_stat stat;
        for (mz_uint i = 0; i < num_entries; ++i) {
            if (mz_zip_reader_file_stat(archive.get(), i, &stat)) {
                std::string name(stat.m_filename);

                if (mz_zip_reader_is_file_a_directory(archive.get(), i)) {
                    std::string msg = fmt::format("Skipping directory entry: {}", name);
                    SPDLOG_INFO(msg);
                    continue;
                }

                if (stat.m_uncomp_size > 0) {
                    auto buffer = std::make_unique_for_overwrite<char[]>((size_t) stat.m_uncomp_size);
                    mz_bool res = mz_zip_reader_extract_to_mem(
                        archive.get(),
                        stat.m_file_index,
                        buffer.get(),
                        (size_t) stat.m_uncomp_size,
                        0
                    );
                    if (res == 0) {
                        std::string msg = fmt::format("Failed to unzip {}", stat.m_filename);
                        SPDLOG_ERROR(msg);
                        process_status->set_warning(msg, PresetUpdaterReason::DataCorrupted);
                        continue;
                    }
    
                    fs::path tmp_path(temp_dir / (name + ".tmp"));
                    if (!fs::exists(tmp_path.parent_path(), ec) || ec) {
                        std::string msg = fmt::format(
                            "Failed to unzip file {}. Directories are not supported. Skipping file.",
                            name
                        );
                        SPDLOG_ERROR(msg);
                        process_status->set_warning(msg, PresetUpdaterReason::DataCorrupted);
                        continue;
                    }
    
                    fs::path target_path(temp_dir / name);
                    boost::nowide::fstream file(
                        tmp_path,
                        std::ios::out | std::ios::binary | std::ios::trunc
                    );

                    if (!file) {
                        std::string msg = fmt::format("Failed to open temp file for writing: {}", tmp_path.string());
                        SPDLOG_ERROR(msg);
                        process_status->set_warning(msg, PresetUpdaterReason::LocalStorageFailed);
                        continue;
                    }

                    file.write(buffer.get(), (std::streamsize) stat.m_uncomp_size);
                    if (!file) {
                        std::string msg = fmt::format("Failed to write to temp file: {}", tmp_path.string());
                        SPDLOG_ERROR(msg);
                        process_status->set_warning(msg, PresetUpdaterReason::LocalStorageFailed);
                        continue;
                    }
                    file.close();
    
                    bool exists = fs::exists(tmp_path, ec);
                    if (!exists || ec) {
                        std::string msg = fmt::format(
                            "Failed to find unzipped file at {}. Terminating Preset updater synchronization.",
                            tmp_path.string()
                        );
                        SPDLOG_ERROR(msg);
                        process_status->set_warning(msg, PresetUpdaterReason::LocalStorageFailed);
                        return;
                    }

                    auto result = safe_move(tmp_path, target_path);
                    if (ec || !result.has_value()) {
                        std::string msg = fmt::format(
                            "Failed to rename unzipped file at {}. Terminating Preset updater synchorinzation. Error message: {}",
                            tmp_path.string(),
                            ec ? ec.message() : result.error()
                        );
                        SPDLOG_ERROR(msg);
                        process_status->set_warning(msg, PresetUpdaterReason::LocalStorageFailed);
                        return;
                    }

                    if (name.length() >= 3 && name.substr(name.size() - 3) == "idx") {
                        vendors_list.emplace_back(name);
                    }
                }
            }
        }
    }

    // Now we have vendors_list, but we need index_db.
    std::vector<PresetUpdaterIndex> index_db;
    try {
        index_db = load_vendors_db_filtered(temp_dir, vendors_list);
    } catch (const std::exception& e) {
        std::string msg = fmt::format(
            "Loading index db of {} has failed {}",
            repo->descriptor().id,
            e.what()
        );
        SPDLOG_ERROR(msg);
        warn_about_repo(process_status, repo->descriptor().id, msg);
        return;
    }

    for (const auto& index : index_db) {
        if (process_status->get_canceled()) {
            return;
        }
        process_status->set_warning_target(repo->descriptor().id, index.vendor());
        if (!is_vendor_installed(index.vendor(), repo->descriptor().id)) {
            // For not installed vendor we need to download its recommended version.
            sync_not_installed_vendor(temp_dir, repo, process_status, index);
            continue;
        }
        // if installed, it can still mean there is different recommended version.
        sync_installed_vendor(temp_dir, repo, process_status, index);
    }
    process_status->clear_warning_target();
}

void PresetUpdaterRepositorySync::sync_not_installed_vendor(
    const boost::filesystem::path& temp_path,
    const AbstractPresetUpdaterRepository* repo,
    PresetUpdaterProcessStatus* process_status,
    const PresetUpdaterIndex& index
) const
{
    const fs::path temp_vendor_dir_path        = temp_path / index.vendor();
    const fs::path update_sync_vendor_dir_path = fs::path(data_dir())
        / "update_sync"
        / repo->descriptor().id
        / index.vendor();
    const fs::path temp_manifest_path = temp_vendor_dir_path / (index.vendor() + ".manifest");
    const fs::path update_sync_manifest_path = update_sync_vendor_dir_path
        / (index.vendor() + ".manifest");
    const fs::path index_update_sync_path = update_sync_vendor_dir_path.parent_path()
        / index.path().filename();
    boost::system::error_code ec;

    // Recommended version of vendor
    const PresetUpdaterIndex::const_iterator recommended = index.recommended();
    if (recommended == index.end()) {
        std::string msg = fmt::format(
            "No recommended version for vendor: {}, Index file might be corrupted.",
            index.vendor()
        );
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::NoUsableVersion);
        return;
    }

    // Create directory in temp. Delete all previous content if already extists.
    if(!prepare_temp_vendor_dir(temp_vendor_dir_path, index.vendor(), process_status, __FUNCTION__)) {
        return;
    }

    if (!download_version_manifest(repo, index, recommended, temp_manifest_path, process_status, __FUNCTION__)) {
        return;
    }

    // read the version manifest
    std::map<std::string, std::string> files_in_version_manifest = read_version_manifest(
        temp_manifest_path,
        process_status
    );
    SPDLOG_INFO(
        "{}/{}: {} files",
        repo->descriptor().id,
        index.vendor(),
        std::to_string(files_in_version_manifest.size())
    );

    // Create vendor dir in update_sync (leave files in it if already exists)
    if (!ensure_vendor_dir_exists(update_sync_vendor_dir_path, index.vendor(), process_status, __FUNCTION__)){
        return;
    }

    // Create list of files in update_sync
    std::map<std::string, PresetUpdaterFileHash> files_in_update_sync =
        get_directory_file_hashes(update_sync_vendor_dir_path, process_status, __FUNCTION__);

    // Compare files_in_version_manifest and files_in_update_sync
    SyncOperations sync_files = calculate_sync_operations(files_in_version_manifest, files_in_update_sync);

    // The index is the commit marker: dropping it hides the payload while it is rebuilt.
    remove_file(index_update_sync_path, process_status, __FUNCTION__);

    // Delete selected files in update_sync
    delete_staged_files(sync_files.files_to_delete, update_sync_vendor_dir_path, process_status);

    // Download and stage files
    if (!download_and_stage_vendor_files(
            sync_files.files_to_download, 
            repo, 
            index, 
            recommended, 
            temp_vendor_dir_path, 
            temp_path, 
            update_sync_vendor_dir_path, 
            process_status)) 
    {
        return; 
    }


    // Move manifest file to update_sync
    const bool manifest_staged =
        move_file(temp_manifest_path, update_sync_manifest_path, process_status, __FUNCTION__);

    // Move index to stage_sync
    if (manifest_staged) {
        move_file(index.path(), index_update_sync_path, process_status, __FUNCTION__);
    }

    // Cleanup temp
    remove_directory(temp_vendor_dir_path, process_status, __FUNCTION__);
}

void PresetUpdaterRepositorySync::sync_installed_vendor(
    const boost::filesystem::path& temp_path,
    const AbstractPresetUpdaterRepository* repo,
    PresetUpdaterProcessStatus* process_status,
    const PresetUpdaterIndex& index
) const
{
    const fs::path installed_vendor_dir_path =
        local_vendor_path(repo->descriptor().id, index.vendor());
    const fs::path update_sync_vendor_dir_path = fs::path(data_dir())
        / "update_sync"
        / repo->descriptor().id
        / index.vendor();
    const fs::path temp_vendor_dir_path = temp_path / index.vendor();
    const fs::path temp_manifest_path   = temp_vendor_dir_path / (index.vendor() + ".manifest");
    const fs::path update_sync_manifest_path = update_sync_vendor_dir_path
        / (index.vendor() + ".manifest");
    const fs::path update_sync_vendor_yaml_path = update_sync_vendor_dir_path / "vendor.yaml";
    const fs::path index_update_sync_path = update_sync_vendor_dir_path.parent_path()
        / index.path().filename();
    const fs::path installed_vendor_yaml_path = installed_vendor_dir_path / "vendor.yaml";
    boost::system::error_code ec;

    // Current installed version
    Preset::IO::HwConfigLoader hw_config_loader;
    Domain::Preset::VendorData vendor_data;
    try {
        vendor_data = hw_config_loader.load(installed_vendor_yaml_path.string());
    } catch (const std::exception& e) {
        process_status->set_warning(
            fmt::format("Failed to read vendor file {}: {}", installed_vendor_yaml_path.string(), e.what())
        , PresetUpdaterReason::DataInconsistent);
        return;
    }
    Semver installed_version = Semver(vendor_data.info.version);

    // Recommended version of vendor
    const PresetUpdaterIndex::const_iterator recommended = index.recommended();
    if (recommended == index.end()) {
        process_status->set_warning(
            fmt::format(
                "No recommended version for vendor: {}, Index file might be corrupted.",
                index.vendor()
            )
        , PresetUpdaterReason::NoUsableVersion);
        return;
    }
    const PresetUpdaterIndex::const_iterator vendor_current_version_it = index.find(installed_version);
    if (recommended == vendor_current_version_it) {
        SPDLOG_INFO(
            "Vendor {}/{} has installed recommended version.",
            repo->descriptor().id,
            index.vendor()
        );

        // Delete staged in update_sync. It is not needed.
        remove_file(index_update_sync_path, process_status, __FUNCTION__);
        remove_directory(update_sync_vendor_dir_path, process_status, __FUNCTION__);
        return;
    }

    // Create directory in temp. Delete all previous content if already extists.
    if (!prepare_temp_vendor_dir(temp_vendor_dir_path, index.vendor(), process_status, __FUNCTION__)) {
        return;
    }

    if (!download_version_manifest(repo, index, recommended, temp_manifest_path, process_status, __FUNCTION__)) {
        return;
    }

    // read the version manifest
    std::map<std::string, std::string> files_in_version_manifest = read_version_manifest(
        temp_manifest_path,
        process_status
    );
    SPDLOG_INFO(
        "{}/{}: {} files",
        repo->descriptor().id,
        index.vendor(),
        std::to_string(files_in_version_manifest.size())
    );

    // Create list of files in installed_vendor_dir_path and subdirectories
    std::map<std::string, PresetUpdaterFileHash> files_in_installed =
        get_directory_file_hashes(installed_vendor_dir_path, process_status, __FUNCTION__);

    // Compare files_in_version_manifest and files_in_update_sync
    std::map<std::string, std::string> files_to_download_against_installed;
    for (const auto& [name, hash] : files_in_version_manifest) {
        auto it = files_in_installed.find(name);
        if (it == files_in_installed.end() || it->second != hash) {
            // Not in installed or not same hash -> download
            files_to_download_against_installed[name] = hash;
        }
        // In both files_in_version_manifest and files_in_update_sync and same hash = do nothing
    }

    // Create vendor dir in update_sync (leave files in it if already exists)
    if (!ensure_vendor_dir_exists(update_sync_vendor_dir_path, index.vendor(), process_status, __FUNCTION__)){
        return;
    }

    // Create list of files in update_sync
    std::map<std::string, PresetUpdaterFileHash> files_in_update_sync =
        get_directory_file_hashes(update_sync_vendor_dir_path, process_status, __FUNCTION__);

    // Compare files_to_download_against_installed and files_in_update_sync
    SyncOperations sync_files = calculate_sync_operations(files_to_download_against_installed, files_in_update_sync);
   
    // The index is the commit marker: dropping it hides the payload while it is rebuilt.
    remove_file(index_update_sync_path, process_status, __FUNCTION__);

    // Delete selected files in update_sync
    delete_staged_files(sync_files.files_to_delete, update_sync_vendor_dir_path, process_status);

    // Download and stage files
    if (!download_and_stage_vendor_files(
            sync_files.files_to_download, 
            repo, 
            index, 
            recommended, 
            temp_vendor_dir_path, 
            temp_path, 
            update_sync_vendor_dir_path, 
            process_status)) 
    {
        return; 
    }

    // Move manifest file to update_sync
    const bool manifest_staged =
        move_file(temp_manifest_path, update_sync_manifest_path, process_status, __FUNCTION__);

    // Move index to stage_sync
    if (manifest_staged) {
        move_file(index.path(), index_update_sync_path, process_status, __FUNCTION__);
    }

    // Cleanup temp
    remove_directory(temp_vendor_dir_path, process_status, __FUNCTION__);
}

} // namespace Slic3r::Biz::PresetUpdater
