#include "Slic3r/Biz/Network/MockHttp.hpp"
#include "Slic3r/Biz/Network/MockHttpFactory.hpp"

#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/SecretStoreDummy.hpp"
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/TestData.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Biz/Slicing/TestUtils.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Semver.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/PresetUpdater/TestPresetUpdaterListener.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/nowide/filesystem.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/nowide/fstream.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace Slic3r::Biz;
namespace fs = boost::filesystem;

bool wait_for_updater(
    PresetUpdater::TestPresetUpdaterListener& updater,
    const std::chrono::seconds& timeout,
    Slic3r::App::Platform::StdMainThreadDispatcher& dispatcher
)
{
    const auto start{std::chrono::high_resolution_clock::now()};
    while (true) {
        dispatcher.dispatch_enqueued();
        if (updater.has_result()) {
            return true;
        }
        const auto now{std::chrono::high_resolution_clock::now()};
        if (now - start > timeout) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
}


void copy_dir_content(const fs::path& from, const fs::path& to)
{
    ASSERT (fs::exists(from) && fs::is_directory(from));

    if (!fs::exists(to)) {
        fs::create_directory(to);
    }

    boost::system::error_code ec;

    for (fs::recursive_directory_iterator it(from);
         it != fs::recursive_directory_iterator();
         it.increment(ec))
    {
        ASSERT (!ec);
        // Get the path relative to the source directory
        fs::path relative_path = it->path().lexically_relative(from);
        fs::path dest_path = to / relative_path;
        // Handle directories
        if (fs::is_directory(it->status())) {
            if (!fs::exists(dest_path)) {
                fs::create_directories(dest_path);
            }
        }
        // Handle regular files
        else if (fs::is_regular_file(it->status())) {
            fs::copy_file(it->path(), dest_path, fs::copy_options::overwrite_existing);
        }
    }
}

struct ProjectInteractorWrapper
{
    ProjectInteractorWrapper()
        : workbench()
        , dispatcher()
        , app_instance_message_handler_scope(dispatcher)
        , job_manager_scope(dispatcher)
        , thumbnail_image_generator()
        , project_interactor(workbench, dispatcher, thumbnail_image_generator)
    {
    }

    ~ProjectInteractorWrapper()
    {
        dispatcher.close();
    }

    /// Declared between the dispatcher and the project interactor so they are destroyed after the
    /// interactors that cancel jobs through them, and before the dispatcher they hold a reference
    /// to.
    Slic3r::App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope;
    Tests::JobManagerScope job_manager_scope;
    Slic3r::Domain::Workbench workbench;
    Slic3r::Test::MockThumbnailImageGenerator thumbnail_image_generator;
    Slic3r::Biz::ProjectInteractor project_interactor;
};

TEST_CASE("PresetUpdater stage updates", "[preset_updater]")
{
    boost::nowide::nowide_filesystem();

    Slic3r::Biz::Network::configure_http_factory_with_mock();

    const std::string repo_name = "test_repo";
    const std::string vendor_name = "TestVendor";

    const fs::path resource_dir  = Tests::get_datadir();
    Slic3r::set_resources_dir(resource_dir.string());
    Slic3r::set_data_dir((resource_dir / "datadir").string());
    const fs::path data_dir = fs::path(Slic3r::data_dir());
    
    const fs::path installed_path = data_dir / "presets" / "local";
    const fs::path staged_path = data_dir / "update_sync";
    const fs::path shared_runtime_path = data_dir / "shared_runtime";
    const fs::path resources_repo_path = resource_dir / "presets" / repo_name;
    
    const fs::path server_runtime_path = resource_dir / "server" / "runtime";
    const fs::path resources_profile_path = resource_dir / "preset updater";
    const fs::path temp_dir_path = resource_dir / "temp";

    Network::ServiceConfig::instance().set_preset_repo_url("http://localhost:8000/");

    fs::create_directories(installed_path);
    fs::create_directories(staged_path);
    fs::create_directories(shared_runtime_path);
    fs::create_directories(resources_repo_path);
    fs::create_directories(server_runtime_path);
    fs::create_directories(temp_dir_path);

    ASSERT(fs::exists(data_dir));
    ASSERT(fs::exists(resource_dir));
    ASSERT(fs::exists(resources_profile_path));
    ASSERT(fs::exists(temp_dir_path));

     Slic3r::set_temp_dir(temp_dir_path);

    fs::copy_file(resources_profile_path / "RepositoryManifest.json", shared_runtime_path / "RepositoryManifest.json", fs::copy_options::overwrite_existing);

    Network::MockHttpProvider::get().set_create_fn(
        [server_runtime_path](
            Network::IHttp::RequestMethod method,
            const std::string& url,
            Network::IHttp::RetryFn retryfn
        ) -> std::unique_ptr<Network::MockHttp>
        {
            ASSERT(method == Network::IHttp::RequestMethod::Get);
            std::unique_ptr<Network::MockHttp> ret =
                std::make_unique<Network::MockHttp>(url, retryfn);

            ret->set_perform_override_fn(
                [url, server_runtime_path](Network::MockHttp* http, const Network::HttpRetryOpt&)
                {
                    std::string aux(url);
                    const std::string server_addr = "http://localhost:8000/";
                    if (aux.starts_with(server_addr)) {
                        aux.replace(0, server_addr.length(), "");
                    }
                    const boost::filesystem::path path = server_runtime_path / aux;
                    boost::nowide::ifstream file(path, std::ios::in | std::ios::binary);

                    if (file.is_open()) {
                        std::stringstream buffer;
                        buffer << file.rdbuf();

                        http->invoke_complete(buffer.str(), 200);

                    } else {
                        http->invoke_error(
                            "",
                            "File not found or unreadable: " + path.string(),
                            404
                        );
                    }
                }
            );

            ret->default_allow_timeout = NAMED_ALLOW_CALL(*ret, timeout_total_mock(trompeloeil::_));
            ret->default_allow_size_limit = NAMED_ALLOW_CALL(*ret, size_limit_mock(trompeloeil::_));

            return ret;
        }
    );

    Platform::PlatformServices& platform_services =
        Platform::PlatformServices::instance();
    platform_services.set_secret_store(std::make_unique<SecretStoreDummy>());

    ProjectInteractorWrapper project_interactor_wrapper;
    PresetUpdater::TestPresetUpdaterListener presetUpdater{project_interactor_wrapper.project_interactor.preset_updater_interactor()};

    {
        auto [resources_version, server_version, staged_version, installed_version, result_version, result_type] =
            GENERATE(
                table<std::string, std::string, std::string, std::string, Slic3r::Semver, PresetUpdater::ReconfigurationResult>({

                    {"100", "", "", "", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "100", "", "", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "", "100", "", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "", "", "100", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                
                    {"100", "100", "", "", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"100", "101", "", "", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"101", "100", "", "", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                
                    {"100", "", "100", "", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"101", "", "100", "", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"100", "", "101", "", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                
                    {"100", "", "", "100", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"100", "", "", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"101", "", "", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},

                    {"", "100", "100", "", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "100", "101", "", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "101", "100", "", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},

                    {"", "100", "", "100", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"", "100", "", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"", "101", "", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},

                    {"", "", "100", "100", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"", "", "100", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"", "", "101", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},

                    {"100", "", "100", "100", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},

                    {"101", "", "100", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
                    {"100", "", "101", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
                    {"100", "", "100", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},

                    {"101", "", "100", "101", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"100", "", "101", "101", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"101", "", "101", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},

                    {"100", "", "101", "102", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"102", "", "100", "101", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                    {"101", "", "102", "100", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                    {"102", "", "101", "100", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                    {"101", "", "100", "102", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"100", "", "102", "101", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                
                    {"", "100", "100", "100", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None}, 

                    {"", "101", "100", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
                    {"", "100", "101", "100", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"", "100", "100", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
      
                    {"", "101", "100", "101", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"", "100", "101", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"", "101", "101", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
  
                    {"", "100", "101", "102", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"", "102", "100", "101", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                    {"", "101", "102", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
                    {"", "102", "101", "100", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                    {"", "101", "100", "102", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"", "100", "102", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},

                    {"100", "100", "", "100", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},

                    {"101", "100", "", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::None},
                    {"100", "101", "", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
                    {"100", "100", "", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},

                    {"101", "100", "", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"100", "101", "", "101", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"101", "101", "", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
 
                    {"100", "101", "", "102", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"102", "100", "", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"101", "102", "", "100", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                    {"102", "101", "", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
                    {"101", "100", "", "102", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"100", "102", "", "101", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},

                    {"100", "100", "100", "", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},

                    {"101", "100", "100", "", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"100", "101", "100", "", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"100", "100", "101", "", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},

                    {"101", "100", "101", "", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"100", "101", "101", "", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"101", "101", "100", "", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},

                    {"100", "101", "102", "", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"102", "100", "101", "", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"101", "102", "100", "", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"102", "101", "100", "", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"101", "100", "102", "", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"100", "102", "101", "", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::NewVendor},

                    {"100", "100", "100", "100", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},

                    {"101", "100", "100", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::None},
                    {"100", "101", "100", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
                    {"100", "100", "101", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::None},
                    {"100", "100", "100", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},

                    {"101", "101", "100", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
                    {"101", "100", "101", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::None},
                    {"101", "100", "100", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"100", "101", "101", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
                    {"100", "101", "100", "101", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"100", "100", "101", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                
                    {"100", "101", "101", "101", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    
                    {"101", "100", "101", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"101", "101", "100", "101", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"101", "101", "101", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
                    
                    {"100", "100", "101", "102", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"100", "100", "102", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"100", "101", "100", "102", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"100", "101", "101", "102", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"100", "101", "102", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
                    {"100", "101", "102", "101", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::None},
                    {"100", "101", "102", "102", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"100", "102", "100", "101", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                    {"100", "102", "101", "100", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                    {"100", "102", "101", "101", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},   
                    {"100", "102", "101", "102", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"100", "102", "102", "101", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                    {"101", "100", "100", "102", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"101", "100", "101", "102", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"101", "100", "102", "100", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::None},
                    {"101", "100", "102", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"101", "100", "102", "102", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"101", "101", "100", "102", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"101", "101", "102", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
                    {"101", "102", "100", "100", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                    {"101", "102", "100", "101", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                    {"101", "102", "100", "102", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"101", "102", "101", "100", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                    {"101", "102", "102", "100", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                    {"102", "100", "100", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"102", "100", "101", "100", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::None},
                    {"102", "100", "101", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"102", "100", "101", "102", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"102", "100", "102", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"102", "101", "100", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
                    {"102", "101", "100", "101", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::None},
                    {"102", "101", "100", "102", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"102", "101", "101", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
                    {"102", "101", "102", "101", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::None},
                    {"102", "102", "100", "101", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                    {"102", "102", "100", "101", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},

                    {"100", "101", "102", "103", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"100", "101", "103", "102", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"100", "102", "101", "103", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"100", "102", "103", "101", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                    {"100", "103", "101", "102", Slic3r::Semver{1,0,3}, PresetUpdater::ReconfigurationResult::Update},
                    {"100", "103", "102", "101", Slic3r::Semver{1,0,3}, PresetUpdater::ReconfigurationResult::Update},
                    {"101", "100", "102", "103", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"101", "100", "103", "102", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"101", "102", "100", "103", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"101", "102", "103", "100", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                    {"101", "103", "100", "102", Slic3r::Semver{1,0,3}, PresetUpdater::ReconfigurationResult::Update},
                    {"101", "103", "102", "100", Slic3r::Semver{1,0,3}, PresetUpdater::ReconfigurationResult::Update},
                    {"102", "100", "101", "103", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"102", "100", "103", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"102", "101", "100", "103", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"102", "101", "103", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
                    {"102", "103", "100", "101", Slic3r::Semver{1,0,3}, PresetUpdater::ReconfigurationResult::Update},
                    {"102", "103", "101", "100", Slic3r::Semver{1,0,3}, PresetUpdater::ReconfigurationResult::Update},
                    {"103", "100", "101", "102", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"103", "100", "102", "101", Slic3r::Semver{1,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"103", "101", "100", "102", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"103", "101", "102", "100", Slic3r::Semver{1,0,1}, PresetUpdater::ReconfigurationResult::Update},
                    {"103", "102", "100", "101", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                    {"103", "102", "101", "100", Slic3r::Semver{1,0,2}, PresetUpdater::ReconfigurationResult::Update},
                
                    {"200", "", "", "", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "200", "", "", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "", "200", "", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "", "", "200", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},

                    {"201", "", "", "", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "201", "", "", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "", "201", "", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "", "", "201", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                
                
                    {"200", "200", "", "", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"201", "201", "", "", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"200", "201", "", "", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"201", "200", "", "", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                
                    {"200", "", "200", "", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"201", "", "201", "", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"201", "", "200", "", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"200", "", "201", "", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                
                    {"200", "", "", "200", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"201", "", "", "201", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"200", "", "", "201", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"201", "", "", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::ForcedUpdate},

                    {"", "200", "200", "", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "201", "201", "", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "200", "201", "", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "201", "200", "", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},

                    {"", "200", "", "200", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"", "201", "", "201", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"", "200", "", "201", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"", "201", "", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::ForcedUpdate},

                    {"", "", "200", "200", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"", "", "201", "201", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"", "", "200", "201", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"", "", "201", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::ForcedUpdate},

                    {"200", "", "200", "200", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"201", "", "201", "201", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},

                    {"201", "", "200", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::ForcedUpdate},
                    {"200", "", "201", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::ForcedUpdate},
                    {"200", "", "200", "201", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},

                    {"201", "", "200", "201", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"200", "", "201", "201", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"201", "", "201", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::ForcedUpdate},
                
                    {"", "200", "200", "200", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None}, 
                    {"", "201", "201", "201", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None}, 

                    {"", "201", "200", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::ForcedUpdate},
                    {"", "200", "201", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::None},
                    {"", "200", "200", "201", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
      
                    {"", "201", "200", "201", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"", "200", "201", "201", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"", "201", "201", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::ForcedUpdate},

                    {"200", "200", "", "200", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"201", "201", "", "201", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},

                    {"201", "200", "", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::None},
                    {"200", "201", "", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::ForcedUpdate},
                    {"200", "200", "", "201", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},

                    {"201", "200", "", "201", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"200", "201", "", "201", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"201", "201", "", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::ForcedUpdate},

                    {"200", "200", "200", "", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"201", "201", "201", "", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},

                    {"201", "200", "200", "", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"200", "201", "200", "", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"200", "200", "201", "", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},

                    {"201", "200", "201", "", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"200", "201", "201", "", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"201", "201", "200", "", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::NewVendor},
                
                    {"200", "200", "200", "200", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"201", "201", "201", "201", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},

                    {"201", "200", "200", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::None},
                    {"200", "201", "200", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::ForcedUpdate},
                    {"200", "200", "201", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::None},
                    {"200", "200", "200", "201", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},

                    {"201", "201", "200", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::ForcedUpdate},
                    {"201", "200", "201", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::None},
                    {"201", "200", "200", "201", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"200", "201", "201", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::ForcedUpdate},
                    {"200", "201", "200", "201", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"200", "200", "201", "201", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},         
                    {"200", "201", "201", "201", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},                
                    {"201", "200", "201", "201", Slic3r::Semver{2,0,0}, PresetUpdater::ReconfigurationResult::NotInIndex},
                    {"201", "201", "200", "201", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"201", "201", "201", "200", Slic3r::Semver{2,0,1}, PresetUpdater::ReconfigurationResult::ForcedUpdate},
            
                    {"300", "", "", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "300", "", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "", "300", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "", "", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
 
                    {"301", "", "", "", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"", "301", "", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "", "301", "", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"", "", "", "301", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                
                    {"300", "300", "", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"301", "301", "", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor}, // 301 resources fault
                    {"300", "301", "", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"301", "300", "", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor}, // 301 resources fault
                
                    {"300", "", "300", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"301", "", "301", "", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None}, // 301 resources fault
                    {"301", "", "300", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor}, // 301 resources fault
                    {"300", "", "301", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                
                    {"300", "", "", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"301", "", "", "301", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"300", "", "", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},
                    {"301", "", "", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None}, // 301 resources fault

                    {"", "300", "300", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "301", "301", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "300", "301", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"", "301", "300", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},

                    {"", "300", "", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"", "301", "", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},
                    {"", "300", "", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},
                    {"", "301", "", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},

                    {"", "", "300", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"", "", "301", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},
                    {"", "", "300", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},
                    {"", "", "301", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},

                    {"300", "", "300", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"301", "", "301", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},

                    {"301", "", "300", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"300", "", "301", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"300", "", "300", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},

                    {"301", "", "300", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},
                    {"300", "", "301", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},
                    {"301", "", "301", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                
                    {"", "300", "300", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None}, 
                    {"", "301", "301", "301", Slic3r::Semver{3  ,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade}, 

                    {"", "301", "300", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"", "300", "301", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"", "300", "300", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},
      
                    {"", "301", "300", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},
                    {"", "300", "301", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},
                    {"", "301", "301", "300", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::None},

                    {"300", "300", "", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"301", "301", "", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},

                    {"301", "300", "", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"300", "301", "", "300", Slic3r::Semver{3,0,1}, PresetUpdater::ReconfigurationResult::None},
                    {"300", "300", "", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},

                    {"301", "300", "", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},
                    {"300", "301", "", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},
                    {"301", "301", "", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},

                    {"300", "300", "300", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"301", "301", "301", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},

                    {"301", "300", "300", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"300", "301", "300", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"300", "300", "301", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},

                    {"301", "300", "301", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"300", "301", "301", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                    {"301", "301", "300", "", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::NewVendor},
                
                    {"300", "300", "300", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"301", "301", "301", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},

                    {"301", "300", "300", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"300", "301", "300", "300", Slic3r::Semver{3,0,1}, PresetUpdater::ReconfigurationResult::None},
                    {"300", "300", "301", "300", Slic3r::Semver{3,0,1}, PresetUpdater::ReconfigurationResult::None},
                    {"300", "300", "300", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},

                    {"301", "301", "300", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"301", "300", "301", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                    {"301", "300", "300", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},
                    {"300", "301", "301", "300", Slic3r::Semver{3,0,1}, PresetUpdater::ReconfigurationResult::None},
                    {"300", "301", "300", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},
                    {"300", "300", "301", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},         
                    {"300", "301", "301", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},                
                    {"301", "300", "301", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},
                    {"301", "301", "300", "301", Slic3r::Semver{3,0,0}, PresetUpdater::ReconfigurationResult::ForcedDowngrade},
                    {"301", "301", "301", "300", Slic3r::Semver{0,0,0}, PresetUpdater::ReconfigurationResult::None},
                })
            );
        
        // In current state there is no need to destroy Project interactor or Preset Updater interactor.
        // Simple deleting files and copying new ones sets up correct enviroment for test.
        // This might change later in development.

        fs::remove_all(resources_repo_path);

        for (auto& dir_entry : fs::directory_iterator(staged_path)) {
            fs::remove_all(dir_entry);
        }
        for (auto& dir_entry : fs::directory_iterator(installed_path)) {
            fs::remove_all(dir_entry);
        }
        for (auto& dir_entry : fs::directory_iterator(server_runtime_path)) {
            fs::remove_all(dir_entry);
        }

        if (!resources_version.empty()) {
            copy_dir_content(resources_profile_path / ("resource" + resources_version) / repo_name, resources_repo_path);
        }
        if (!server_version.empty()) {
            copy_dir_content(resources_profile_path / ("server" + server_version), server_runtime_path);
        }
        if (!staged_version.empty()) {
            copy_dir_content(resources_profile_path / ("resource" + staged_version), staged_path);
        }
        if (!installed_version.empty()) {
            copy_dir_content(resources_profile_path / ("resource" + installed_version), installed_path);
        }

        std::set<PresetUpdater::PresetUpdaterReason> expected_reasons;
        if (server_version.empty()) {
            expected_reasons.insert(PresetUpdater::PresetUpdaterReason::SourceListUnavailable);
            expected_reasons.insert(PresetUpdater::PresetUpdaterReason::SourceUnreachable);
        }
        if (resources_version == "301"
            || (staged_version == "301" && resources_version.empty() && server_version.empty()
                && installed_version.empty()))
        {
            expected_reasons.insert(PresetUpdater::PresetUpdaterReason::DataInconsistent);
        }

        presetUpdater.start({result_type, result_version, std::move(expected_reasons)});
        //printf("Case: %s; %s; %s; %s\n",resources_version.c_str() ,server_version.c_str() ,staged_version.c_str() , installed_version.c_str() );
        REQUIRE(wait_for_updater(
            presetUpdater,
            20s,
            project_interactor_wrapper.dispatcher
        ));  
    }

    // cleanup
    fs::remove_all(resources_repo_path);
    fs::remove_all(data_dir);
    fs::remove_all(server_runtime_path);
    fs::remove_all(server_runtime_path.parent_path());
    fs::remove_all(temp_dir_path);
    Slic3r::set_temp_dir({});
}
