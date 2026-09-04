#include "PresetUpdaterUtils.hpp"
#include "PresetUpdaterFileHash.hpp"
#include "PresetUpdaterIndex.hpp"
#include "PresetUpdaterProcessStatus.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterReconfigurationList.hpp"
#include "Slic3r/Biz/Utils/CopyFile.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Biz/Preset/IO/HwConfigLoader.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterRepository.hpp"

#include "Slic3r/Exception.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"

#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/system/error_code.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/cstdio.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <type_traits>

namespace fs = boost::filesystem;

namespace Slic3r::Biz::PresetUpdater {

boost::filesystem::path local_presets_path()
{
    return fs::path(data_dir()) / "presets" / "local";
}

boost::filesystem::path local_vendor_path(
    const std::string& repo_id,
    const std::string& vendor_id,
    const std::string& suffix
)
{
    return local_presets_path() / repo_id / (vendor_id + suffix);
}

FileOpResult safe_move(const boost::filesystem::path& source, const boost::filesystem::path& target)
{
    std::string src, tgt, error_message;
    src = source.string();
    tgt = target.string();
    auto result = Utils::copy_file(src, tgt, error_message);
    if (result == Utils::Success) {
        boost::nowide::remove(src.c_str());
    } else {
        return tl::unexpected(error_message);
    }
    return {};
}

bool move_directory_contents(
    const fs::path& source_dir, 
    const fs::path& dest_dir, 
    PresetUpdaterProcessStatus* process_status,
    const char* caller_name) 
{
    boost::system::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(source_dir, ec)) {
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }
        const fs::path& source_path = entry.path();
        fs::path relative_path      = fs::relative(source_path, source_dir);
        fs::path dest_path          = dest_dir / relative_path;

        if (!fs::create_directories(dest_path.parent_path(), ec) && ec) {
            process_status->set_warning(
                fmt::format(
                    "Failed to create target directory {}: {}. Staging update has failed.",
                    dest_path.parent_path().string(),
                    ec.message()
                )
            , PresetUpdaterReason::LocalStorageFailed);
            return false;
        }

        auto result = safe_move(source_path, dest_path);
        if (!result) {
            process_status->set_warning(
                fmt::format(
                    "Failed to move file {} to {}: {}",
                    source_path.string(),
                    dest_path.string(),
                    result.error()
                )
            , PresetUpdaterReason::LocalStorageFailed);
            return false;
        }
    }
    
    if (ec) {
        std::string msg = fmt::format(
            "{}: Error traversing directory {}: {}",
            caller_name,
            source_dir.string(),
            ec.message()
        );
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::LocalStorageFailed);
    }
    
    return true;
}

bool copy_file_wrapper(const fs::path& source, const fs::path& target, PresetUpdaterProcessStatus* process_status)
{
    SPDLOG_INFO("PresetUpdater: Copying {} -> {}", source.string(), target.string());
    std::string error_message;
    Biz::Utils::CopyFileResult
        cfr = Biz::Utils::copy_file(source.string(), target.string(), error_message, false);
    if (cfr != Biz::Utils::CopyFileResult::Success) {
        std::string msg = fmt::format(
            "Copying of file {} to {} failed: {}",
            source.string(),
            target.string(),
            error_message
        );
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::LocalStorageFailed);
        return false;
    }
    // Permissions should be copied from the source file by copy_file(). We are not sure about the source
    // permissions, let's rewrite them with 644.
    static constexpr const auto perms = fs::owner_read
        | fs::owner_write
        | fs::group_read
        | fs::others_read;
    fs::permissions(target, perms);
    return true;
}

/// Load all idx from single directory
std::vector<PresetUpdaterIndex> load_vendors_db(const fs::path& archive_path)
{
    std::vector<PresetUpdaterIndex> index_db;
    std::string errors_cumulative;
    boost::system::error_code ec;
    if (!fs::exists(archive_path, ec) || ec) {
        throw Slic3r::RuntimeError(archive_path.string() + " does not exists. " + ec.message());
    }
    if (!fs::is_directory(archive_path, ec) || ec) {
        throw Slic3r::RuntimeError(archive_path.string() + " is not directory. " + ec.message());
    }
    for (auto& dir_entry : fs::directory_iterator(archive_path, ec)) {
        if (is_idx_file(dir_entry)) {
            PresetUpdaterIndex idx;
            try {
                idx.load(dir_entry.path());
            } catch (const std::runtime_error& err) {
                errors_cumulative += err.what();
                errors_cumulative += "\n";
                continue;
            }
            if (std::find_if(
                    index_db.begin(),
                    index_db.end(),
                    [idx](const PresetUpdaterIndex& index) { return idx.vendor() == index.vendor(); }
                )
                == index_db.end())
                index_db.emplace_back(std::move(idx));
        }
    }
    if (!errors_cumulative.empty())
        throw Slic3r::RuntimeError(errors_cumulative);
    return index_db;
}

