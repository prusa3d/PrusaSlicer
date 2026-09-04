#include "PresetUpdaterRepositoryDatabase.hpp"

#include "PresetUpdaterProcessStatus.hpp"
#include "PresetUpdaterUtils.hpp"
#include "Slic3r/Biz/Network/IHttp.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/Utils/CopyFile.hpp"
#include "Slic3r/Directories.hpp"

#include "Slic3r/Exception.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"

#include <boost/filesystem/directory.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include "nlohmann/json.hpp"
#include <fmt/format.h>

namespace fs = boost::filesystem;

namespace Slic3r::Biz::PresetUpdater {

PresetUpdaterRepositoryDatabase::PresetUpdaterRepositoryDatabase(PresetUpdaterProcessStatus* process_status)
{
    boost::system::error_code ec;
    m_unq_tmp_path = Slic3r::temp_dir() / fs::unique_path();
    if (!fs::create_directories(m_unq_tmp_path, ec) && ec) {
        process_status->set_error(
            fmt::format("Failed to create temp directory {}: {}.", m_unq_tmp_path.string(), ec.what())
        , PresetUpdaterReason::DataDirUnusable);
        return;
    }
    load_app_manifest_json(process_status);
}

PresetUpdaterRepositoryDatabase::~PresetUpdaterRepositoryDatabase()
{
    boost::system::error_code ec;
    fs::remove_all(m_unq_tmp_path, ec);
    if (ec) {
        SPDLOG_ERROR("Failed to remove temp directory {}: {}", m_unq_tmp_path.string(), ec.what());
    }
}

void PresetUpdaterRepositoryDatabase::add_local_repository(
    const boost::filesystem::path& zip_path,
    PresetUpdaterProcessStatus* process_status
)
{
    PresetUpdaterRepositoryDescriptor header_data;
    const std::string uuid         = get_next_uuid();
    header_data.zip_path           = zip_path;
    header_data.unzipped_data_path = fs::path(data_dir()) / "local_repositories" / uuid;
    boost::system::error_code ec;

    std::string err_msg;
    if (!LocalPresetUpdaterRepository::extract_local_archive_repository(header_data, err_msg)) {
        process_status->set_error(err_msg, PresetUpdaterReason::ArchiveInvalid);
        return;
    }
    if (!LocalPresetUpdaterRepository::data_structure_check(header_data.unzipped_data_path, err_msg)) {
        fs::remove_all(header_data.unzipped_data_path, ec);
        process_status->set_error(err_msg, PresetUpdaterReason::ArchiveInvalid);
        return;
    }
    for (auto& repo : m_all_repositories) {
        if (repo.get()->descriptor().id == header_data.id) {
            repo.get()->set_selected(false);
        }
    }
    m_all_repositories.emplace_back(
        std::make_unique<LocalPresetUpdaterRepository>(uuid, std::move(header_data), true)
    );
    save_app_manifest_json(process_status);
}

void PresetUpdaterRepositoryDatabase::remove_local_repository(
    const std::string& uuid,
    PresetUpdaterProcessStatus* process_status
)
{
    auto compare_repo = [uuid](const std::unique_ptr<AbstractPresetUpdaterRepository>& repo) {
        return repo->uuid() == uuid;
    };

    auto archives_it = std::find_if(m_all_repositories.begin(), m_all_repositories.end(), compare_repo);
    if (archives_it == m_all_repositories.end()) {
        process_status->set_error(
            fmt::format(
                "Failed to remove repository. Could not find repository with matching UUID \"{}\".",
                uuid
            )
        , PresetUpdaterReason::SourceNotFound);
        return;
    }
    if (archives_it->get()->descriptor().unzipped_data_path.empty()) {
        process_status->set_error(
            fmt::format(
                "Failed to remove repository. The repository with UUID \"{}\" is an online one.",
                uuid
            )
        , PresetUpdaterReason::SourceNotFound);
        return;
    }

    boost::system::error_code ec;
    fs::remove_all(archives_it->get()->descriptor().unzipped_data_path, ec);
    if (ec) {
        process_status->set_warning(
            fmt::format(
                "Failed to remove directory {}: {}.",
                archives_it->get()->descriptor().unzipped_data_path.string(),
                ec.what()
            )
        , PresetUpdaterReason::LocalStorageFailed);
    }

    const std::string removed_id = archives_it->get()->descriptor().id;
    const bool was_selected      = archives_it->get()->is_selected();
    m_all_repositories.erase(archives_it);

    if (was_selected) {
        select_online_repository_with_id(removed_id);
    }

    save_app_manifest_json(process_status);
}

void PresetUpdaterRepositoryDatabase::select_online_repository_with_id(const std::string& id)
{
    auto online_it = m_all_repositories.end();
    for (auto it = m_all_repositories.begin(); it != m_all_repositories.end(); ++it) {
        if ((*it)->descriptor().id != id) {
            continue;
        }
        if ((*it)->is_selected()) {
            return;
        }
        if ((*it)->descriptor().unzipped_data_path.empty()
            && online_it == m_all_repositories.end())
        {
            online_it = it;
        }
    }

    if (online_it != m_all_repositories.end()) {
        (*online_it)->set_selected(true);
    }
}

void PresetUpdaterRepositoryDatabase::load_app_manifest_json(PresetUpdaterProcessStatus* process_status)
{
    ASSERT(
        m_all_repositories.empty(),
        "This method only ever appends, so calling it twice would list every source twice."
        " It belongs in the constructor and nowhere else."
    );

    const fs::path path = get_stored_manifest_path();
    boost::system::error_code ec;
    if (!fs::exists(path, ec) || ec) {
        copy_initial_manifest(process_status);
    }
    boost::nowide::ifstream file(path.string());
    std::string data;
    if (file.is_open()) {
        std::string line;
        while (getline(file, line)) {
            data += line;
        }
        file.close();
    } else {
        process_status->set_error(
            fmt::format(
                "Failed to read Repository Source Manifest at {}: Couldn't open the file. The file is being deleted to prevent this error in future.",
                path.string()
            )
        , PresetUpdaterReason::ManifestUnusable);
        fs::remove(path, ec);
        return;
    }
    if (data.empty()) {
        process_status->set_error(
            "The Repository Source Manifest file is empty. The file is being deleted to prevent this error in future."
        , PresetUpdaterReason::ManifestUnusable);
        fs::remove(path, ec);
        return;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(data, nullptr, false);

        if (j.is_discarded() || !j.is_array()) {
            process_status->set_error(
                "Failed to parse Repository Source Manifest JSON: Input is not a valid JSON array. The file is being deleted to prevent this error in future."
            , PresetUpdaterReason::ManifestUnusable);
            fs::remove(path, ec);
            return;
        }

        for (const auto& repo_json : j) {
            // if "zip_path in repo_json is not empty, it's a local repo, else it's an online repo
            if (repo_json.contains("zip_path") && !repo_json["zip_path"].is_null() && !repo_json["zip_path"].get<std::string>().empty()) {
                PresetUpdaterRepositoryDescriptor descriptor;
                std::string warning_msg;
                if (!AbstractPresetUpdaterRepository::extract_repository_header(
                        repo_json,
                        descriptor,
                        warning_msg
                    ))
                {
                    process_status->set_warning(warning_msg, PresetUpdaterReason::ManifestUnusable);
                    continue;
                }
                if (descriptor.unzipped_data_path.empty()
                    || !fs::exists(descriptor.unzipped_data_path, ec)
                    || ec
                    || !fs::is_directory(descriptor.unzipped_data_path, ec)
                    || ec)
                {
                    process_status->set_warning(
                        fmt::format(
                            "Local repository source {} has is missing its directory at {}: {}. Skipping.",
                            descriptor.id,
                            descriptor.unzipped_data_path.string(),
                            ec.what()
                        )
                    , PresetUpdaterReason::SourceDropped);
                    continue;
                }

                if (descriptor.uuid.empty()) {
                    descriptor.uuid = descriptor.unzipped_data_path.filename().string();
                }
                std::string uuid = descriptor.uuid;
                bool selected    = repo_json.value("selected", true);
                m_all_repositories.emplace_back(
                    std::make_unique<LocalPresetUpdaterRepository>(
                        uuid,
                        std::move(descriptor),
                        selected
                    )
                );
            } else {
                // Online repo
                PresetUpdaterRepositoryDescriptor descriptor;
                std::string warning_msg;
                if (!AbstractPresetUpdaterRepository::extract_repository_header(
                        repo_json,
                        descriptor,
                        warning_msg
                    ))
                {
                    process_status->set_warning(warning_msg, PresetUpdaterReason::ManifestUnusable);
                    continue;
                }
                if (descriptor.uuid.empty()) {
                    descriptor.uuid = get_next_uuid();
                }
                std::string uuid = descriptor.uuid;
                bool selected = repo_json.value("selected", true);
                m_all_repositories.emplace_back(
                    std::make_unique<OnlinePresetUpdaterRepository>(
                        uuid,
                        std::move(descriptor),
                        selected
                    )
                );
            }
        }
    } catch (const nlohmann::json::exception& e) {
        process_status->set_error(
            fmt::format(
                "Failed to read Repository Source Manifest JSON. Reason: {}. The file is being deleted to prevent this error in future.",
                e.what()
            )
        , PresetUpdaterReason::ManifestUnusable);
        fs::remove(path, ec);
        return;
    }

    // If there were corrupt data in json, we store it again.
    save_app_manifest_json(process_status);
}

void PresetUpdaterRepositoryDatabase::copy_initial_manifest(PresetUpdaterProcessStatus* process_status) const
{
    const fs::path target_path = get_stored_manifest_path();
    const fs::path source_path = fs::path(resources_dir()) / "presets" / "RepositoryManifest.json";
    boost::system::error_code ec;
    if (!fs::exists(source_path, ec) || ec) {
        process_status->set_error(
            fmt::format(
                "Resources descriptor file does not exists at {}: {}. The installation is corrupt.",
                source_path.string(),
                ec.message()
            )
        , PresetUpdaterReason::ManifestUnusable);
        return;
    }
    copy_file_wrapper(source_path, target_path, process_status);
}

void PresetUpdaterRepositoryDatabase::save_app_manifest_json(PresetUpdaterProcessStatus* process_status) const
{
    /*
    [{
        "name": "Production",
        "description": "Production repository",
        "visibility": null,
        "id": "prod",
        "url": "http://10.24.3.3:8001/v1/repos/prod",
        "index_url": "http://10.24.3.3:8001/v1/repos/prod/vendor_indices.zip"
        "selected": 1
        "has_installed_printers": 1
    }, {
        "name": "Development",
        "description": "Production repository",
        "visibility": "developers only",
        "id": "dev",
        "url": "http://10.24.3.3:8001/v1/repos/dev",
        "index_url": "http://10.24.3.3:8001/v1/repos/dev/vendor_indices.zip"
        "selected": 0
        "has_installed_printers": 0
    }]
    */
    nlohmann::json json = nlohmann::json::array();
    for (const auto& repo : m_all_repositories) {
        const PresetUpdaterRepositoryDescriptor& descriptor = repo.get()->descriptor();
        if (!repo->descriptor().unzipped_data_path.empty()) {
            nlohmann::json sub = {
                {"uuid", descriptor.uuid},
                {"name", descriptor.name},
                {"description", descriptor.description},
                {"visibility", descriptor.visibility},
                {"id", descriptor.id},
                {"url", descriptor.url},
                {"index_url", descriptor.index_url},
                {"selected", repo.get()->is_selected()},
                {"offline_archive_url", descriptor.offline_archive_url},
                {"unzipped_data_path", descriptor.unzipped_data_path.string()},
                {"zip_path", descriptor.zip_path.string()}
            };
            json.push_back(sub);
        } else {
            nlohmann::json sub = {
                {"uuid", descriptor.uuid},
                {"name", descriptor.name},
                {"description", descriptor.description},
                {"visibility", descriptor.visibility},
                {"id", descriptor.id},
                {"url", descriptor.url},
                {"index_url", descriptor.index_url},
                {"selected", repo.get()->is_selected()}
            };
            json.push_back(sub);
        }
    }

    std::string path = get_stored_manifest_path().string();
    boost::nowide::ofstream file(path);
    if (file.is_open()) {
        file << json.dump();
        file.close();
    } else {
        process_status->set_error(
            fmt::format("Failed to open Repository Source Manifest for writing at {}.", path)
        , PresetUpdaterReason::ManifestUnusable);
    }
}

fs::path PresetUpdaterRepositoryDatabase::get_stored_manifest_path() const
{
    return (boost::filesystem::path(data_dir()) / "shared_runtime" / "RepositoryManifest.json")
        .make_preferred();
}

void PresetUpdaterRepositoryDatabase::clear_online_repos()
{
    auto it = m_all_repositories.begin();
    while (it != m_all_repositories.end()) {
        // Do not clean repos with local path (local repo).
        if ((*it)->descriptor().unzipped_data_path.empty()) {
            it = m_all_repositories.erase(it);
        } else {
            ++it;
        }
    }
}

void PresetUpdaterRepositoryDatabase::read_server_manifest(
    const std::string& json_body,
    PresetUpdaterProcessStatus* process_status
)
{
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(json_body);
    } catch (const nlohmann::json::exception& e) {
        process_status->set_warning(
            fmt::format(
                "Failed to parse online Repository Source Manifest JSON. Reason: {}. Sources were not updated from online Manifest.",
                e.what()
            )
        , PresetUpdaterReason::SourceListUnavailable);
        return;
    }

