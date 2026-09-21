#include "PresetUpdaterRepositorySync.hpp"

#include "PresetUpdaterProcessStatus.hpp"
#include "PresetUpdaterRepository.hpp"
#include "PresetUpdaterIndex.hpp"
#include "PresetUpdaterFileHash.hpp"
#include "PresetUpdaterUtils.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterStaging.hpp"
#include "Slic3r/Biz/Preset/IO/HwConfigLoader.hpp"
#include "Slic3r/Directories.hpp"

#include "Slic3r/Exception.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"

#include "Slic3r/Biz/Algorithms/ZipArchive.hpp"

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

constexpr size_t BATCH_DOWNLOAD_TRESHOLD = 20;

namespace {

fs::path update_sync_root()
{
    return fs::path(data_dir()) / "update_sync";
}

fs::path update_sync_repo_path(const std::string& repo_id)
{
    return update_sync_root() / repo_id;
}

fs::path update_sync_index_path(const std::string& repo_id, const std::string& vendor_id)
{
    return update_sync_repo_path(repo_id) / (vendor_id + ".idx");
}

fs::path resources_repo_path(const std::string& repo_id)
{
    return fs::path(Slic3r::resources_dir()) / "presets" / repo_id;
}

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

void remove_empty_parents(fs::path directory, const fs::path& root)
{
    const fs::path::string_type& root_native = root.native();

    boost::system::error_code ec;
    while (directory.native().size() > root_native.size()
           && directory.native().compare(0, root_native.size(), root_native) == 0)
    {
        if (!fs::is_directory(directory, ec) || ec || !fs::is_empty(directory, ec) || ec) {
            return;
        }
        if (!fs::remove(directory, ec) || ec) {
            return;
        }
        directory = directory.parent_path();
    }
}

bool ensure_dir_exists(
    const boost::filesystem::path& target_dir,
    PresetUpdaterProcessStatus* process_status,
    const char* func_name)
{
    boost::system::error_code ec;
    if (!boost::filesystem::create_directories(target_dir, ec) && ec) {
        std::string msg = fmt::format(
            "{}: Failed to create directory {}: {}.",
            func_name, target_dir.string(), ec.message());
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::LocalStorageFailed);
        return false;
    }
    return true;
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

bool unzip_files_from_bundle_zip(
    const boost::filesystem::path& zip_file,
    const boost::filesystem::path& dest_dir,
    PresetUpdaterProcessStatus* process_status
)
{
    Biz::Algorithms::ZipArchive archive = Biz::Algorithms::ZipArchive::open(
        zip_file.string(),
        Biz::Algorithms::ZipArchive::Mode::Read
    );

    if (!archive.is_open()) {
        std::string msg = fmt::format("Couldn't open zipped bundle: {}", zip_file.string());
        SPDLOG_ERROR(msg);
        if (process_status) process_status->set_warning(msg, PresetUpdaterReason::DataCorrupted);
        return false;
    }

    mz_uint num_entries = archive.get_num_files();
    mz_zip_archive_file_stat stat;
    bool success = true;

    for (mz_uint i = 0; i < num_entries; ++i) {
        if (process_status && process_status->get_canceled()) {
            success = false;
            break;
        }

        if (!archive.stat_file(i, stat)) {
            continue;
        }

        if (archive.is_file_directory(i)) {
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
            if (!archive.extract_to_file(stat.m_file_index, target_path.string())) {
                std::string msg = fmt::format("Failed to extract {}", stat.m_filename);
                SPDLOG_ERROR(msg);
                if (process_status)
                    process_status->set_warning(msg, PresetUpdaterReason::DataCorrupted);
                success = false;
                continue;
            }
        }
    }

    return success;
}

std::map<std::string, PresetUpdaterFileHash> get_directory_file_hashes(
    const fs::path& dir_path,
    PresetUpdaterProcessStatus* process_status,
    const char* func_name)
{
    std::map<std::string, PresetUpdaterFileHash> hashes;
    boost::system::error_code ec;

    // A vendor that is not installed yet has no directory, which is not a failure - it is the
    // answer: nothing is there.
    if (!fs::is_directory(dir_path, ec) || ec) {
        return hashes;
    }

    for (const auto& entry : fs::recursive_directory_iterator(dir_path, ec)) {
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }
        const fs::path& path = entry.path();
        hashes.emplace(fs::relative(path, dir_path).generic_string(), file_hash(path, process_status));
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
    const std::string& vendor_id,
    const Slic3r::Semver& version,
    const fs::path& manifest_path,
    PresetUpdaterProcessStatus* process_status,
    const char* func_name)
{
    const std::string source_subpath = fmt::format("{}/{}/manifest.json", vendor_id, version.to_string());

    if (!repo->get_version_manifest(source_subpath, manifest_path, process_status)) {
        std::string msg = fmt::format("{}: Failed to get file {}. Update has failed.", func_name, source_subpath);
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::DataUnreadable);
        return false;
    }
    return true;
}

