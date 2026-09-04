#pragma once

#include "Slic3r/Biz/Network/MockHttp.hpp"
#include "Slic3r/Biz/Network/MockHttpFactory.hpp"

#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/SecretStoreDummy.hpp"
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/TestData.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Biz/Slicing/TestUtils.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Semver.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/PresetUpdater/IPresetUpdaterResultListener.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterInteractor.hpp"
#include "Slic3r/Biz/SHA256.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/nowide/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <functional>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Slic3r::Biz::PresetUpdater::TestSupport {

using namespace std::chrono_literals;
namespace fs = boost::filesystem;

inline const std::string k_repo_name     = "test_repo";
inline const std::string k_vendor_name   = "TestVendor";
inline const std::string k_second_vendor = "SecondVendor";
inline const std::string k_server_addr   = "http://localhost:8000/";

using ResponseMutator =
    std::function<void(const std::string& url, std::string& body, unsigned& status)>;

inline void copy_dir_content_local(const fs::path& from, const fs::path& to)
{
    ASSERT(fs::exists(from) && fs::is_directory(from));
    if (!fs::exists(to)) {
        fs::create_directories(to);
    }
    boost::system::error_code ec;
    for (fs::recursive_directory_iterator it(from); it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        ASSERT(!ec);
        const fs::path relative_path = it->path().lexically_relative(from);
        const fs::path dest_path     = to / relative_path;
        if (fs::is_directory(it->status())) {
            if (!fs::exists(dest_path)) {
                fs::create_directories(dest_path);
            }
        } else if (fs::is_regular_file(it->status())) {
            fs::copy_file(it->path(), dest_path, fs::copy_options::overwrite_existing);
        }
    }
}