    if (!json.is_array()) {
        process_status->set_warning(
            fmt::format("Failed to parse online Repository Source Manifest JSON. JSON is not an array.")
        , PresetUpdaterReason::SourceListUnavailable);
        return;
    }

    // Online repo manifests are in json_body. We already have read descriptor from datadir with repos from last run.
    // Keep the local ones and replace the online ones but keep uuid for same id so the selected map is correct.
    // Solution: Create id - uuid translate table for online repos.
    std::map<std::string, std::string> id_to_uuid;
    for (const auto& repo_ptr : m_all_repositories) {
        if (repo_ptr->descriptor().zip_path.empty()) {
            id_to_uuid[repo_ptr->descriptor().id] = repo_ptr->uuid();
        }
    }

    // Remember selected repos
    std::map<std::string, bool> uuid_selected_map;
    for (const auto& repo_ptr : m_all_repositories) {
        uuid_selected_map[repo_ptr->uuid()] = repo_ptr->is_selected();
    }

    // Make a stash of secret repos that are online and is selected.
    // If some of these will be missing afer reading the json tree, it needs to be added back to main population.
    PrivateRepositoryVector secret_online_used_repos_cache;
    for (const auto& repo_ptr : m_all_repositories) {
        if (repo_ptr->descriptor().visibility.empty()
            || !repo_ptr->descriptor().unzipped_data_path.empty())
        {
            // public and local repo are skipped
            continue;
        }
        if (repo_ptr->is_selected()) {
            PresetUpdaterRepositoryDescriptor descriptor(repo_ptr->descriptor());
            secret_online_used_repos_cache.emplace_back(
                std::make_unique<OnlinePresetUpdaterRepository>(
                    repo_ptr->uuid(),
                    std::move(descriptor),
                    true
                )
            );
        }
    }

