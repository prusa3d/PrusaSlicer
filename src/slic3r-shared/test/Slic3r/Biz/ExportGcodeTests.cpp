#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/SecretStoreDummy.hpp"
#include "Slic3r/Biz/FDMResultCache.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/Slicing/TestUtils.hpp"
#include "Slic3r/Biz/Slicing/GCodeUtils.hpp"

#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"

#include "Slic3r/Directories.hpp"
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/ScopedThreadDispatcher.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/nowide/filesystem.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/nowide/fstream.hpp>

using Slic3r::Test::is_gcode_sane;
using namespace Slic3r::Biz;
using namespace trompeloeil;
namespace fs = boost::filesystem;

struct SlicingStatusListener : public Slic3r::Biz::Slicing::IStatusListener
{
    Slic3r::Biz::ProjectInteractor& m_pi;
    std::map<Slic3r::Domain::SlicingId, std::vector<boost::filesystem::path>> m_id_map;
    bool m_to_fdm;

    SlicingStatusListener(
        Slic3r::Biz::ProjectInteractor& pi,
        bool to_fdm
    ) :
        m_pi(pi),
        m_to_fdm(to_fdm)
    {}

    virtual void on_status_changed(
        const Slic3r::Biz::Slicing::StatusUpdate status_update,
        const Slic3r::Domain::SlicingId id
    ) override
    {
        if (status_update.code && status_update.code == Slic3r::Biz::Slicing::StatusCode::Finished) {
            auto it = m_id_map.find(id);
            ASSERT(it != m_id_map.end());
            for (const auto& path : it->second) {
                if (m_to_fdm) {
                    const std::optional<FDMResultRef> fdm_result{m_pi.fdm_result_cache().get_result(id)};
                    ASSERT(fdm_result);
                    m_pi.set_output_dir(id.project_id, path);
                    PhysicalPrinter::PhysicalPrinterConfig config;
                    config.payload = PhysicalPrinter::FileSystemExport{};
                    PrintHost::PrintHostJobData data{
                        fdm_result.value().get().const_gcode(),
                        path,
                        PrintHost::get_export_format_from_extension(path.extension().string())
                    };
                    m_pi.result_export_interactor().perform(std::move(config), std::move(data));
                } else {
                    const std::optional<SLAResultRef> sla_result{m_pi.sla_result_cache().get_result(id)};
                    ASSERT(sla_result);
                    m_pi.set_output_dir(id.project_id, path);
                    PhysicalPrinter::PhysicalPrinterConfig config;
                    config.payload = PhysicalPrinter::FileSystemExport{};
                    PrintHost::PrintHostJobData data{
                        sla_result.value().get().export_data,
                        path,
                        PrintHost::get_export_format_from_extension(path.extension().string())
                    };
                    m_pi.result_export_interactor().perform(std::move(config), std::move(data));
                }
                
            }
        }
    }

    void add_export(const Slic3r::Domain::SlicingId& id, const boost::filesystem::path& path)
    {
        m_id_map[id].push_back(path);
    }
};

struct JobManagerStatusListener :
    public Slic3r::Biz::Platform::JobManager::IJobManagerStatusChangedListener
{
    JobManagerStatusListener(Slic3r::Domain::JobStatus status)
        : watched_status(status)
    {
    }
     
    void on_job_manager_status_changed(
        const Slic3r::Biz::Platform::JobManager::JobManagerStatus& job_manager_status
    ) override
    {
        // Only count "printhost*" export jobs, otherwise other project_interactor jobs may be counted
        for (const auto& [job_name, progress] : job_manager_status)
        {
            if (progress.status == watched_status && job_name.starts_with("printhost")) {
                status_counter.insert(job_name);
            }
        }

    }
    Slic3r::Domain::JobStatus watched_status;
    std::set<std::string> status_counter;
};

