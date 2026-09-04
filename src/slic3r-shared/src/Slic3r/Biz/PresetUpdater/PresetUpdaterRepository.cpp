#include "PresetUpdaterRepository.hpp"

#include "PresetUpdaterProcessStatus.hpp"
#include "PresetUpdaterUtils.hpp"
#include "Slic3r/Biz/Network/IHttp.hpp"
#include "Slic3r/Biz/Utils/CopyFile.hpp"

#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"

#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/directory.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <fmt/format.h>

namespace fs                     = boost::filesystem;
static const char* TMP_EXTENSION = ".download";

namespace Slic3r::Biz::PresetUpdater {

using Algorithms::open_zip_reader;
using Algorithms::close_zip_reader;

namespace {

void delete_path_recursive(const fs::path& path)
{
    try {
        boost::system::error_code ec;
        if (fs::exists(path, ec) && !ec) {
            for (fs::directory_iterator it(path); it != fs::directory_iterator(); ++it) {
                const fs::path subpath = it->path();
                if (fs::is_directory(subpath)) {
                    delete_path_recursive(subpath);
                } else {
                    fs::remove(subpath);
                }
            }
            fs::remove(path);
        }
    } catch (const std::exception&) {
        SPDLOG_ERROR("Failed to delete files at: {}", path.string());
    }
}

bool unzip_repository(const fs::path& source_path, const fs::path& target_path)
{
    boost::system::error_code ec;
    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);
    if (!open_zip_reader(&archive, source_path.string())) {
        SPDLOG_ERROR("Couldn't open zipped Archive source. {}", source_path.string());
        return false;
    }
    size_t num_files = mz_zip_reader_get_num_files(&archive);

    for (size_t i = 0; i < num_files; ++i) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&archive, i, &file_stat)) {
            SPDLOG_ERROR(
                "Failed to get file stat for file #{} in the zip archive. Ending Unzipping.",
                std::to_string(i)
            );
            close_zip_reader(&archive);
            return false;
        }
        fs::path extracted_path = target_path / file_stat.m_filename;
        if (file_stat.m_is_directory) {
            // Create directory if it doesn't exist
            if (!fs::create_directories(extracted_path, ec) && ec) {
                SPDLOG_ERROR("Failed to create directory {}: {}", extracted_path.string(), ec.message());
                return false;
            }
            continue;
        }
        // Create parent directory if it doesn't exist
        if (!fs::create_directories(extracted_path.parent_path(), ec) && ec) {
            SPDLOG_ERROR(
                "Failed to create directory {}: {}",
                extracted_path.parent_path().string(),
                ec.message()
            );
            return false;
        }
        // Extract file
        if (!mz_zip_reader_extract_to_file(&archive, i, extracted_path.string().c_str(), 0)) {
            SPDLOG_ERROR("Failed to extract file #{} from the zip archive. Ending Unzipping.", i);
            close_zip_reader(&archive);
            return false;
        }
    }
    close_zip_reader(&archive);
    return true;
}

} // namespace

bool AbstractPresetUpdaterRepository::extract_repository_header(
    const nlohmann::json& json,
    PresetUpdaterRepositoryDescriptor& data,
    std::string& warning_msg
)
{
    try {
        // Mandatory attributes
        json.at("name").get_to(data.name);
        json.at("id").get_to(data.id);
        json.at("url").get_to(data.url);

        // Optional attributes
        if (json.contains("uuid")) {
            json.at("uuid").get_to(data.uuid);
        }
        if (json.contains("index_url")) {
            json.at("index_url").get_to(data.index_url);
        }
        if (json.contains("description")) {
            json.at("description").get_to(data.description);
        }
        if (json.contains("visibility")) {
            json.at("visibility").get_to(data.visibility);
        }

        if (json.contains("offline_archive_url")) {
            json.at("offline_archive_url").get_to(data.offline_archive_url);
        }
        if (json.contains("unzipped_data_path")) {
            std::string path_str    = json.at("unzipped_data_path").get<std::string>();
            data.unzipped_data_path = boost::filesystem::path(path_str);
        }
        if (json.contains("zip_path")) {
            std::string path_str = json.at("zip_path").get<std::string>();
            data.zip_path        = boost::filesystem::path(path_str);
        }

    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Failed to parse source manifest: {}. json: {}", e.what(), json.dump());
        warning_msg = fmt::format("Failed to parse source manifest: {}.", e.what());
        return false;
    }

    if (data.id.empty()) {
        SPDLOG_ERROR("Source manifest has an empty id. json: {}", json.dump());
        warning_msg = "Failed to parse source manifest: the source id must not be empty.";
        return false;
    }

    return true;
}