    clear_online_repos();

    for (const auto& repo_json : json) {
        PresetUpdaterRepositoryDescriptor descriptor;
        std::string warning_msg;
        if (!AbstractPresetUpdaterRepository::extract_repository_header(
                repo_json,
                descriptor,
                warning_msg
            ))
        {
            process_status->set_warning(warning_msg, PresetUpdaterReason::ManifestUnusable);
            continue;
        }

        auto id_it       = id_to_uuid.find(descriptor.id);
        std::string uuid = (id_it == id_to_uuid.end() ? get_next_uuid() : id_it->second);
        bool selected = (id_it == id_to_uuid.end() ? true : uuid_selected_map[uuid]); // selected is set as default to true - its a never seen before repository

        m_all_repositories.emplace_back(
            std::make_unique<OnlinePresetUpdaterRepository>(uuid, std::move(descriptor), selected)
        );
    }

    // return missing secret selected online repos to the vector
    for (const auto& repo_ptr : secret_online_used_repos_cache) {
        std::string uuid = repo_ptr->uuid();
        if (std::find_if(
                m_all_repositories.begin(),
                m_all_repositories.end(),
                [uuid](const std::unique_ptr<AbstractPresetUpdaterRepository>& ptr) {
                    return ptr->uuid() == uuid;
                }
            )
            == m_all_repositories.end())
        {
            PresetUpdaterRepositoryDescriptor descriptor(repo_ptr->descriptor());
            m_all_repositories.emplace_back(
                std::make_unique<OnlinePresetUpdaterRepository>(
                    repo_ptr->uuid(),
                    std::move(descriptor),
                    true
                )
            );
        }
    }