inline std::string read_file(const fs::path& path)
{
    boost::nowide::ifstream file(path, std::ios::in | std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

inline std::string file_sha256(const fs::path& path)
{
    std::ostringstream hex;
    hex << std::hex << std::uppercase << std::setfill('0');
    for (const unsigned char byte : Slic3r::Biz::sha256(read_file(path))) {
        hex << std::setw(2) << static_cast<int>(byte);
    }
    return hex.str();
}

/// The manifest is rewritten by the first job that reads it, so match its value, not its spelling.
inline void deselect_every_source(const fs::path& manifest_path)
{
    const std::string key{"\"selected\""};
    std::string manifest = read_file(manifest_path);
    size_t deselected    = 0;

    for (size_t position = manifest.find(key); position != std::string::npos;
         position        = manifest.find(key, position))
    {
        const size_t colon = manifest.find(':', position + key.size());
        if (colon == std::string::npos) {
            break;
        }

        position = manifest.find_first_not_of(" \t\r\n", colon + 1);
        if (position == std::string::npos) {
            break;
        }
        if (manifest.compare(position, 4, "true") == 0) {
            manifest.replace(position, 4, "false");
            ++deselected;
        }
    }

    ASSERT(deselected > 0);

    boost::nowide::ofstream file(manifest_path, std::ios::out | std::ios::binary | std::ios::trunc);
    file << manifest;
}

/// Adds a source the offline check will accept the id of. Nothing is served for it, which is all
/// a check that only reads the data dir needs.
inline void add_source_to_manifest(const fs::path& manifest_path, const std::string& id)
{
    nlohmann::json manifest = nlohmann::json::parse(read_file(manifest_path));
    manifest.push_back(
        nlohmann::json{
            {"description", "Test purpose only."},
            {"id", id},
            {"index_url", k_server_addr + id + "/vendor_indices.zip"},
            {"name", id},
            {"selected", true},
            {"url", k_server_addr + id + "/"},
            {"uuid", "uuid-" + id},
            {"visibility", ""}
        }
    );

    boost::nowide::ofstream file(manifest_path, std::ios::out | std::ios::binary | std::ios::trunc);
    file << manifest.dump(4);
}

/// Restates the served hashes over the bytes actually on disk, which line endings make local.
inline void rehash_version_manifests(const fs::path& root)
{
    boost::system::error_code ec;
    for (fs::recursive_directory_iterator it(root); it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        ASSERT(!ec);
        if (!fs::is_regular_file(it->status()) || it->path().filename().string() != "manifest.json") {
            continue;
        }

        nlohmann::json manifest = nlohmann::json::parse(read_file(it->path()));
        for (nlohmann::json& entry : manifest) {
            const fs::path file =
                it->path().parent_path() / entry.at("filename").get<std::string>();
            ASSERT(fs::exists(file));
            entry["filehash"] = file_sha256(file);
        }

        boost::nowide::ofstream file(
            it->path(), std::ios::out | std::ios::binary | std::ios::trunc
        );
        file << manifest.dump(4);
    }
}

/// Captures every result the interactor dispatches so a test can assert on it afterwards.
class CaptureListener : public IPresetUpdaterResultListener
{
public:
    explicit CaptureListener(PresetUpdaterInteractor& interactor) : m_interactor(interactor)
    {
        m_interactor.add_listener<IPresetUpdaterResultListener>(this);
    }

    /// The listener dies before the fixture, and the dispatcher still runs what it has queued.
    ~CaptureListener() override
    {
        m_interactor.remove_listener<IPresetUpdaterResultListener>(this);
    }

    CaptureListener(const CaptureListener&)            = delete;
    CaptureListener& operator=(const CaptureListener&) = delete;

    void reset()
    {
        m_has_result = false;
        m_error      = false;
        m_performed  = false;
        m_error_body.clear();
        m_reconfigurations.reset();
        m_repos.clear();
        m_warnings.clear();
        m_finished.clear();
        m_payload_order.clear();
    }

    bool has_result() const { return m_has_result; }
    bool got_error() const { return m_error; }
    const std::string& error_body() const { return m_error_body; }
    bool performed() const { return m_performed; }
    const std::optional<PresetUpdaterReconfigurationList>& reconfigurations() const
    {
        return m_reconfigurations;
    }
    const SharedPresetUpdaterRepositoryInfoVector& repos() const { return m_repos; }
    const std::vector<PresetUpdaterWarning>& warnings() const { return m_warnings; }

    void on_preset_updater_error(
        JobId job_id, const std::string& body, PresetUpdaterReason
    ) override
    {
        m_error      = true;
        m_error_body = body;
        m_has_result = true;
        m_payload_order.push_back(job_id);
    }

    void on_preset_updater_reconfigurations_list(
        JobId job_id,
        const PresetUpdaterReconfigurationList& reconfigurations,
        const std::vector<PresetUpdaterWarning>& warnings,
        VerboseStyle
    ) override
    {
        m_reconfigurations = reconfigurations;
        m_warnings         = warnings;
        m_has_result       = true;
        m_payload_order.push_back(job_id);
    }

    void on_preset_updater_forced_reconfigurations_list(
        JobId job_id,
        const PresetUpdaterReconfigurationList& reconfigurations,
        const std::vector<PresetUpdaterWarning>& warnings
    ) override
    {
        m_reconfigurations = reconfigurations;
        m_warnings         = warnings;
        m_has_result       = true;
        m_payload_order.push_back(job_id);
    }

    void on_preset_updater_reconfigurations_performed(
        JobId job_id, const std::vector<PresetUpdaterWarning>& warnings
    ) override
    {
        m_performed  = true;
        m_warnings   = warnings;
        m_has_result = true;
        m_payload_order.push_back(job_id);
    }

    void on_preset_updater_status(JobId, const std::string&, int, unsigned, VerboseStyle) override
    {}

    void on_preset_updater_repository_info_vector(
        JobId job_id,
        const SharedPresetUpdaterRepositoryInfoVector& descriptor,
        const std::vector<PresetUpdaterWarning>& warnings
    ) override
    {
        store_repos(job_id, descriptor, warnings);
    }

    void on_preset_updater_repository_selection_performed(
        JobId job_id,
        const SharedPresetUpdaterRepositoryInfoVector& descriptor,
        const std::vector<PresetUpdaterWarning>& warnings
    ) override
    {
        store_repos(job_id, descriptor, warnings);
    }

    void on_preset_updater_job_finished(JobId job_id, JobState state) override
    {
        m_finished.emplace_back(job_id, state);
    }

    const std::vector<std::pair<JobId, JobState>>& finished() const { return m_finished; }

    size_t finished_count() const { return m_finished.size(); }

    /// The state a job was finished with, or nullopt if it has not finished yet.
    std::optional<JobState> state_of(JobId job_id) const
    {
        for (const auto& entry : m_finished) {
            if (entry.first == job_id) {
                return entry.second;
            }
        }
        return std::nullopt;
    }

    /// Ids of the jobs that finished, in the order they finished.
    std::vector<JobId> finished_order() const
    {
        std::vector<JobId> ids;
        ids.reserve(m_finished.size());
        for (const auto& entry : m_finished) {
            ids.push_back(entry.first);
        }
        return ids;
    }

    /// Ids of the jobs that dispatched a payload, in the order the payloads arrived.
    const std::vector<JobId>& payload_order() const { return m_payload_order; }

    size_t payload_count(JobId job_id) const
    {
        size_t count = 0;
        for (JobId id : m_payload_order) {
            if (id == job_id) {
                ++count;
            }
        }
        return count;
    }

private:
    void store_repos(
        JobId job_id,
        const SharedPresetUpdaterRepositoryInfoVector& descriptor,
        const std::vector<PresetUpdaterWarning>& warnings
    )
    {
        m_repos.assign(descriptor.begin(), descriptor.end());
        m_warnings   = warnings;
        m_has_result = true;
        m_payload_order.push_back(job_id);
    }

    PresetUpdaterInteractor& m_interactor;
    bool m_has_result{false};
    bool m_error{false};
    bool m_performed{false};
    std::string m_error_body;
    std::optional<PresetUpdaterReconfigurationList> m_reconfigurations;
    SharedPresetUpdaterRepositoryInfoVector m_repos;
    std::vector<PresetUpdaterWarning> m_warnings;
    std::vector<std::pair<JobId, JobState>> m_finished;
    std::vector<JobId> m_payload_order;
};

/// Sets up the mock-server world used by the preset updater tests and restores the global dirs it
/// changed on teardown, so the fixture leaves no state behind.
struct Fixture
{
    fs::path resource_dir;
    fs::path data_dir;
    fs::path installed_path;
    fs::path staged_path;
    fs::path shared_runtime_path;
    fs::path resources_repo_path;
    fs::path server_runtime_path;
    fs::path resources_profile_path;
    fs::path temp_dir_path;

    std::shared_ptr<ResponseMutator> mutator_slot = std::make_shared<ResponseMutator>();

    std::string prev_resources_dir;
    std::string prev_data_dir;
    fs::path prev_temp_dir;

    /// Declared between the dispatcher and the project interactor so they are destroyed after the
    /// interactors that cancel jobs through them, and before the dispatcher they hold a reference
    /// to.
    Slic3r::App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{dispatcher};
    Tests::JobManagerScope job_manager_scope{dispatcher};
    Slic3r::Domain::Workbench workbench;
    Slic3r::Test::MockThumbnailImageGenerator thumbnail_image_generator;
    Slic3r::Biz::ProjectInteractor project_interactor;

    /// @param suffix keeps the scratch directories of different test files apart.
    explicit Fixture(const std::string& suffix = "actions") :
        project_interactor(workbench, dispatcher, thumbnail_image_generator)
    {
        boost::nowide::nowide_filesystem();

        prev_resources_dir = Slic3r::resources_dir();
        prev_data_dir      = Slic3r::data_dir();
        prev_temp_dir      = Slic3r::temp_dir();

        Slic3r::Biz::Network::configure_http_factory_with_mock();

        resource_dir = ::Tests::get_datadir();
        Slic3r::set_resources_dir(resource_dir.string());
        Slic3r::set_data_dir((resource_dir / ("datadir_" + suffix)).string());
        data_dir = fs::path(Slic3r::data_dir());

        installed_path         = data_dir / "presets" / "local";
        staged_path            = data_dir / "update_sync";
        shared_runtime_path    = data_dir / "shared_runtime";
        resources_repo_path    = resource_dir / "presets" / k_repo_name;
        server_runtime_path    = resource_dir / "server" / ("runtime_" + suffix);
        resources_profile_path = resource_dir / "preset updater";
        temp_dir_path          = resource_dir / ("temp_" + suffix);

        Network::ServiceConfig::instance().set_preset_repo_url(k_server_addr);

        fs::remove_all(data_dir);
        fs::remove_all(resources_repo_path);
        fs::remove_all(server_runtime_path);
        fs::remove_all(temp_dir_path);

        fs::create_directories(installed_path);
        fs::create_directories(staged_path);
        fs::create_directories(shared_runtime_path);
        fs::create_directories(resources_repo_path);
        fs::create_directories(server_runtime_path);
        fs::create_directories(temp_dir_path);

        Slic3r::set_temp_dir(temp_dir_path);

        fs::copy_file(
            resources_profile_path / "RepositoryManifest.json",
            shared_runtime_path / "RepositoryManifest.json",
            fs::copy_options::overwrite_existing
        );

        auto slot                     = mutator_slot;
        const fs::path server_runtime = server_runtime_path;
        Network::MockHttpProvider::get().set_create_fn(
            [server_runtime, slot](
                Network::IHttp::RequestMethod method,
                const std::string& url,
                Network::IHttp::RetryFn retryfn
            ) -> std::unique_ptr<Network::MockHttp> {
                ASSERT(method == Network::IHttp::RequestMethod::Get);
                std::unique_ptr<Network::MockHttp> ret =
                    std::make_unique<Network::MockHttp>(url, retryfn);
                ret->set_perform_override_fn(
                    [url, server_runtime, slot](Network::MockHttp* http, const Network::HttpRetryOpt&) {
                        std::string aux(url);
                        if (aux.starts_with(k_server_addr)) {
                            aux.replace(0, k_server_addr.length(), "");
                        }
                        const fs::path path = server_runtime / aux;
                        boost::nowide::ifstream file(path, std::ios::in | std::ios::binary);
                        if (file.is_open()) {
                            std::stringstream buffer;
                            buffer << file.rdbuf();
                            std::string body = buffer.str();
                            unsigned status  = 200;
                            if (slot && *slot) {
                                (*slot)(url, body, status);
                            }
                            http->invoke_complete(body, status);
                        } else {
                            http->invoke_error("", "File not found: " + path.string(), 404);
                        }
                    }
                );
                ret->default_allow_timeout = NAMED_ALLOW_CALL(*ret, timeout_total_mock(trompeloeil::_));
                ret->default_allow_size_limit = NAMED_ALLOW_CALL(*ret, size_limit_mock(trompeloeil::_));
                return ret;
            }
        );

        Platform::PlatformServices::instance().set_secret_store(std::make_unique<SecretStoreDummy>());
    }

    ~Fixture()
    {
        // A destructor body runs before the members are destroyed, so the interactor is still
        // alive here and a job may still be writing into the directories removed below. Stop and
        // join it first, otherwise remove_all races the job thread.
        project_interactor.preset_updater_interactor().shutdown();
        dispatcher.close();

        // Non-throwing overloads on purpose: a destructor that throws terminates the whole run.
        boost::system::error_code ec;
        fs::remove_all(resources_repo_path, ec);
        fs::remove_all(data_dir, ec);
        fs::remove_all(server_runtime_path, ec);
        fs::remove_all(temp_dir_path, ec);
        Slic3r::set_resources_dir(prev_resources_dir);
        Slic3r::set_data_dir(prev_data_dir);
        Slic3r::set_temp_dir(prev_temp_dir);
    }

    PresetUpdaterInteractor& interactor()
    {
        return project_interactor.preset_updater_interactor();
    }

    void set_mutator(ResponseMutator m) { *mutator_slot = std::move(m); }

    void put_resources(const std::string& version)
    {
        copy_dir_content_local(
            resources_profile_path / ("resource" + version) / k_repo_name, resources_repo_path
        );
    }

    void put_server(const std::string& version)
    {
        copy_dir_content_local(resources_profile_path / ("server" + version), server_runtime_path);
        rehash_version_manifests(server_runtime_path);
    }

    void put_installed(const std::string& version)
    {
        copy_dir_content_local(resources_profile_path / ("resource" + version), installed_path);
    }

    /// Installs the presets of a version under a repo id of your choosing, which is the directory
    /// name the updater reads the source id from.
    void put_installed_as(const std::string& repo_id, const std::string& version)
    {
        copy_dir_content_local(
            resources_profile_path / ("resource" + version) / k_repo_name,
            installed_path / repo_id
        );
    }

    void put_staged(const std::string& version)
    {
        copy_dir_content_local(resources_profile_path / ("resource" + version), staged_path);
    }

    bool wait(CaptureListener& listener, std::chrono::seconds timeout = 20s)
    {
        return pump([&listener]() { return listener.has_result(); }, timeout);
    }

    /// Pumps the dispatcher until at least count jobs have reported a terminal state.
    bool wait_for_finished(
        CaptureListener& listener, size_t count, std::chrono::seconds timeout = 20s
    )
    {
        return pump([&listener, count]() { return listener.finished_count() >= count; }, timeout);
    }

    bool pump(const std::function<bool()>& done, std::chrono::seconds timeout = 20s)
    {
        const auto start = std::chrono::high_resolution_clock::now();
        while (true) {
            dispatcher.dispatch_enqueued();
            if (done()) {
                return true;
            }
            if (std::chrono::high_resolution_clock::now() - start > timeout) {
                return false;
            }
            std::this_thread::sleep_for(1ms);
        }
    }
};

} // namespace Slic3r::Biz::PresetUpdater::Tests
