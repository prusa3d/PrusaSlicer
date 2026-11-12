#include "CLITestUtils.hpp"

#include "Slic3r/App/CLI/CLIRuntime.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Algorithms/ZipArchive.hpp"
#include "Slic3r/Biz/AppInstance/AppInstanceMessageHandlerFactory.hpp"
#include "Slic3r/Biz/Format/STL.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/SecretStoreDummy.hpp"
#include "Slic3r/Domain/Preset/Bundle.hpp"
#include "Slic3r/Domain/Workbench.hpp"

#include <ranges>
#include <stdexcept>
#include <utility>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>

using namespace Slic3r;
using namespace Slic3r::Biz;

using Slic3r::App::CLI::CLIThumbnailImageGenerator;
using Slic3r::Biz::Platform::PlatformServices;
using Slic3r::Biz::ProjectInteractor;
using Slic3r::Biz::SecretStoreDummy;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::TriangleMesh;
using Slic3r::Domain::Workbench;
using Slic3r::Domain::Preset::Bundle;
using Slic3r::Domain::Preset::EvaluatedMaterialPreset;
using Slic3r::Domain::Preset::EvaluatedPrinterPreset;
using Slic3r::Domain::Preset::EvaluatedPrintPreset;
using Slic3r::Domain::Preset::SingleToolEvaluatedMaterialPresets;