bool OnlinePresetUpdaterRepository::get_file_inner(
    const std::string& url,
    const fs::path& target_path,
    PresetUpdaterProcessStatus* process_status
) const
{
    boost::uuids::random_generator generator;
    boost::uuids::uuid uuid = generator();

    bool res          = false;
    fs::path tmp_path = target_path;
    tmp_path += fmt::format(".{}{}", boost::uuids::to_string(generator()), TMP_EXTENSION);

    SPDLOG_DEBUG(
        "Get: `{}`\n\t-> `{}`\n\tvia tmp path `{}`",
        url,
        target_path.string(),
        tmp_path.string()
    );

    auto retry_fn = [process_status, url](Network::IHttp::Retry retry, bool& cancel) {
        cancel = process_status->on_attempt(retry.attempt, retry.ms_to_next_attempt);
        if (cancel) {
            SPDLOG_ERROR("Download canceled {}", url);
        }
    };

    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Get,
        url,
        retry_fn
    );
    const std::string access_token = process_status->access_token();
    if (!access_token.empty()) {
        http->header("Authorization", "Bearer " + access_token);
    }
    http->timeout_total(30)
        .size_limit(1024 * 1024 * 100) // How large is the largest bundle?
        .on_progress([process_status](Network::IHttp::Progress, bool& cancel)
                     { cancel = process_status->cancel_requested(); })
        .on_error([&](std::string body, std::string error, unsigned http_status) {
            SPDLOG_ERROR("Error getting: `{}`: HTTP {}, {}", url, http_status, body);
            res = false;
        })
        .on_complete([&](std::string body, unsigned /* http_status */) {
            if (body.empty()) {
                std::string msg = fmt::format("Error getting: `{}`: Body is empty.", url);
                SPDLOG_ERROR(msg);
                return;
            }
            boost::nowide::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
            file.write(body.c_str(), body.size());
            file.close();
            safe_move(tmp_path, target_path);
            res = true;
        })
        .perform_sync(process_status->get_retry_policy());

    return res;
}

bool OnlinePresetUpdaterRepository::get_archive(
    const fs::path& target_path,
    PresetUpdaterProcessStatus* process_status
) const
{
    return get_file_inner(
        m_data.index_url.empty() ? m_data.url + "vendor_indices.zip" : m_data.index_url,
        target_path,
        process_status
    );
}

bool OnlinePresetUpdaterRepository::get_file(
    const std::string& source_subpath,
    const boost::filesystem::path& target_path,
    PresetUpdaterProcessStatus* process_status
) const
{
    process_status->set_download_target(target_path.filename().string());

    const std::string escaped_source_subpath = Network::IHttp::escape_path_by_element(
        fs::path(source_subpath)
    );

    return get_file_inner(m_data.url + escaped_source_subpath, target_path, process_status);
}

bool OnlinePresetUpdaterRepository::get_version_manifest(
    const std::string& source_subpath,
    const boost::filesystem::path& target_path,
    PresetUpdaterProcessStatus* process_status
) const
{
    process_status->set_download_target(target_path.filename().string());

    const std::string escaped_source_subpath = Network::IHttp::escape_path_by_element(
        fs::path(source_subpath)
    );
    return get_file_inner(m_data.url + escaped_source_subpath, target_path, process_status);
}

bool LocalPresetUpdaterRepository::get_file_inner(
    const fs::path& source_path,
    const fs::path& target_path
) const
{
    SPDLOG_INFO("Copying {} to {}", source_path.string(), target_path.string());
    std::string error_message;
    Utils::CopyFileResult
        cfr = Utils::copy_file(source_path.string(), target_path.string(), error_message, false);
    if (cfr != Utils::CopyFileResult::Success) {
        SPDLOG_ERROR(
            "Copying of {} to {} has failed: {}",
            source_path.string(),
            target_path.string(),
            error_message
        );
        // remove target file, even if it was there before
        boost::system::error_code ec;
        if (fs::exists(target_path, ec) && !ec) {
            fs::remove(target_path, ec);
            if (ec) {
                SPDLOG_ERROR("Failed to delete file: {}", ec.message());
            }
        }
        return false;
    }
    // Permissions should be copied from the source file by copy_file(). We are not sure about the source
    // permissions, let's rewrite them with 644.
    static constexpr const auto perms = fs::owner_read
        | fs::owner_write
        | fs::group_read
        | fs::others_read;
    fs::permissions(target_path, perms);

    return true;
}