/**
 * @brief Downloads into staging under the manifest relative names. Nothing here touches the
 * installed presets.
 */
bool download_vendor_files(
    const std::map<std::string, std::string>& files_to_download,
    const AbstractPresetUpdaterRepository* repo,
    const std::string& vendor_id,
    const Slic3r::Semver& version,
    const fs::path& staging_dir,
    PresetUpdaterProcessStatus* process_status)
{
    boost::system::error_code ec;

    if (files_to_download.empty()) {
        return true;
    }

    // Whole version zip is possible only for online repo.
    bool download_whole = files_to_download.size() > BATCH_DOWNLOAD_TRESHOLD;
    download_whole &= repo->descriptor().unzipped_data_path.empty();

    if (download_whole) {
        // Available at URL /v2/repos/<repo_id>/<repo_id>-<vendor_id>-<vendor_version>.zip.
        const std::string zip_filename = fmt::format(
            "{}-{}-{}.zip",
            repo->descriptor().id,
            vendor_id,
            version.to_string()
        );
        // The zip carries its own <vendor>/<version>/ prefix, so it has to be unpacked one level
        // above the vendor for its contents to land in the staging directory of this version.
        const fs::path repo_staging = staging_dir.parent_path().parent_path();
        const fs::path zip_path(repo_staging / zip_filename);

        if (!ensure_dir_exists(repo_staging, process_status, __FUNCTION__)) {
            return false;
        }

        if (!repo->get_file(zip_filename, zip_path, process_status)) {
            std::string msg = fmt::format(
                "{}: Failed to get file {}. Update has failed.",
                std::string(__FUNCTION__), zip_filename);
            SPDLOG_ERROR(msg); process_status->set_warning(msg, PresetUpdaterReason::DataUnreadable); return false;
        }

        const bool unzipped = unzip_files_from_bundle_zip(zip_path, repo_staging, process_status);
        remove_file(zip_path, process_status, __FUNCTION__);

        if (!unzipped) {
            std::string msg = fmt::format(
                "{}: Failed to unzip bundle {}. Update has failed.",
                std::string(__FUNCTION__), zip_path.string());
            SPDLOG_ERROR(msg); process_status->set_warning(msg, PresetUpdaterReason::DataCorrupted); return false;
        }
    } else {
        for (const auto& [name, hash] : files_to_download) {
            const fs::path target_path(staging_dir / name);
            if (!ensure_dir_exists(target_path.parent_path(), process_status, __FUNCTION__)) {
                return false;
            }

            const std::string source_subpath = fmt::format("{}/{}/{}", vendor_id, version.to_string(), name);
            if (!repo->get_file(source_subpath, target_path, process_status)) {
                std::string msg = fmt::format(
                    "{}: Failed to get file {}. Update has failed.",
                    std::string(__FUNCTION__), source_subpath);
                SPDLOG_ERROR(msg); process_status->set_warning(msg, PresetUpdaterReason::DataUnreadable); return false;
            }
        }
    }

    // Verify what arrived. The download itself does not check hashes.
    for (const auto& [name, hash] : files_to_download) {
        const fs::path target_path(staging_dir / name);

        if (!fs::exists(target_path, ec) || ec) {
            std::string msg = fmt::format(
                "{}: Failed to find file {}. Error: {}. Bundle is corrupted. Update has failed.",
                std::string(__FUNCTION__), target_path.string(), ec.message());
            SPDLOG_ERROR(msg); process_status->set_warning(msg, PresetUpdaterReason::DataCorrupted); return false;
        }

        if (PresetUpdaterFileHash downloaded = PresetUpdater::file_hash(target_path, process_status);
            !process_status->ignore_file_hash() && downloaded != hash)
        {
            std::string msg = fmt::format(
                "File {} has failed file hash test after download. File is probably corrupted.",
                target_path.string());
            SPDLOG_ERROR(msg); process_status->set_warning(msg, PresetUpdaterReason::DataCorrupted); return false;
        }
    }

    return true;
}