std::vector<PresetUpdaterIndex> load_vendors_db_filtered(
    const boost::filesystem::path& from_path,
    const std::vector<std::string>& filter
)
{
    // We want only indicies of vendors from filter vector
    // Rest of the idx files are vendors of different archive
    std::vector<PresetUpdaterIndex> index_db;
    std::string errors_cumulative;
    boost::system::error_code ec;
    if (!fs::exists(from_path, ec) || ec) {
        throw Slic3r::RuntimeError(from_path.string() + " does not exists. " + ec.message());
    }
    if (!fs::is_directory(from_path, ec) || ec) {
        throw Slic3r::RuntimeError(from_path.string() + " is not directory. " + ec.message());
    }

    fs::directory_iterator source_directory_iterator(from_path, ec);
    // PresetUpdaterIndex work with dir entries, so we iterate every time
    for (auto& dir_entry : source_directory_iterator) {
        if (is_idx_file(dir_entry)) {
            if (std::find(filter.begin(), filter.end(), dir_entry.path().filename().string())
                == filter.end())
            {
                // not an index from this archive
                continue;
            }
            PresetUpdaterIndex idx;
            try {
                idx.load(dir_entry.path());
            } catch (const std::runtime_error& err) {
                errors_cumulative += err.what();
                errors_cumulative += "\n";
                continue;
            }
            if (std::find_if(
                    index_db.begin(),
                    index_db.end(),
                    [idx](const PresetUpdaterIndex& index) { return idx.vendor() == index.vendor(); }
                )
                == index_db.end())
            {
                index_db.emplace_back(std::move(idx));
            } else {
                SPDLOG_ERROR(
                    "Index {} names vendor {}, which another index in {} already names. Skipping it.",
                    dir_entry.path().string(),
                    idx.vendor(),
                    from_path.string()
                );
            }
        }
    }
    if (!errors_cumulative.empty())
        throw Slic3r::RuntimeError(errors_cumulative);
    return index_db;
}

std::map<std::string, std::string> read_version_manifest(
    const fs::path& path,
    PresetUpdaterProcessStatus* process_status
)
{
    std::map<std::string, std::string> manifest_data;
    boost::nowide::ifstream file(path);

    if (!file.is_open()) {
        std::string msg = fmt::format("Failed to read version manifest {}", path.string());
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::DataUnreadable);
        return {};
    }
    nlohmann::json data;
    try {
        // Parse the JSON directly from the file stream
        file >> data;
    } catch (nlohmann::json::parse_error& e) {
        std::string msg = fmt::format("Failed to read version manifest {}. {}", path.string(), e.what());
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::DataCorrupted);
        return {};
    }

    if (!data.is_array()) {
        std::string msg = fmt::format(
            "Failed to read version manifest {}. Unexpected json formating.",
            path.string()
        );
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::DataCorrupted);
        return {};
    }

    for (const auto& item : data) {
        // Ensure the element is an object and contains the required keys
        if (item.is_object()
            && item.contains("filename")
            && item.contains("filehash")
            && item["filename"].is_string()
            && item["filehash"].is_string())
        {
            std::string filename    = item.at("filename").get<std::string>();
            std::string filehash    = item.at("filehash").get<std::string>();
            manifest_data[filename] = filehash;
        } else {
            std::string msg = fmt::format(
                "Failed to read version manifest {}. Uexpected json formating.",
                path.string()
            );
            SPDLOG_ERROR(msg);
            process_status->set_warning(msg, PresetUpdaterReason::DataCorrupted);
            return {};
        }
    }
    return manifest_data;
}