bool LocalPresetUpdaterRepository::get_file(
    const std::string& source_subpath,
    const boost::filesystem::path& target_path,
    PresetUpdaterProcessStatus* process_status
) const
{
    return get_file_inner(m_data.unzipped_data_path / source_subpath, target_path);
}

bool LocalPresetUpdaterRepository::get_version_manifest(
    const std::string& source_subpath,
    const boost::filesystem::path& target_path,
    PresetUpdaterProcessStatus* process_status
) const
{
    return get_file_inner(m_data.unzipped_data_path / source_subpath, target_path);
}

bool LocalPresetUpdaterRepository::get_archive(
    const fs::path& target_path,
    PresetUpdaterProcessStatus* process_status
) const
{
    fs::path source_path = fs::path(m_data.unzipped_data_path) / "vendor_indices.zip";
    return get_file_inner(std::move(source_path), target_path);
}

template <typename F>
struct ScopeFail {
    F f;
    bool active = true;
    ~ScopeFail() { if (active) f(); }
    void dismiss() { active = false; }
};

bool LocalPresetUpdaterRepository::extract_local_archive_repository(
    PresetUpdaterRepositoryDescriptor& descriptor,
    std::string& error_msg
)
{
    // Delete previous data before unzip.
    delete_path_recursive(descriptor.unzipped_data_path);

    boost::system::error_code ec;
    if (!fs::create_directories(descriptor.unzipped_data_path, ec) && ec) {
        error_msg = fmt::format(
            "Failed to create directory at {} for repository {}.",
            descriptor.unzipped_data_path.string(),
            descriptor.id
        );
        return false;
    }

    // RAII guard to clean up the directory if the function exits early
    ScopeFail cleanup_guard{[&]() { delete_path_recursive(descriptor.unzipped_data_path); }};

    // Unzip repository zip to unique path in temp directory.
    if (!unzip_repository(descriptor.zip_path, descriptor.unzipped_data_path)) {
        error_msg = fmt::format("Failed to unzip repository on path \"{}\".", descriptor.zip_path.string());
        return false;
    }

    // Read the manifest file.
    fs::path manifest_path = descriptor.unzipped_data_path / "manifest.json";

    try {
        boost::nowide::ifstream file_stream(manifest_path.c_str());
        if (!file_stream.is_open()) {
            error_msg = fmt::format("Failed to open manifest file: {}", manifest_path.string());
            return false;
        }

        nlohmann::json j = nlohmann::json::parse(file_stream, nullptr, false);
        if (j.is_discarded()) {
            error_msg = fmt::format("Failed to parse manifest as JSON: {}", manifest_path.string());
            return false;
        }

        if (!extract_repository_header(j, descriptor, error_msg)) {
            return false;
        }
    } catch (const nlohmann::json::exception& e) {
        error_msg = fmt::format(
            "Failed to read source manifest JSON {}. Reason: {}",
            manifest_path.string(),
            e.what()
        );
        return false;
    }

    // Success path: dismiss the guard so it doesn't delete the data
    cleanup_guard.dismiss();
    return true;
}

bool LocalPresetUpdaterRepository::data_structure_check(
    const boost::filesystem::path& unzipped_path,
    std::string& error_msg
)
{
    boost::system::error_code ec;
    if (!fs::exists(unzipped_path, ec) || ec || !fs::is_directory(unzipped_path, ec) || ec) {
        error_msg = fmt::format("Directory \"{}\" does not exists.", unzipped_path.string());
        return false;
    }

    const fs::path manifest_path = unzipped_path / "manifest.json";
    if (!fs::exists(manifest_path, ec) || ec || !fs::is_regular_file(manifest_path, ec) || ec) {
        error_msg = fmt::format("Missing manifest file at \"{}\".", manifest_path.string());
        return false;
    }

    const fs::path index_archive_path = unzipped_path / "vendor_indices.zip";
    if (!fs::exists(index_archive_path, ec)
        || ec
        || !fs::is_regular_file(index_archive_path, ec)
        || ec)
    {
        error_msg = fmt::format("Missing index archive at \"{}\".", index_archive_path.string());
        return false;
    }

    return true;
}

} // namespace Slic3r::Biz::PresetUpdater