bool wait_for_status_count(
    size_t target_count,
    std::set<std::string>& status_counter,
    const std::chrono::seconds& timeout,
    Slic3r::App::Platform::StdMainThreadDispatcher& dispatcher
)
{
    const auto start{std::chrono::high_resolution_clock::now()};
    while (true) {
        dispatcher.dispatch_enqueued();
        if (status_counter.size() == target_count) {
            return true;
        }
        const auto now{std::chrono::high_resolution_clock::now()};
        if (now - start > timeout) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
}

class ThumbnailGenerator : public Slic3r::Biz::Slicing::IThumbnailImageGenerator
{
    virtual std::future<Slic3r::Biz::Slicing::ThumbnailImageResults> enqueue_thumbnail_requests(
        const Slic3r::Biz::Slicing::ThumbnailImageRequests& requests
    ) override
    {
        std::promise<Slic3r::Biz::Slicing::ThumbnailImageResults> promise;
        promise.set_value(Slic3r::Biz::Slicing::ThumbnailImageResults{});
        return promise.get_future();
    }

    void handle_enqueued_requests() override {}
};

Slic3r::Domain::ConfigPack get_config()
{
    Slic3r::Domain::ConfigPackFDM config;
    config.print.items.opt("skirts").set(0);
    return config;
}

class TargetPathGuard
{
public:
    TargetPathGuard(fs::path p) 
        : m_path(std::move(p)) 
    {
        fs::remove(m_path);
    }

    ~TargetPathGuard()
    {
        fs::remove(m_path);
    }

    const fs::path& path() const
    {
        return m_path;
    }

    std::string read_content() const {
        if (!fs::exists(m_path)) {
            return {};
        }
        boost::nowide::ifstream file_stream(m_path);
        file_stream.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        std::stringstream buffer;
        buffer << file_stream.rdbuf();
        return buffer.str();
    }

private:
    fs::path m_path;
};

struct CaseData
{
    Slic3r::Test::ModelOnBed model_on_bed;
    Slic3r::Domain::SlicingId id;
    std::vector<TargetPathGuard> paths;

    CaseData(Slic3r::Test::ModelOnBed mob, Slic3r::Domain::SlicingId sid)
        : model_on_bed(std::move(mob)), id(std::move(sid))
    {
    }
    CaseData(const CaseData&) = delete;
    CaseData& operator=(const CaseData&) = delete;
    CaseData(CaseData&&) = default; 
    CaseData& operator=(CaseData&&) = default;
};

struct ExportGcodeFixture
{
    ExportGcodeFixture()
    {
        boost::nowide::nowide_filesystem();

        std::unique_ptr<SecretStoreDummy> store_dummy = std::make_unique<SecretStoreDummy>();
        Platform::PlatformServices::instance().set_secret_store(std::move(store_dummy));

        Slic3r::set_data_dir(Tests::get_datadir().string());

        Platform::PlatformServices::instance()
            .job_manager()
            .add_listener<Slic3r::Biz::Platform::JobManager::IJobManagerStatusChangedListener>(
                &job_listener
            );

        project_interactor.preset_interactor().load_preset_bundle(
            Preset::IO::BundlePaths::make_test_runtime(Tests::get_datadir())
        );
    }

    Slic3r::Domain::Workbench workbench;
    Slic3r::App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{dispatcher};
    Tests::JobManagerScope job_manager_scope{dispatcher};
    ThumbnailGenerator thumbnail_image_generator;
    ProjectInteractor project_interactor{workbench, dispatcher, thumbnail_image_generator};
    JobManagerStatusListener job_listener{Slic3r::Domain::JobStatus::Finished};
    // Queue must be clear before project_interactor/job_listener are destroyed.
    Tests::ScopedThreadDispatcher thread_dispatcher{dispatcher};
};

TEST_CASE_METHOD(ExportGcodeFixture, "Export gcode", "[export][timeout]")
{
    auto [project_count, export_count, extension, seconds] =
        GENERATE(
            table<size_t, size_t, std::string, std::chrono::seconds>({
                {1, 1, ".gcode",  60s}, // Export single gcode file.
                {1, 1, ".bgcode", 60s}, // Export single bgcode file.
                {3, 1, ".gcode", 100s}, // Export gcode files of multiple projects.
                {1, 3, ".gcode", 100s}, // Export multiple gcode files of 1 project.
                {3, 3, ".gcode", 200s}  // Export a lot.
            })
        );

    auto data_dir              = Tests::get_datadir();
    fs::path preset_bundle_dir = data_dir / "presets";
    fs::path config_dir        = data_dir / "configs";

    SlicingStatusListener slicing_listener{project_interactor, true};
    project_interactor.slicing_interactor().add_listener<Slic3r::Biz::Slicing::IStatusListener>(&slicing_listener);
    // Must close (and thus drain) the dispatcher's queue while slicing_listener is still alive:
    // it can hold pointers to slicing_listener, and this local goes out of scope before the
    // fixture (and its dispatcher.close() in ~ExportGcodeFixture) is torn down.
    Tests::ScopedThreadDispatcher thread_dispatcher_guard{dispatcher};

    std::vector<CaseData> projects;
    for (size_t i = 0; i < project_count; i++) {

        project_interactor.new_project();
        Slic3r::Test::ModelOnBed new_model{Slic3r::Test::get_cubes_model(1, 5)};
        Slic3r::Domain::SlicingId new_id{i, new_model.bed_instance.id().id};
        projects.emplace_back(std::move(new_model), std::move(new_id));
        for (size_t k = 0; k < export_count; k++) {
            projects.back().paths.emplace_back(fs::path(Slic3r::data_dir()) / (std::string("test") + std::to_string(k) + extension));
            slicing_listener.add_export( projects.back().id,  projects.back().paths.back().path());
        }

        //auto config{std::get<Slic3r::Domain::ConfigPackFDM>(projects.back().model_on_bed.config)};

        project_interactor.slicing_interactor().update_process(
            projects.back().model_on_bed.model,
            projects.back().model_on_bed.project_metadata,
            projects.back().model_on_bed.preset_metadata,
            projects.back().model_on_bed.config,
            projects.back().model_on_bed.bed_instance
        );
        project_interactor.slicing_interactor().slice_all();
    }

    REQUIRE(wait_for_status_count(
        project_count * export_count,
        job_listener.status_counter,
        seconds,
        dispatcher
    ));

    for (size_t i = 0; i < project_count; i++) {
        for (size_t k = 0; k < export_count; k++) {
            REQUIRE(fs::exists(projects[i].paths[k].path()));
            std::string gcode;
            REQUIRE_NOTHROW(gcode = projects[i].paths[k].read_content());
            REQUIRE(!gcode.empty());
            if (extension == ".gcode") {
                const auto error{is_gcode_sane(gcode, projects[i].model_on_bed.model)};
                INFO((error ? *error : ""));
                REQUIRE(!error);
            }
        }
    }
}

TEST_CASE_METHOD(ExportGcodeFixture, "Export sla", "[export][timeout]")
{
    auto [project_count, export_count, extension, seconds] =
        GENERATE(
            table<size_t, size_t, std::string, std::chrono::seconds>({
                {1, 1, ".sl1", 30s},  // Export single sl1 file.
                {1, 1, ".sl1s", 30s}, // Export single sl1s file.
                {3, 1, ".sl1", 50s},  // Export sl1 files of multiple projects.
                {1, 3, ".sl1", 50s},  // Export multiple sl1 files of 1 project.
                {3, 3, ".sl1", 100s}  // Export a lot.
            })
        );

    SlicingStatusListener slicing_listener{project_interactor, false};
    project_interactor.slicing_interactor().add_listener<Slic3r::Biz::Slicing::IStatusListener>(&slicing_listener);
    // Must close (and thus drain) the dispatcher's queue while slicing_listener is still alive:
    // it can hold pointers to slicing_listener, and this local goes out of scope before the
    // fixture (and its dispatcher.close() in ~ExportGcodeFixture) is torn down.
    Tests::ScopedThreadDispatcher thread_dispatcher_guard{dispatcher};

    std::vector<CaseData> projects;
    for (size_t i = 0; i < project_count; i++) {

        project_interactor.new_project();
        Slic3r::Test::ModelOnBed new_model {Slic3r::Test::generate_cubes(1, 5), Slic3r::Domain::ConfigPackSLA{}};
        Slic3r::Domain::SlicingId new_id{i, new_model.bed_instance.id().id};
        projects.emplace_back(std::move(new_model), std::move(new_id));
        for (size_t k = 0; k < export_count; k++) {
            projects.back().paths.emplace_back(fs::path(Slic3r::data_dir()) / (std::string("test") + std::to_string(k) + extension));
            slicing_listener.add_export( projects.back().id,  projects.back().paths.back().path());
        }

        project_interactor.slicing_interactor().update_process(
            projects.back().model_on_bed.model,
            projects.back().model_on_bed.project_metadata,
            projects.back().model_on_bed.preset_metadata,
            projects.back().model_on_bed.config,
            projects.back().model_on_bed.bed_instance
        );
        project_interactor.slicing_interactor().slice_all();
    }

    REQUIRE(wait_for_status_count(
        project_count * export_count,
        job_listener.status_counter,
        seconds,
        dispatcher
    ));

    for (size_t i = 0; i < project_count; i++) {
        for (size_t k = 0; k < export_count; k++) {
            REQUIRE(fs::exists(projects[i].paths[k].path()));
            std::string gcode;
            REQUIRE_NOTHROW(gcode = projects[i].paths[k].read_content());
            REQUIRE(!gcode.empty());
            if (extension == ".gcode") {
                const auto error{is_gcode_sane(gcode, projects[i].model_on_bed.model)};
                INFO((error ? *error : ""));
                REQUIRE(!error);
            }
        }
    }
}