namespace Slic3r::App::CLI::Test {

namespace {

struct ProfilePreferences
{
    PrinterTechnology technology;
    std::string print_name_part;
    std::string material_name_part;
};

std::vector<TestProfileSet> get_test_printer_profile_sets(
    const Bundle& preset_bundle,
    const ProfilePreferences& preferences
)
{
    std::vector<TestProfileSet> result;

    for (const std::vector<EvaluatedPrinterPreset>& printer_presets :
         preset_bundle.evaluated_presets | std::views::values)
    {
        for (const EvaluatedPrinterPreset& printer_preset : printer_presets) {
            if (printer_preset.technology() != preferences.technology) {
                continue;
            }

            const std::size_t tool_count = printer_preset.hw_config.tool_count;

            if (printer_preset.prints.empty()) {
                continue;
            }

            // Prefer the print profile named in the preferences, e.g. 0.20mm.
            const EvaluatedPrintPreset* chosen_print_preset = &printer_preset.prints.front();
            for (const EvaluatedPrintPreset& print_preset : printer_preset.prints) {
                if (print_preset.preset.name.find(preferences.print_name_part) != std::string::npos) {
                    chosen_print_preset = &print_preset;
                    break;
                }
            }

            if (chosen_print_preset->materials.empty()
                || chosen_print_preset->materials.front().empty())
            {
                continue;
            }

            // Prefer the material named in the preferences, e.g. Prusament PLA.
            const SingleToolEvaluatedMaterialPresets& slot_materials =
                chosen_print_preset->materials.front();
            const EvaluatedMaterialPreset* chosen_material_preset = &slot_materials.front();
            for (const EvaluatedMaterialPreset& material_preset : slot_materials) {
                if (material_preset.preset.name.find(preferences.material_name_part) != std::string::npos) {
                    chosen_material_preset = &material_preset;
                    break;
                }
            }

            result.push_back(
                TestProfileSet{.printer_profile_name  = printer_preset.hw_config.name,
                               .print_profile_name    = chosen_print_preset->preset.name,
                               .material_profile_name = chosen_material_preset->preset.name,
                               .tool_count            = tool_count});
        }
    }

    if (result.empty()) {
        throw std::runtime_error{"No suitable printers were found in the preset bundle."};
    }
    return result;
}

std::vector<TestProfileSet> load_test_profile_sets(const ProfilePreferences& preferences)
{
    // Self-contained: sets up PlatformServices itself (mirroring CLIRuntime's own
    // setup/teardown), so this can safely run before or after a CLIRuntime.
    PlatformServices& platform_services = PlatformServices::instance();
    platform_services.set_secret_store(std::make_unique<SecretStoreDummy>());
    // A previous CLIRuntime leaves its job manager and message handler behind; they reference
    // the dispatcher replaced below, so they have to go first.
    platform_services.set_job_manager(nullptr);
    platform_services.set_app_instance_message_handler(nullptr);
    platform_services.set_main_thread_dispatcher(
        std::make_unique<App::Platform::StdMainThreadDispatcher>()
    );
    platform_services.set_job_manager(
        std::make_unique<Biz::Platform::JobManager::JobManager>(
            platform_services.main_thread_dispatcher()
        )
    );
    platform_services.set_app_instance_message_handler(
        AppInstance::create_app_instance_message_handler(
            platform_services.main_thread_dispatcher()
        )
    );

    std::vector<TestProfileSet> result;
    {
        Workbench workbench;
        CLIThumbnailImageGenerator thumbnail_image_generator;
        ProjectInteractor project_interactor(
            workbench,
            platform_services.main_thread_dispatcher(),
            thumbnail_image_generator
        );
        project_interactor.preset_interactor().load_preset_bundle(
            Preset::IO::BundlePaths::make_standard_runtime()
        );

        const Bundle& preset_bundle = workbench.preset_bundle();
        result = get_test_printer_profile_sets(preset_bundle, preferences);

        // Queue must be clear before ProjectInteractor can be destroyed.
        platform_services.main_thread_dispatcher().close();
    }
    // project_interactor (and its BackupStore, still registered on the handler) is
    // destroyed above; only now is it safe to tear down the handler/job_manager it used.
    platform_services.set_app_instance_message_handler(nullptr);
    platform_services.set_job_manager(nullptr);

    return result;
}

} // namespace

const std::vector<TestProfileSet>& resolved_profile_sets()
{
    static const std::vector<TestProfileSet> profile_sets = load_test_profile_sets(
        {.technology = PrinterTechnology::FFF, .print_name_part = "0.20mm", .material_name_part = "Prusament PLA"}
    );
    return profile_sets;
}

const std::vector<TestProfileSet>& resolved_sla_profile_sets()
{
    static const std::vector<TestProfileSet> profile_sets = load_test_profile_sets(
        {.technology = PrinterTechnology::SLA, .print_name_part = "0.05", .material_name_part = "Prusament Resin"}
    );
    return profile_sets;
}

ScopedTempDir::ScopedTempDir() :
    m_path(
        boost::filesystem::temp_directory_path()
        / boost::filesystem::unique_path("slic3r-cli-test-%%%%-%%%%")
    )
{
    boost::filesystem::create_directories(m_path);
}

ScopedTempDir::~ScopedTempDir()
{
    boost::system::error_code cleanup_error;
    boost::filesystem::remove_all(m_path, cleanup_error);
}

boost::filesystem::path write_cube_stl(
    const boost::filesystem::path& directory,
    const std::string& file_name,
    const double cube_size_mm
)
{
    const boost::filesystem::path stl_path = directory / file_name;
    const TriangleMesh cube_mesh =
        Algorithms::TriangleMesh::make_cube(cube_size_mm, cube_size_mm, cube_size_mm);
    store_stl(stl_path.string(), cube_mesh, true);
    return stl_path;
}

InitParams make_params(const TestProfileSet& profile_set)
{
    InitParams init_params;
    init_params.input.printer_profile_preset = profile_set.printer_profile_name;
    init_params.input.print_profile_preset   = profile_set.print_profile_name;
    init_params.input.material_profile_presets.emplace_back(profile_set.material_profile_name);
    return init_params;
}

std::size_t count_gcode_files(const boost::filesystem::path& directory)
{
    std::size_t gcode_file_count = 0;
    for (const boost::filesystem::directory_entry& directory_entry :
         boost::filesystem::directory_iterator(directory))
    {
        if (!boost::filesystem::is_regular_file(directory_entry.path())) {
            continue;
        }

        const std::string extension = directory_entry.path().extension().string();
        if (boost::iequals(extension, ".gcode") || boost::iequals(extension, ".bgcode")) {
            ++gcode_file_count;
        }
    }

    return gcode_file_count;
}

std::vector<boost::filesystem::path> find_sla_archives(const boost::filesystem::path& directory)
{
    std::vector<boost::filesystem::path> archives;
    for (const boost::filesystem::directory_entry& directory_entry :
         boost::filesystem::directory_iterator(directory))
    {
        if (!boost::filesystem::is_regular_file(directory_entry.path())) {
            continue;
        }

        const std::string extension = directory_entry.path().extension().string();
        if (boost::iequals(extension, ".sl1") || boost::iequals(extension, ".sl1s")) {
            archives.push_back(directory_entry.path());
        }
    }

    return archives;
}

std::optional<std::string> read_zip_entry(
    const boost::filesystem::path& archive_path,
    const std::string& entry_name
)
{
    using Biz::Algorithms::ZipArchive;
    ZipArchive archive = ZipArchive::open(archive_path.string(), ZipArchive::Mode::Read);
    if (!archive.is_open()) {
        return std::nullopt;
    }

    const int index = archive.locate_file(entry_name);
    mz_zip_archive_file_stat stat;
    if (index < 0 || !archive.stat_file(index, stat)) {
        return std::nullopt;
    }

    std::string content(static_cast<std::size_t>(stat.m_uncomp_size), '\0');
    if (!archive.extract_entry_to(static_cast<mz_uint>(index), content.data(), content.size())) {
        return std::nullopt;
    }
    return content;
}

} // namespace Slic3r::App::CLI::Test