namespace {

void perform_removals(
    const std::vector<VendorReconfiguration>& removals,
    PresetUpdaterProcessStatus* process_status
)
{
    for (const VendorReconfiguration& removal : removals) {
        ASSERT(
            !removal.vendor_id.empty() && !removal.vendor_repo_id.empty(),
            "Both paths below are built from these two names. An empty one addresses the presets"
            " directory itself, and the removal wipes every installed vendor."
        );
        SPDLOG_INFO("Deleting unusable bundle {}", removal.vendor_id);

        const fs::path dir_path = local_vendor_path(removal.vendor_repo_id, removal.vendor_id);
        const fs::path idx_path =
            local_vendor_path(removal.vendor_repo_id, removal.vendor_id, ".idx");

        // Already gone is the wanted state, so only a real failure is worth reporting.
        boost::system::error_code ec;
        process_status->set_warning_target(removal.vendor_repo_id, removal.vendor_id);
        fs::remove_all(dir_path, ec);
        if (ec) {
            process_status->set_warning(
                fmt::format("Failed to remove directory {}. {}", dir_path.string(), ec.message())
            , PresetUpdaterReason::LocalStorageFailed);
            process_status->clear_warning_target();
            continue;
        }
        fs::remove(idx_path, ec);
        if (ec) {
            process_status->set_warning(
                fmt::format("Failed to remove index {}. {}", idx_path.string(), ec.message())
            , PresetUpdaterReason::LocalStorageFailed);
        }
        process_status->clear_warning_target();
    }
}

bool whole_repository_failed(
    const PresetUpdaterProcessStatus* process_status, const std::string& repo_id
)
{
    return process_status->has_vendor_warning(repo_id, {});
}

void collect_removal_of_unusable_vendor(
    const fs::path& installed_repo_dir,
    const std::string& repo_id,
    const PresetUpdaterIndex& index,
    PresetUpdaterReconfigurationList& results,
    PresetUpdaterProcessStatus* process_status
)
{
    const fs::path installed_vendor_yaml_path = installed_repo_dir / index.vendor() / "vendor.yaml";
    boost::system::error_code ec;
    if (!fs::exists(installed_vendor_yaml_path, ec) || ec) {
        return;
    }

    Preset::IO::HwConfigLoader hw_config_loader;
    Domain::Preset::VendorData vendor_data;
    try {
        vendor_data = hw_config_loader.load(installed_vendor_yaml_path.string());
    } catch (const std::exception& e) {
        process_status->set_warning_target(repo_id, index.vendor());
        process_status->set_warning(
            fmt::format(
                "Failed to load vendor file {}: {}",
                installed_vendor_yaml_path.string(),
                e.what()
            )
        , PresetUpdaterReason::DataInconsistent);
        process_status->clear_warning_target();
        return;
    }

    const Semver current_version = Semver(vendor_data.info.version);
    const PresetUpdaterIndex::const_iterator installed = index.find(current_version);
    if (installed == index.end() || installed->is_current_slic3r_supported()) {
        return;
    }

    SPDLOG_WARN(
        "No source offers a usable version of {}. Offering removal of {}.",
        index.vendor(),
        installed_vendor_yaml_path.string()
    );
    results.emplace_back(
        VendorReconfigurationState::RemoveVendor,
        vendor_data.info.id,
        repo_id,
        current_version,
        Slic3r::Semver(),
        std::string{}
    );
}

void perform_update_no_source_manifest(
    const VendorReconfiguration& update,
    PresetUpdaterProcessStatus* process_status
)
{
    boost::system::error_code ec;

    const fs::path update_sync_path   = fs::path(data_dir()) / "update_sync";

    const fs::path vendor_dest_dir_path =
        local_vendor_path(update.vendor_repo_id, update.vendor_id);
    const fs::path vendor_dest_idx_path =
        local_vendor_path(update.vendor_repo_id, update.vendor_id, ".idx");

    const fs::path vendor_source_dir_path = update_sync_path / update.vendor_repo_id / update.vendor_id;
    const fs::path vendor_source_idx_path = update_sync_path
        / update.vendor_repo_id
        / (update.vendor_id + ".idx");
    const fs::path vendor_source_manifest_path = vendor_source_dir_path
        / (update.vendor_id + ".manifest");

    if (!fs::is_directory(vendor_source_dir_path, ec)
        || !fs::is_regular_file(vendor_source_idx_path, ec))
    {
        process_status->set_warning(
            fmt::format(
                "Staged data of vendor {} is not at {} anymore. Nothing was installed.",
                update.vendor_id,
                vendor_source_dir_path.string()
            )
        , PresetUpdaterReason::InstallFailed);
        return;
    }

    process_status->set_install_target(update.vendor_id);

    if (!fs::create_directories(vendor_dest_dir_path, ec) && ec) {
        process_status->set_warning(
            fmt::format("Failed to create directory {}: {}", vendor_dest_dir_path.string(), ec.message())
        , PresetUpdaterReason::InstallFailed);
        return;
    }

    // Delete all files in source
    for (const auto& entry : fs::directory_iterator(vendor_dest_dir_path, ec)) {
        const fs::path& path = entry.path();
        SPDLOG_INFO("Deleting {}", path.string());
        if (!fs::remove_all(path, ec) || ec) {
            process_status->set_warning(
                fmt::format("Failed to remove file {}: {}", path.string(), ec.message())
            , PresetUpdaterReason::InstallFailed);
            return;
        }
    }
    if (ec) {
        std::string msg = fmt::format(
            "{}: Error traversing directory {}: {}",
            std::string(__FUNCTION__),
            vendor_dest_dir_path.string(),
            ec.message()
        );
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::InstallFailed);
    }

    // move alle files from source
    if (!move_directory_contents(vendor_source_dir_path, vendor_dest_dir_path, process_status, __FUNCTION__)) {
        return;
    }

    // move index
    auto result = safe_move(vendor_source_idx_path, vendor_dest_idx_path);
    if (!result) {
        process_status->set_warning(
            fmt::format(
                "Failed to move file {} to {}: {}",
                vendor_source_idx_path.string(),
                vendor_dest_idx_path.string(),
                result.error()
            )
        , PresetUpdaterReason::InstallFailed);
        return;
    }

    // cleanup source
    if (!fs::remove_all(vendor_source_dir_path, ec) || ec) {
        process_status->set_warning(
            fmt::format("Failed to remove {}: {}", vendor_source_dir_path.string(), ec.message())
        , PresetUpdaterReason::InstallFailed);
        return;
    }
}

bool staged_update_is_complete(
    const std::map<std::string, std::string>& files_in_version_manifest,
    const fs::path& vendor_source_dir_path,
    const fs::path& vendor_dest_dir_path,
    PresetUpdaterProcessStatus* process_status
)
{
    if (files_in_version_manifest.empty()) {
        process_status->set_warning(
            fmt::format(
                "Version manifest of vendor {} names no file, so an unreadable manifest cannot be"
                " told apart from a complete one. Nothing was installed.",
                vendor_source_dir_path.filename().string()
            )
        , PresetUpdaterReason::InstallFailed);
        return false;
    }

    boost::system::error_code ec;
    for (const auto& [name, hash] : files_in_version_manifest) {
        if (fs::is_regular_file(vendor_source_dir_path / name, ec) && !ec) {
            continue;
        }
        const fs::path installed_path = vendor_dest_dir_path / name;
        if (fs::is_regular_file(installed_path, ec)
            && !ec
            && file_hash(installed_path, process_status) == hash)
        {
            continue;
        }
        process_status->set_warning(
            fmt::format(
                "Staged data of vendor {} is incomplete: {} is neither staged at {} nor already"
                " installed at the version the manifest names. Nothing was installed.",
                vendor_source_dir_path.filename().string(),
                name,
                vendor_source_dir_path.string()
            )
        , PresetUpdaterReason::InstallFailed);
        return false;
    }

    return true;
}