    save_app_manifest_json(process_status);
}

SharedPresetUpdaterRepositoryInfoVector PresetUpdaterRepositoryDatabase::get_all_repositories() const
{
    SharedPresetUpdaterRepositoryInfoVector result;
    result.reserve(m_all_repositories.size());
    for (const auto& repo_ptr : m_all_repositories) {
        result.emplace_back(repo_ptr->descriptor(), repo_ptr->is_selected());
    }
    return result;
}

SharedRepositoryVector PresetUpdaterRepositoryDatabase::get_selected_repositories() const
{
    SharedRepositoryVector result;
    result.reserve(m_all_repositories.size());
    for (const auto& repo_ptr : m_all_repositories) {
        if (repo_ptr->is_selected()) {
            result.emplace_back(repo_ptr.get());
        }
    }
    return result;
}

SharedRepositoryVector PresetUpdaterRepositoryDatabase::get_selected_and_required_repositories(
    const std::set<std::string>& required_ids
) const
{
    SharedRepositoryVector result = get_selected_repositories();
    for (const std::string& id : required_ids) {
        const auto served = std::find_if(
            result.begin(),
            result.end(),
            [&id](const AbstractPresetUpdaterRepository* repo) { return repo->descriptor().id == id; }
        );
        if (served != result.end()) {
            continue;
        }
        const auto offering = std::find_if(
            m_all_repositories.begin(),
            m_all_repositories.end(),
            [&id](const std::unique_ptr<AbstractPresetUpdaterRepository>& repo) {
                return repo->descriptor().id == id;
            }
        );
        if (offering != m_all_repositories.end()) {
            result.emplace_back(offering->get());
        }
    }
    return result;
}