/**
 * @brief Whether every file the manifest names is available to install.
 *
 * An unchanged file is not downloaded again, so the already installed copy counts, as long as it
 * is the one the manifest describes.
 */
bool update_is_complete(
    const std::map<std::string, std::string>& files_in_version_manifest,
    const std::map<std::string, std::string>& files_staged,
    const fs::path& staging_dir,
    const fs::path& installed_dir,
    const std::string& vendor_id,
    PresetUpdaterProcessStatus* process_status
)
{
    if (files_in_version_manifest.empty()) {
        process_status->set_warning(
            fmt::format(
                "Version manifest of vendor {} names no file, so an unreadable manifest cannot be"
                " told apart from a complete one. Nothing was installed.",
                vendor_id
            )
        , PresetUpdaterReason::InstallFailed);
        return false;
    }

    boost::system::error_code ec;
    for (const auto& [name, hash] : files_in_version_manifest) {
        if (files_staged.contains(name) && fs::is_regular_file(staging_dir / name, ec) && !ec) {
            continue;
        }
        const fs::path installed_path = installed_dir / name;
        if (fs::is_regular_file(installed_path, ec)
            && !ec
            && (process_status->ignore_file_hash()
                || file_hash(installed_path, process_status) == hash))
        {
            continue;
        }
        process_status->set_warning(
            fmt::format(
                "Staged data of vendor {} is incomplete: {} is neither staged at {} nor already"
                " installed at the version the manifest names. Nothing was installed.",
                vendor_id,
                name,
                staging_dir.string()
            )
        , PresetUpdaterReason::InstallFailed);
        return false;
    }

    return true;
}

bool vendor_data_declares_version(
    const fs::path& vendor_yaml,
    const std::string& vendor_id,
    const Slic3r::Semver& target_version,
    PresetUpdaterProcessStatus* process_status
)
{
    boost::system::error_code ec;
    if (!fs::is_regular_file(vendor_yaml, ec) || ec) {
        return true;
    }

    Domain::Preset::VendorInfo info;
    try {
        info = Preset::IO::HwConfigLoader::load_info_only(vendor_yaml.string());
    } catch (const std::exception& e) {
        process_status->set_warning(
            fmt::format(
                "Failed to read the vendor data of {} at {}: {}. Nothing was installed.",
                vendor_id, vendor_yaml.string(), e.what()
            )
        , PresetUpdaterReason::DataInconsistent);
        return false;
    }

    const boost::optional<Slic3r::Semver> declared = Slic3r::Semver::parse(info.version);
    if (!declared) {
        process_status->set_warning(
            fmt::format(
                "Vendor data of {} failed its sanity check: its declared version \"{}\" cannot be"
                " parsed. Nothing was installed.",
                vendor_id, info.version
            )
        , PresetUpdaterReason::DataInconsistent);
        return false;
    }
    if (*declared != target_version) {
        process_status->set_warning(
            fmt::format(
                "Vendor data of {} failed its sanity check: it declares version {}, not the {} its"
                " index recommends. Nothing was installed.",
                vendor_id, declared->to_string(), target_version.to_string()
            )
        , PresetUpdaterReason::DataInconsistent);
        return false;
    }

    return true;
}

/**
 * @brief Removes installed files the new version does not have, so nothing stale survives it.
 */
bool prune_installed(
    const fs::path& installed_dir,
    const std::map<std::string, std::string>& keep,
    PresetUpdaterProcessStatus* process_status
)
{
    boost::system::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(installed_dir, ec)) {
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }
        const fs::path& path = entry.path();
        if (keep.contains(fs::relative(path, installed_dir).generic_string())) {
            continue;
        }
        if (!fs::remove(path, ec) || ec) {
            process_status->set_warning(
                fmt::format("Failed to remove file {}: {}", path.string(), ec.message())
            , PresetUpdaterReason::InstallFailed);
            return false;
        }
    }
    return true;
}

/**
 * @brief Loads the index a repository has in resources, if it ships one for this vendor.
 */