void perform_update_with_source_manifest(
    const VendorReconfiguration& update,
    PresetUpdaterProcessStatus* process_status
)
{
    boost::system::error_code ec;

    const fs::path profile_local_path = local_presets_path();
    const fs::path update_sync_path   = fs::path(data_dir()) / "update_sync";
    
    const fs::path repo_dest_dir_path = profile_local_path / update.vendor_repo_id;
    const fs::path vendor_dest_dir_path = repo_dest_dir_path / update.vendor_id;
    const fs::path vendor_dest_idx_path = repo_dest_dir_path / (update.vendor_id + ".idx");

    const fs::path vendor_source_dir_path = update_sync_path / update.vendor_repo_id / update.vendor_id;
    const fs::path vendor_source_idx_path = update_sync_path
        / update.vendor_repo_id
        / (update.vendor_id + ".idx");
    const fs::path vendor_source_manifest_path = vendor_source_dir_path
        / (update.vendor_id + ".manifest");

    if (!fs::is_directory(vendor_source_dir_path, ec)
        || !fs::is_regular_file(vendor_source_idx_path, ec)
        || !fs::is_regular_file(vendor_source_manifest_path, ec))
    {
        process_status->set_warning(
            fmt::format(
                "Staged data of vendor {} is not at {} anymore. Nothing was installed.",
                update.vendor_id,
                vendor_source_dir_path.string()
            )
        , PresetUpdaterReason::InstallFailed);
        return;
    }

    process_status->set_install_target(update.vendor_id);

    // Read manifest
    std::map<std::string, std::string> files_in_version_manifest = read_version_manifest(
        vendor_source_manifest_path,
        process_status
    );

    // Nothing installed is deleted before the new version is known to be complete.
    if (!staged_update_is_complete(
            files_in_version_manifest, vendor_source_dir_path, vendor_dest_dir_path, process_status
        ))
    {
        return;
    }

    // in case of new vendor, repo_dest_dir_path needs to be created
    if (!fs::create_directory(repo_dest_dir_path, ec) && ec) {
        process_status->set_warning(
            fmt::format("Failed to create directory {}: {}", repo_dest_dir_path.string(), ec.message())
        , PresetUpdaterReason::InstallFailed);
        return;
    }

    if (!fs::create_directory(vendor_dest_dir_path, ec) && ec) {
        process_status->set_warning(
            fmt::format("Failed to create directory {}: {}", vendor_dest_dir_path.string(), ec.message())
        , PresetUpdaterReason::InstallFailed);
        return;
    }

    // Delete files that are not in manifest
    for (const auto& entry : fs::recursive_directory_iterator(vendor_dest_dir_path, ec)) {
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }
        const fs::path& path = entry.path();
        if (files_in_version_manifest.find(fs::relative(path, vendor_dest_dir_path).generic_string())
            == files_in_version_manifest.end())
        {
            if (!fs::remove(path, ec) || ec) {
                process_status->set_warning(
                    fmt::format("Failed to remove file {}: {}", path.string(), ec.message())
                , PresetUpdaterReason::InstallFailed);
                return;
            }
        }
    }
    if (ec) {
        std::string msg = fmt::format(
            "{}: Error traversing directory {}: {}",
            std::string(__FUNCTION__),
            vendor_dest_dir_path.string(),
            ec.message()
        );
        SPDLOG_ERROR(msg);
        process_status->set_warning(msg, PresetUpdaterReason::InstallFailed);
    }

    // move files that are staged
    if (!move_directory_contents(vendor_source_dir_path, vendor_dest_dir_path, process_status, __FUNCTION__)) {
        return;
    }

    // move index
    auto result = safe_move(vendor_source_idx_path, vendor_dest_idx_path);
    if (!result) {
        process_status->set_warning(
            fmt::format(
                "Failed to move file {} to {}: {}",
                vendor_source_idx_path.string(),
                vendor_dest_idx_path.string(),
                result.error()
            )
        , PresetUpdaterReason::InstallFailed);
        return;
    }

    // cleanup source
    if (!fs::remove_all(vendor_source_dir_path, ec) || ec) {
        process_status->set_warning(
            fmt::format("Failed to remove {}: {}", vendor_source_dir_path.string(), ec.message())
        , PresetUpdaterReason::InstallFailed);
        return;
    }
}