void PresetUpdaterRepositoryDatabase::apply_selection(
    const SharedPresetUpdaterRepositoryInfoVector& repos,
    PresetUpdaterProcessStatus* process_status
)
{
    // first confirm no more than 1 selected repo of same id
    for (size_t i = 0; i < repos.size(); i++) {
        if (!repos[i].selected) {
            continue;
        }
        for (size_t k = i + 1; k < repos.size(); k++) {
            if (repos[i].descriptor.id == repos[k].descriptor.id && repos[k].selected) {
                process_status->set_error(
                    fmt::format(
                        "Can't apply selection. Two repositories of same id ({}) are selected.",
                        repos[i].descriptor.id
                    )
                , PresetUpdaterReason::Internal);
                return;
            }
        }
    }
    
    for (const SharedPresetUpdaterRepositoryInfo& info : repos) {
        auto repo_it = std::find_if(
            m_all_repositories.begin(),
            m_all_repositories.end(),
            [&info](const std::unique_ptr<AbstractPresetUpdaterRepository>& ptr) {
                return ptr->uuid() == info.descriptor.uuid;
            }
        );

        // A repository the caller knows and this database does not. Only add_local_repository
        // creates repositories, so the caller is working from a listing older than a removal.
        if (repo_it == m_all_repositories.end()) {
            continue;
        }

        repo_it->get()->set_selected(info.selected);
    }

    // We also need to remove offline repos missing in info vector
    std::erase_if(
        m_all_repositories,
        [&repos](const std::unique_ptr<AbstractPresetUpdaterRepository>& it)
        {
            if (it->descriptor().unzipped_data_path.empty()) {
                return false;
            }
            
            const std::string& uuid = it->descriptor().uuid;
            
            return std::find_if(
                repos.begin(),
                repos.end(),
                [&uuid](const auto& info) { return info.descriptor.uuid == uuid; }
            ) == repos.end();
        }
    );

    consolidate_offline_repo_unzipped_folders(process_status);
    save_app_manifest_json(process_status);
}