bool load_resources_index(
    const std::string& repo_id,
    const std::string& vendor_id,
    PresetUpdaterIndex& index
)
{
    const fs::path index_path = resources_repo_path(repo_id) / (vendor_id + ".idx");
    boost::system::error_code ec;
    if (!fs::is_regular_file(index_path, ec) || ec) {
        return false;
    }
    try {
        index.load(index_path);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

/**
 * @brief Stages one index file, replacing whatever was there.
 */
void stage_index(
    const fs::path& source,
    const std::string& repo_id,
    const std::string& vendor_id,
    PresetUpdaterProcessStatus* process_status,
    const char* func_name
)
{
    const fs::path target = update_sync_index_path(repo_id, vendor_id);
    if (!ensure_dir_exists(target.parent_path(), process_status, func_name)) {
        return;
    }
    if (!copy_file_wrapper(source, target, process_status)) {
        SPDLOG_ERROR("{}: Failed to stage index {}", func_name, source.string());
    }
}

} // namespace

void PresetUpdaterRepositorySync::sync_indices(
    const SharedRepositoryVector& repositories,
    bool online_allowed,
    PresetUpdaterProcessStatus* process_status
) const
{
    ASSERT(
        process_status,
        "Every step of the sync reports through it, so there is nothing sensible to do without one."
    );

    fs::path temp_dir;
    try {
        temp_dir = create_temp_dir();
    } catch (const Slic3r::RuntimeError& e) {
        process_status->set_error(std::string("Preset Archive Sync has failed. ") + e.what(), PresetUpdaterReason::DataDirUnusable);
        return;
    }

    for (const AbstractPresetUpdaterRepository* repo : repositories) {
        if (process_status->get_canceled()) {
            break;
        }
        if (!repo->is_selected()) {
            process_status->set_warning_target(repo->descriptor().id);
            remove_directory(update_sync_repo_path(repo->descriptor().id), process_status, __FUNCTION__);
            process_status->clear_warning_target();
        }
        stage_indices_from_resources(resources_repo_path(repo->descriptor().id), repo, process_status);
    }

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
        this->stage_indices_from_repository(temp_dir / repo->descriptor().id, repo, process_status);
        remove_empty_parents(update_sync_repo_path(repo->descriptor().id), update_sync_root());
    }

    remove_directory(temp_dir, process_status, __FUNCTION__);
}