void perform_updates(
    const std::vector<VendorReconfiguration>& updates,
    PresetUpdaterProcessStatus* process_status
)
{
    const fs::path update_sync_path = fs::path(data_dir()) / "update_sync";
    boost::system::error_code ec;
    for (const VendorReconfiguration& update : updates) {
        ASSERT(
            !update.vendor_id.empty() && !update.vendor_repo_id.empty(),
            "Every path below is built from these two names. An empty one addresses the source"
            " directory instead of the vendor directory, and the install wipes it."
        );
        SPDLOG_INFO("Updating bundle {}", update.vendor_id);

        const fs::path vendor_source_manifest_path = update_sync_path
            / update.vendor_repo_id
            / update.vendor_id
            / (update.vendor_id + ".manifest");

        process_status->set_warning_target(update.vendor_repo_id, update.vendor_id);
        if (fs::exists(vendor_source_manifest_path, ec)
            && !ec
            && fs::is_regular_file(vendor_source_manifest_path, ec)
            && !ec)
        {
            perform_update_with_source_manifest(update, process_status);
        } else {
            perform_update_no_source_manifest(update, process_status);
        }
        process_status->clear_warning_target();
        // cleanup the repo dir
        const fs::path repo_path = update_sync_path / update.vendor_repo_id;
        if (fs::is_empty(repo_path, ec) && !ec) {
            fs::remove(repo_path, ec);
        }
    }
}

} // namespace

PresetUpdaterReconfigurationList check_forced_reconfigurations(
    const std::set<std::string>& known_repo_ids,
    PresetUpdaterProcessStatus* process_status
)
{
    PresetUpdaterReconfigurationList results;
    const fs::path profile_local_path = local_presets_path();
    boost::system::error_code ec;

    if (!fs::exists(profile_local_path, ec) || ec) {
        process_status->set_error(profile_local_path.string() + " does not exists. " + ec.message(), PresetUpdaterReason::DataDirUnusable);
        return {};
    }
    if (!fs::is_directory(profile_local_path, ec) || ec) {
        process_status->set_error(profile_local_path.string() + " is not directory. " + ec.message(), PresetUpdaterReason::DataDirUnusable);
        return {};
    }

    // vendors are inside repo_id dir
    for (auto& archive_dir : fs::directory_iterator(profile_local_path, ec)) {
        if (process_status->get_canceled()) {
            return {};
        }
        if (!fs::is_directory(archive_dir.path(), ec) || ec) {
            continue;
        }
        const std::string repo_id = archive_dir.path().filename().string();

        if (!known_repo_ids.contains(repo_id)) {
            SPDLOG_INFO(
                "No source offers {}, so its installed presets cannot be reconfigured. Skipping.",
                repo_id
            );
            continue;
        }

        std::vector<PresetUpdaterIndex> index_db;
        try {
            index_db = load_vendors_db(archive_dir.path());
        } catch (const std::exception& e) {
            std::string msg = fmt::format("Loading index db of {} has failed {}", repo_id, e.what());
            SPDLOG_ERROR(msg);
            process_status->set_warning_target(repo_id);
            process_status->set_warning(msg, PresetUpdaterReason::IndexUnreadable);
            process_status->clear_warning_target();
            continue;
        }
        for (const auto& index : index_db) {
            const fs::path installed_vendor_yaml_path = archive_dir.path()
                / index.vendor()
                / "vendor.yaml";

            if (!fs::exists(installed_vendor_yaml_path, ec) || ec) {
                process_status->set_warning_target(repo_id, index.vendor());
                process_status->set_warning(
                    fmt::format(
                        "File does not exists {}. {}",
                        installed_vendor_yaml_path.string(),
                        ec.message()
                    )
                , PresetUpdaterReason::DataUnreadable);
                process_status->clear_warning_target();
                continue;
            }

            // Current version
            Preset::IO::HwConfigLoader hw_config_loader;
            Domain::Preset::VendorData vendor_data;

            try {
                vendor_data = hw_config_loader.load(installed_vendor_yaml_path.string());
            } catch (const std::exception& e) {
                process_status->set_warning_target(repo_id, index.vendor());
                process_status->set_warning(
                    fmt::format(
                        "Failed to load vendor file {}: {}",
                        installed_vendor_yaml_path.string(),
                        e.what()
                    )
                , PresetUpdaterReason::DataInconsistent);
                process_status->clear_warning_target();
                continue;
            }
            Semver current_version = Semver(vendor_data.info.version);

            // Recommended version of vendor
            const PresetUpdaterIndex::const_iterator recommended = index.recommended();
            if (recommended == index.end()) {
                SPDLOG_ERROR(
                    "No recommended version for vendor: {}, Index file might be corrupted.",
                    index.vendor()
                );
                continue;
            }
            const PresetUpdaterIndex::const_iterator vendor_current_version_it = index.find(
                current_version
            );
            const bool ver_current_found = vendor_current_version_it != index.end();

            SPDLOG_INFO(
                "Vendor: {}, version installed: {}{}, recommended version: {}",
                vendor_data.info.name,
                current_version.to_string(),
                (ver_current_found ? "" : " (not found in index!)"),
                recommended->config_version.to_string()
            );

            if (!ver_current_found) {
                // Any published config shall be always found in the latest config index.
                SPDLOG_ERROR(
                    "Preset bundle `{}` version not found in index: {}",
                    index.vendor(),
                    current_version.to_string()
                );
                if (process_status->has_vendor_warning(repo_id, vendor_data.info.id)) {
                    SPDLOG_ERROR(
                        "Vendor {} of {} repository is not added to reconfigurations due to previous warnings.",
                        vendor_data.info.id,
                        repo_id
                    );
                    continue;
                }
                results.emplace_back(
                    VendorReconfigurationState::NotInIndex,
                    vendor_data.info.id,
                    repo_id,
                    Slic3r::Semver(),
                    recommended->config_version,
                    recommended->comment
                );
                continue;
            }

            if (vendor_current_version_it->is_current_slic3r_supported()) {
                // This slicer needs no forced reconfigurations
                SPDLOG_INFO(
                    "Preset bundle `{}` version {} needs no forced reconfiguration.",
                    index.vendor(),
                    current_version.to_string()
                );
                continue;
            }

            if (vendor_current_version_it->is_current_slic3r_downgrade()) {
                SPDLOG_WARN(
                    "Current Slic3r incompatible with installed bundle (forced downgrade): {}",
                    installed_vendor_yaml_path.string()
                );
                results.emplace_back(
                    VendorReconfigurationState::ForcedDowngrade,
                    vendor_data.info.id,
                    repo_id,
                    vendor_current_version_it->config_version,
                    recommended->config_version,
                    recommended->comment
                );
                continue;
            }
            SPDLOG_WARN(
                "Current Slic3r incompatible with installed bundle (forced update): {}",
                installed_vendor_yaml_path.string()
            );
            results.emplace_back(
                VendorReconfigurationState::ForcedUpdate,
                vendor_data.info.id,
                repo_id,
                vendor_current_version_it->config_version,
                recommended->config_version,
                recommended->comment
            );
        }
    }
    return results;
}