void PresetUpdaterRepositoryDatabase::consolidate_offline_repo_unzipped_folders(PresetUpdaterProcessStatus* process_status) const
{
    boost::system::error_code ec;
    const fs::path local_repos_dir = fs::path(data_dir()) / "local_repositories";

    if (!fs::exists(local_repos_dir, ec) || ec) {
        return;
    }

    for (auto it = fs::directory_iterator(local_repos_dir, ec); it != fs::directory_iterator(); it.increment(ec)) {
        if (ec) {
            process_status->set_warning(fmt::format("Failed to iterate local_repositories directory: {}", ec.message()), PresetUpdaterReason::LocalStorageFailed);
            break;
        }

        if (fs::is_directory(it->status())) {
            const std::string dir_name = it->path().filename().string();

            auto repo_it = std::find_if(
                m_all_repositories.begin(),
                m_all_repositories.end(),
                [&dir_name](const std::unique_ptr<AbstractPresetUpdaterRepository>& repo) {
                    return !repo->descriptor().unzipped_data_path.empty() && repo->uuid() == dir_name;
                }
            );

            if (repo_it == m_all_repositories.end()) {
                boost::system::error_code rm_ec;
                fs::remove_all(it->path(), rm_ec);
                if (rm_ec) {
                    process_status->set_warning(
                        fmt::format("Failed to delete unreferenced offline repository folder {}: {}.", it->path().string(), rm_ec.message())
                    , PresetUpdaterReason::LocalStorageFailed);
                }
            }
        }
    }
}
    
std::string PresetUpdaterRepositoryDatabase::get_next_uuid()
{
    thread_local boost::uuids::random_generator generator;
    return boost::uuids::to_string(generator());
}

namespace {

bool add_authorization_header(Network::IHttp& http, PresetUpdaterProcessStatus* process_status)
{
    const std::string access_token = process_status->access_token();
    if (!access_token.empty()) {
        http.header("Authorization", "Bearer " + access_token);
    }
    return true;
}

bool sync_inner(std::string& manifest, PresetUpdaterProcessStatus* process_status)
{
    bool res        = false;
    std::string url = Network::ServiceConfig::instance().preset_repo_repos_url();
    auto retry_fn   = [process_status](Network::IHttp::Retry retry, bool& cancel) {
        cancel = process_status->on_attempt(retry.attempt, retry.ms_to_next_attempt);
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
        .on_error([&](std::string body, std::string error, unsigned http_status) {
        SPDLOG_ERROR(
                "Download of Sources Database Manifest has failed: {} ; {} ; {}",
                body,
                error,
                http_status
            );
            process_status->set_warning("Failed to download Sources Database Manifest file.", PresetUpdaterReason::SourceListUnavailable);
            res = false;
        })
        .on_complete([&](std::string body, unsigned /* http_status */) {
            manifest = body;
            res      = true;
        })
        .perform_sync(process_status->get_retry_policy());

    return res;
}
} // namespace

bool PresetUpdaterRepositoryDatabase::sync(PresetUpdaterProcessStatus* process_status)
{
    std::string manifest;
    bool sync_res = false;
    process_status->set_download_target(std::string(k_manifest_download_target));
    sync_res = sync_inner(manifest, process_status);
    if (!sync_res) {
        return false;
    }
    try {
        read_server_manifest(std::move(manifest), process_status);
    } catch (const Slic3r::RuntimeError& e) {
        process_status->set_warning(fmt::format("Failed to read server manifest: {}", e.what()), PresetUpdaterReason::SourceListUnavailable);
        return false;
    }

    return true;
}

} // namespace Slic3r::Biz::PresetUpdater