void PresetUpdaterRepositorySync::stage_indices_from_resources(
    const boost::filesystem::path& source_dir,
    const AbstractPresetUpdaterRepository* repo,
    PresetUpdaterProcessStatus* process_status
) const
{
    boost::system::error_code ec;
    // Each repo has its own subdir. It does not have to be in resources.
    if (!fs::exists(source_dir, ec) || ec) {
        SPDLOG_INFO(
            "{} Directory does not exists {}. Skipping.",
            std::string(__FUNCTION__),
            source_dir.string()
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
        if (process_status->get_canceled()) {
            return;
        }
        process_status->set_warning_target(repo->descriptor().id, source_index.vendor());

        // What is staged already may come from the source, which can be ahead of resources.
        const fs::path staged = update_sync_index_path(repo->descriptor().id, source_index.vendor());
        if (fs::is_regular_file(staged, ec) && !ec) {
            PresetUpdaterIndex staged_index;
            try {
                staged_index.load(staged);
                const PresetUpdaterIndex::const_iterator staged_recommended = staged_index.recommended();
                const PresetUpdaterIndex::const_iterator source_recommended = source_index.recommended();
                if (staged_recommended != staged_index.end()
                    && (source_recommended == source_index.end()
                        || staged_recommended->config_version >= source_recommended->config_version))
                {
                    continue;
                }
            } catch (const std::runtime_error&) {
                // Unreadable, so it is worth no more than the one in resources.
            }
        }

        stage_index(source_index.path(), repo->descriptor().id, source_index.vendor(), process_status, __FUNCTION__);
    }
    process_status->clear_warning_target();
}

void PresetUpdaterRepositorySync::stage_indices_from_repository(
    const boost::filesystem::path& temp_dir,
    const AbstractPresetUpdaterRepository* repo,
    PresetUpdaterProcessStatus* process_status
) const
{
    boost::system::error_code ec;

    if (!fs::create_directories(temp_dir, ec) && ec) {
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

    if (!unzip_files_from_bundle_zip(archive_path, temp_dir, process_status)) {
        std::string msg = fmt::format(
            "Failed to check updates for source {}. Couldn't open zipped bundle.",
            repo->descriptor().id
        );
        SPDLOG_ERROR(msg);
        warn_about_repo(process_status, repo->descriptor().id, msg);
        return;
    }

    std::vector<PresetUpdaterIndex> index_db;
    try {
        index_db = load_vendors_db(temp_dir);
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

    for (const PresetUpdaterIndex& index : index_db) {
        if (process_status->get_canceled()) {
            return;
        }
        process_status->set_warning_target(repo->descriptor().id, index.vendor());
        stage_index(index.path(), repo->descriptor().id, index.vendor(), process_status, __FUNCTION__);
    }
    process_status->clear_warning_target();
}

bool PresetUpdaterRepositorySync::install_vendor(
    const AbstractPresetUpdaterRepository* repo,
    const std::string& vendor_id,
    const Slic3r::Semver& target_version,
    bool online_allowed,
    const StagingArea& staging,
    PresetUpdaterProcessStatus* process_status
) const
{
    ASSERT(repo, "Without the source there is nothing to download from.");
    ASSERT(
        !vendor_id.empty(),
        "Every path below is built from this name. An empty one addresses the source directory"
        " instead of the vendor directory, and the install wipes it."
    );

    PresetUpdaterIndex resources_index;
    if (load_resources_index(repo->descriptor().id, vendor_id, resources_index)) {
        const PresetUpdaterIndex::const_iterator recommended = resources_index.recommended();
        if (recommended != resources_index.end() && recommended->config_version == target_version) {
            return install_from_resources(repo, vendor_id, target_version, process_status);
        }
    }

    if (repo->is_online() && !online_allowed) {
        process_status->set_warning(
            fmt::format(
                "Preset updates over the internet are turned off, so {} cannot be downloaded.",
                vendor_id
            )
        , PresetUpdaterReason::SourceUnreachable);
        return false;
    }

    process_status->set_step(PresetUpdaterStep::Downloading, vendor_id, target_version.to_string());

    const fs::path staging_dir =
        staging.vendor_path(repo->descriptor().id, vendor_id, target_version.to_string());
    const bool installed =
        download_and_install(repo, vendor_id, target_version, staging_dir, process_status);
    remove_directory(staging_dir.parent_path(), process_status, __FUNCTION__);
    return installed;
}

bool PresetUpdaterRepositorySync::install_from_resources(
    const AbstractPresetUpdaterRepository* repo,
    const std::string& vendor_id,
    const Slic3r::Semver& target_version,
    PresetUpdaterProcessStatus* process_status
) const
{
    process_status->set_step(PresetUpdaterStep::Installing, vendor_id, target_version.to_string());

    const fs::path source_dir    = resources_repo_path(repo->descriptor().id) / vendor_id;
    const fs::path installed_dir = local_vendor_path(repo->descriptor().id, vendor_id);
    const fs::path installed_idx = local_vendor_path(repo->descriptor().id, vendor_id, ".idx");

    boost::system::error_code ec;
    if (!fs::is_directory(source_dir, ec) || ec) {
        process_status->set_warning(
            fmt::format("Vendor {} is not in resources at {}.", vendor_id, source_dir.string())
        , PresetUpdaterReason::InstallFailed);
        return false;
    }

    std::map<std::string, std::string> shipped;
    for (const auto& entry : fs::recursive_directory_iterator(source_dir, ec)) {
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }
        shipped.emplace(fs::relative(entry.path(), source_dir).generic_string(), std::string{});
    }
    if (shipped.empty()) {
        process_status->set_warning(
            fmt::format("Vendor {} has no data in resources.", vendor_id)
        , PresetUpdaterReason::InstallFailed);
        return false;
    }

    if (!vendor_data_declares_version(
            source_dir / "vendor.yaml", vendor_id, target_version, process_status
        ))
    {
        return false;
    }

    if (!ensure_dir_exists(installed_dir, process_status, __FUNCTION__)) {
        return false;
    }
    if (!prune_installed(installed_dir, shipped, process_status)) {
        return false;
    }

    for (const auto& [name, unused] : shipped) {
        const fs::path target = installed_dir / name;
        if (!ensure_dir_exists(target.parent_path(), process_status, __FUNCTION__)) {
            return false;
        }
        if (!copy_file_wrapper(source_dir / name, target, process_status)) {
            return false;
        }
    }

    const fs::path staged_idx = update_sync_index_path(repo->descriptor().id, vendor_id);
    if (fs::is_regular_file(staged_idx, ec) && !ec) {
        move_file(staged_idx, installed_idx, process_status, __FUNCTION__);
    } else {
        copy_file_wrapper(
            resources_repo_path(repo->descriptor().id) / (vendor_id + ".idx"),
            installed_idx,
            process_status
        );
    }
    remove_empty_parents(staged_idx.parent_path(), update_sync_root());

    return true;
}

bool PresetUpdaterRepositorySync::download_and_install(
    const AbstractPresetUpdaterRepository* repo,
    const std::string& vendor_id,
    const Slic3r::Semver& target_version,
    const fs::path& staging_dir,
    PresetUpdaterProcessStatus* process_status
) const
{
    const std::string repo_id    = repo->descriptor().id;
    const fs::path installed_dir = local_vendor_path(repo_id, vendor_id);
    const fs::path installed_idx = local_vendor_path(repo_id, vendor_id, ".idx");
    const fs::path manifest_path = staging_dir.parent_path() / (target_version.to_string() + ".manifest");
    const fs::path staged_idx    = update_sync_index_path(repo_id, vendor_id);

    boost::system::error_code ec;

    if (!fs::is_regular_file(staged_idx, ec) || ec) {
        process_status->set_warning(
            fmt::format(
                "Staged index of vendor {} is not at {} anymore. Nothing was installed.",
                vendor_id,
                staged_idx.string()
            )
        , PresetUpdaterReason::InstallFailed);
        return false;
    }

    if (!ensure_dir_exists(staging_dir, process_status, __FUNCTION__)) {
        return false;
    }

    if (!download_version_manifest(repo, vendor_id, target_version, manifest_path, process_status, __FUNCTION__)) {
        return false;
    }

    const std::map<std::string, std::string> files_in_version_manifest =
        read_version_manifest(manifest_path, process_status);
    SPDLOG_INFO("{}/{}: {} files", repo_id, vendor_id, files_in_version_manifest.size());

    if (files_in_version_manifest.empty()) {
        process_status->set_warning(
            fmt::format("Version manifest of vendor {} could not be read.", vendor_id)
        , PresetUpdaterReason::DataCorrupted);
        return false;
    }

    // An unchanged file is already on disk, so only what differs is worth fetching.
    const std::map<std::string, PresetUpdaterFileHash> files_in_installed =
        get_directory_file_hashes(installed_dir, process_status, __FUNCTION__);
    std::map<std::string, std::string> files_wanted;
    for (const auto& [name, hash] : files_in_version_manifest) {
        auto it = files_in_installed.find(name);
        if (it == files_in_installed.end() || (!process_status->ignore_file_hash() && it->second != hash)) {
            files_wanted.emplace(name, hash);
        }
    }

    if (!download_vendor_files(
            files_wanted, repo, vendor_id, target_version, staging_dir, process_status
        ))
    {
        return false;
    }

    if (process_status->get_canceled()) {
        return false;
    }

    process_status->set_step(PresetUpdaterStep::Installing, vendor_id, target_version.to_string());

    // Nothing installed is deleted before the new version is known to be complete.
    if (!update_is_complete(
            files_in_version_manifest, files_wanted, staging_dir, installed_dir, vendor_id, process_status
        ))
    {
        return false;
    }

    const fs::path staged_vendor_yaml = staging_dir / "vendor.yaml";
    const bool vendor_yaml_staged     = files_wanted.contains("vendor.yaml");
    if (!vendor_data_declares_version(
            vendor_yaml_staged ? staged_vendor_yaml : installed_dir / "vendor.yaml",
            vendor_id,
            target_version,
            process_status
        ))
    {
        return false;
    }

    if (!ensure_dir_exists(installed_dir, process_status, __FUNCTION__)) {
        return false;
    }
    if (!prune_installed(installed_dir, files_in_version_manifest, process_status)) {
        return false;
    }

    for (const auto& [name, hash] : files_wanted) {
        const fs::path target = installed_dir / name;
        if (!ensure_dir_exists(target.parent_path(), process_status, __FUNCTION__)) {
            return false;
        }
        if (!move_file(staging_dir / name, target, process_status, __FUNCTION__)) {
            return false;
        }
    }

    if (!move_file(staged_idx, installed_idx, process_status, __FUNCTION__)) {
        return false;
    }

    remove_empty_parents(staged_idx.parent_path(), update_sync_root());

    return true;
}

} // namespace Slic3r::Biz::PresetUpdater