PresetUpdaterReconfigurationList check_reconfigurations(
    const SharedRepositoryVector& repos,
    PresetUpdaterProcessStatus* process_status
)
{
    // SPDLOG_INFO(__FUNCTION__);
    PresetUpdaterReconfigurationList results;
    const fs::path profile_local_path = local_presets_path();
    const fs::path update_sync_path   = fs::path(data_dir()) / "update_sync";
    boost::system::error_code ec;

    if (!fs::exists(profile_local_path, ec) || ec) {
        process_status->set_error(profile_local_path.string() + " does not exists. " + ec.message(), PresetUpdaterReason::DataDirUnusable);
        return {};
    }
    if (!fs::is_directory(profile_local_path, ec) || ec) {
        process_status->set_error(profile_local_path.string() + " is not directory. " + ec.message(), PresetUpdaterReason::DataDirUnusable);
        return {};
    }
    if (!fs::exists(update_sync_path, ec) || ec) {
        process_status->set_error(update_sync_path.string() + " does not exists. " + ec.message(), PresetUpdaterReason::DataDirUnusable);
        return {};
    }
    if (!fs::is_directory(update_sync_path, ec) || ec) {
        process_status->set_error(update_sync_path.string() + " is not directory. " + ec.message(), PresetUpdaterReason::DataDirUnusable);
        return {};
    }

    // vendors are inside repo_id dir
    for (auto& archive_dir : fs::directory_iterator(profile_local_path, ec)) {
        if (process_status->get_canceled()) {
            return {};
        }
        if (!fs::is_directory(archive_dir.path(), ec) || ec) {
            continue;
        }
        const std::string repo_id = archive_dir.path().filename().string();

        const auto repo_it = std::find_if(repos.begin(), repos.end(), [repo_id](const AbstractPresetUpdaterRepository* repo){ return repo_id == repo->descriptor().id;});
        if(repo_it == repos.end()){
            continue;
        }

        std::vector<PresetUpdaterIndex> index_db;
        try {
            index_db = load_vendors_db(archive_dir.path());
        } catch (const std::exception& e) {
            std::string msg = fmt::format("Loading index db of {} has failed {}", repo_id, e.what());
            SPDLOG_ERROR(msg);
            process_status->set_warning_target(repo_id);
            process_status->set_warning(msg, PresetUpdaterReason::IndexUnreadable);
            process_status->clear_warning_target();
            continue;
        }
        for (const auto& index : index_db) {
            // the installed bundles are in / "presets" / "local"
            // the most recent index file should be in / "update_sync"
            // then check installed bundles version and decide if and what type of reconfiguration

            // Compare update_sync index and index
            const fs::path update_sync_index_path = update_sync_path
                / archive_dir.path().filename()
                / index.path().filename();
            PresetUpdaterIndex update_sync_index;
            if (fs::exists(update_sync_index_path, ec) && !ec) {
                try {
                    update_sync_index.load(update_sync_index_path);
                } catch (const std::runtime_error&) {
                    process_status->set_warning_target(repo_id, index.vendor());
                    process_status->set_warning(
                        "Failed to load index " + update_sync_index_path.string()
                    , PresetUpdaterReason::IndexUnreadable);
                    process_status->clear_warning_target();
                    continue;
                }
            } else {
                if (!whole_repository_failed(process_status, repo_id)) {
                    collect_removal_of_unusable_vendor(
                        archive_dir.path(), repo_id, index, results, process_status
                    );
                }
                continue;
            }
            // use update_sync_index from now
            const fs::path installed_vendor_yaml_path = archive_dir.path()
                / index.vendor()
                / "vendor.yaml";

            // Current version
            Preset::IO::HwConfigLoader hw_config_loader;
            Domain::Preset::VendorData vendor_data;
            try {
                vendor_data = hw_config_loader.load(installed_vendor_yaml_path.string());
            } catch (const std::exception& e) {
                process_status->set_warning_target(repo_id, index.vendor());
                process_status->set_warning(
                    fmt::format(
                        "Failed to load vendor file {}: {}",
                        installed_vendor_yaml_path.string(),
                        e.what()
                    )
                , PresetUpdaterReason::DataInconsistent);
                process_status->clear_warning_target();
                continue;
            }
            Semver current_version = Semver(vendor_data.info.version);

            // Recommended version of vendor
            const PresetUpdaterIndex::const_iterator recommended = update_sync_index.recommended();
            if (recommended == update_sync_index.end()) {
                std::string msg = fmt::format(
                    "No recommended version for vendor: {}, Index file might be corrupted.",
                    index.vendor()
                );
                SPDLOG_ERROR(msg);
                process_status->set_warning_target(repo_id, index.vendor());
                process_status->set_warning(msg, PresetUpdaterReason::NoUsableVersion);
                process_status->clear_warning_target();
                continue;
            }
            const PresetUpdaterIndex::const_iterator vendor_current_version_it = update_sync_index.find(
                current_version
            );
            const bool current_found = vendor_current_version_it != update_sync_index.end();
            SPDLOG_INFO(
                "Vendor: {}, version installed: {}{}, recommended version: {}",
                vendor_data.info.name,
                current_version.to_string(),
                (current_found ? "" : " (not found in index!)"),
                recommended->config_version.to_string()
            );
            if (!current_found) {
                SPDLOG_INFO(
                    "Current installed version for vendor: {} ({}) was not found in index %3%",
                    vendor_data.info.id,
                    current_version.to_string(),
                    update_sync_index.path().string()
                );

                // If the installed index does not support installed version, we have to downgrade
                const PresetUpdaterIndex::const_iterator installed_index_version_it = index.find(current_version);
                if (installed_index_version_it != index.end() && !installed_index_version_it->is_current_slic3r_supported()) {
                    results.emplace_back(
                        VendorReconfigurationState::ForcedDowngrade,
                        vendor_data.info.id,
                        repo_id,
                        current_version,
                        recommended->config_version,
                        recommended->comment
                    );
                } else {
                    results.emplace_back(
                        VendorReconfigurationState::NotInIndex,
                        vendor_data.info.id,
                        repo_id,
                        current_version,
                        recommended->config_version,
                        recommended->comment
                    );
                }
                continue;
            }
            if (vendor_current_version_it->is_current_slic3r_supported()) {
                // This slicer needs no forced reconfigurations
                if (current_version < recommended->config_version) {
                    // regular update
                    SPDLOG_INFO(
                        "Preset bundle `{}` version {} has update {}.",
                        index.vendor(),
                        current_version.to_string(),
                        recommended->config_version.to_string()
                    );
                    if (process_status->has_vendor_warning(repo_id, vendor_data.info.id)) {
                        SPDLOG_ERROR(
                            "Vendor {} of {} repository is not added to reconfigurations due to previous warnings.",
                            vendor_data.info.id,
                            repo_id
                        );
                        continue;
                    }
                    results.emplace_back(
                        VendorReconfigurationState::Update,
                        vendor_data.info.id,
                        repo_id,
                        vendor_current_version_it->config_version,
                        recommended->config_version,
                        recommended->comment
                    );
                }
                continue;
            }

            if (vendor_current_version_it->is_current_slic3r_downgrade()) {
                // Forced downgrade
                SPDLOG_WARN(
                    "Current Slic3r incompatible with installed bundle (forced downgrade): {}",
                    installed_vendor_yaml_path.string()
                );
                results.emplace_back(
                    VendorReconfigurationState::ForcedDowngrade,
                    vendor_data.info.id,
                    repo_id,
                    vendor_current_version_it->config_version,
                    recommended->config_version,
                    recommended->comment
                );
                continue;
            }
            // Not supported and not downgrade = forced update
            SPDLOG_WARN(
                "Current Slic3r incompatible with installed bundle (forced update): {}",
                installed_vendor_yaml_path.string()
            );
            results.emplace_back(
                VendorReconfigurationState::ForcedUpdate,
                vendor_data.info.id,
                repo_id,
                vendor_current_version_it->config_version.to_string(),
                recommended->config_version.to_string(),
                recommended->comment
            );
        }
    }

    // Find new vendors in vendor_sync - they are not in profiles/local/vendor 
    for (const AbstractPresetUpdaterRepository* repository : repos)
    {
        if (process_status->get_canceled()) {
            return {};
        }
        const std::string repo_id = repository->descriptor().id;
        const fs::path archive_dir_path = profile_local_path / repo_id;
        const fs::path update_sync_repo_path = update_sync_path / repo_id;
        if (!fs::exists(update_sync_repo_path, ec)) {
            continue;
        }

        process_status->set_warning_target(repo_id);

        // load installed index_db
        std::vector<PresetUpdaterIndex> index_db;
        if (fs::is_directory(archive_dir_path, ec) && !ec) {
            try {
                index_db = load_vendors_db(archive_dir_path);
            } catch (const std::exception& e) {
                std::string msg = fmt::format("Loading index db of {} has failed {}", repo_id, e.what());
                SPDLOG_ERROR(msg);
                process_status->set_warning(msg, PresetUpdaterReason::IndexUnreadable);
            }
        }

        // load staged index_db
        std::vector<PresetUpdaterIndex> update_sync_index_db;
        try {
            update_sync_index_db = load_vendors_db(update_sync_repo_path);
        } catch (const std::exception& e) {
            std::string msg = fmt::format(
                "Loading index db of {} from update_sync has failed {}",
                repo_id,
                e.what()
            );
            SPDLOG_ERROR(msg);
            process_status->set_warning(msg, PresetUpdaterReason::IndexUnreadable);
            continue;
        }
        
        // compare to find missing vendors
        for (const auto& update_sync_index : update_sync_index_db) {
            const auto it = std::find_if(
                index_db.begin(),
                index_db.end(),
                [update_sync_index](const PresetUpdaterIndex& index)
                { return index.vendor() == update_sync_index.vendor(); }
            );
            if (it != index_db.end()) {
                continue;
            }

            process_status->set_warning_target(repo_id, update_sync_index.vendor());

            // check if present version is recommended.

            const PresetUpdaterIndex::const_iterator recommended = update_sync_index.recommended();
            if (recommended == update_sync_index.end()) {
                process_status->set_warning(
                    fmt::format(
                        "No recommended version for vendor: {}, Index file might be corrupted. {}",
                        update_sync_index.vendor(), update_sync_index.path().string()
                    )
                , PresetUpdaterReason::NoUsableVersion);
                continue;
            }
            const fs::path vendor_yaml = update_sync_index.path().parent_path() / update_sync_index.vendor() / "vendor.yaml";
            Domain::Preset::VendorData vendor_data;
            try {
                Preset::IO::HwConfigLoader hw_config_loader;
                vendor_data = hw_config_loader.load(vendor_yaml.string());
                Semver update_sync_version = Semver(vendor_data.info.version);
                if (update_sync_version != recommended->config_version) {
                    process_status->set_warning(
                        fmt::format("Vendor data in update_sync failed sanity check. Its version is not recommended by its index. Vendor: {}", update_sync_index.vendor())
                    , PresetUpdaterReason::DataInconsistent);
                    continue;
                }
            } catch (const std::exception& e) {
                process_status->set_warning(
                    fmt::format("Failed to load vendor file {}: {}", vendor_yaml.string(), e.what())
                , PresetUpdaterReason::DataInconsistent);
                continue;
            }
            // update_sync_index is new vendor
            results.emplace_back(
                VendorReconfigurationState::NewVendor,
                update_sync_index.vendor(),
                repo_id,
                Slic3r::Semver(),
                recommended->config_version,
                recommended->comment
            );
        }
    }
    process_status->clear_warning_target();

    return results;
}

// Enable bitwise operations for ReconfigurationType
// This would enable bitwise operations for all enums - so its defined only here in cpp file.
template <typename T>
concept Enum = std::is_enum_v<T>;

constexpr Enum auto operator|(Enum auto lhs, Enum auto rhs) {
    using Underlying = std::underlying_type_t<decltype(lhs)>;
    return static_cast<decltype(lhs)>(
        static_cast<Underlying>(lhs) | static_cast<Underlying>(rhs));
}

constexpr Enum auto operator&(Enum auto lhs, Enum auto rhs) {
    using Underlying = std::underlying_type_t<decltype(lhs)>;
    return static_cast<decltype(lhs)>(
        static_cast<Underlying>(lhs) & static_cast<Underlying>(rhs));
}

constexpr bool has_flag(Enum auto flags, Enum auto flag) {
    return (flags & flag) != static_cast<decltype(flags)>(0);
}

void perform_reconfigurations(
    const PresetUpdaterReconfigurationList& reconfigurations,
    ReconfigurationType types_to_perform,
    PresetUpdaterProcessStatus* process_status
) {
    if (has_flag(types_to_perform, ReconfigurationType::ForcedDowngrades)) {
        perform_updates(reconfigurations.forced_downgrades(), process_status);
    }

    if (has_flag(types_to_perform, ReconfigurationType::ForcedUpdates)) {
        perform_updates(reconfigurations.forced_updates(), process_status);
    }

    if (has_flag(types_to_perform, ReconfigurationType::RegularUpdates)) {
        perform_updates(reconfigurations.regular_updates(), process_status);
    }

    if (has_flag(types_to_perform, ReconfigurationType::NotInIndex)) {
        perform_updates(reconfigurations.not_in_index(), process_status);
    }

    if (has_flag(types_to_perform, ReconfigurationType::NewVendors)) {
        perform_updates(reconfigurations.new_vendors(), process_status);
    }

    if (has_flag(types_to_perform, ReconfigurationType::Removals)) {
        perform_removals(reconfigurations.removals(), process_status);
    }
}

void cleanup_update_sync(PresetUpdaterProcessStatus* process_status)
{
    const fs::path update_sync_path = fs::path(data_dir()) / "update_sync";
    boost::system::error_code ec;
    for (const auto& entry : fs::directory_iterator(update_sync_path, ec)) {
        const fs::path& path = entry.path();
        SPDLOG_INFO("Deleting {}", path.string());
        if (!fs::remove_all(path, ec) || ec) {
            process_status->set_warning(
                fmt::format("Failed to remove file {}: {}", path.string(), ec.message())
            , PresetUpdaterReason::LocalStorageFailed);
        }
    }
}
} // namespace Slic3r::Biz::PresetUpdater
