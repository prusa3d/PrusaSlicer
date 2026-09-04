#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <iostream>

#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Biz/Config/ConfigLegacy.hpp"
#include "Slic3r/Biz/Config/ConfigSerialize.hpp"
#include "Slic3r/Biz/Config/3mf_legacy.hpp"

#include "Slic3r/TestUtils/HwConfigUtils.hpp"
#include "boost/filesystem/path.hpp"
#include "boost/nowide/filesystem.hpp"
#include "Slic3r/Assert.hpp"

#include "boost/nowide/fstream.hpp"
#include "boost/filesystem.hpp"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include "miniz.h"

namespace Slic3r::Test {

namespace fs = boost::filesystem;
using Domain::PrinterTechnology;


class TempFile {
public:
    TempFile() {
        boost::system::error_code ec;
        m_path = fs::temp_directory_path() / fs::unique_path(ec);
        ASSERT(! ec);
        boost::nowide::ofstream file(m_path.string()); // create file
        ASSERT(bool(file));
    }
    ~TempFile() {
        boost::system::error_code ec;
        if (fs::exists(m_path, ec) && ! ec)
            fs::remove(m_path, ec);
    }
    std::string get_path() const { return m_path.string(); }
private:
    fs::path m_path;
};


static std::vector<std::string> compare_files_by_lines(std::istream& a, std::istream& b)
{
    std::set<std::string> lines_a;
    std::set<std::string> lines_b;
    std::string line;
    while (a) {
        std::getline(a, line);
        if (! line.empty())
            lines_a.insert(line);
    }
    while (b) {
        std::getline(b, line);
        if (! line.empty())
            lines_b.insert(line);
    }

    ASSERT(!lines_a.empty());
    ASSERT(!lines_b.empty());

    std::vector<std::string> result;
    std::set_symmetric_difference(
        lines_a.begin(), lines_a.end(), lines_b.begin(), lines_b.end(), std::back_inserter(result)
    );

    return result;
}

static std::vector<std::string> roundtrip_and_get_diff_lines(const fs::path& filename)
{
    Domain::ConfigPack cfg = Biz::load_config_from_legacy_file(filename.string());

    // HwConfig cannot be loaded just from ini, hence the nozzle diameters are lost.
    // Mock the hw config, to mirror the ini.
    Domain::Preset::HwPrinterConfig hw_config{Test::create_dummy_hw_config(1, 0.4)};
    hw_config.feeders[{0}] = Domain::Preset::HwFeederConfig{.slot_count = 5};

    // Serialize the config again in the legacy format.
    std::stringstream ss;
    std::visit([&](auto&& cfg) { ss << Biz::serialize_as_legacy_config(cfg, hw_config); }, cfg);
    ss.seekg(0);

    boost::nowide::ifstream original_stream{filename};

    // Get all lines which are in one file but not in the other.
    return compare_files_by_lines(ss, original_stream);
}

static void remove_lines_which_start_with(std::vector<std::string>& lines, const std::vector<std::string>& allowed_diffs_starts)
{
    lines.erase(std::remove_if(lines.begin(), lines.end(),
        [&allowed_diffs_starts](std::string s) {
            return std::any_of(allowed_diffs_starts.begin(), allowed_diffs_starts.end(),
                [&s](const std::string& m) {
                    boost::trim(s);
                    return boost::starts_with(s, m);
                });
        }), lines.end());
}


TEST_CASE("SLA INI roundtrip", "[config]")
{
    std::vector<std::string> diff_lines = roundtrip_and_get_diff_lines(fs::path(TEST_DATA_DIR) / "test_sla.ini");
    remove_lines_which_start_with(diff_lines, {"# generated", "printer_model", "material_vendor",
        "default_sla_print_profile", "default_sla_material_profile", "compatible_prints",
        "compatible_printers_condition_cummulative", "printer_notes", "printer_settings_id",
        "printer_variant", "sla_material_settings_id", "sla_print_settings_id"});
    REQUIRE(diff_lines.empty());
}

TEST_CASE("FDM INI roundtrip", "[config]")
{
    std::vector<std::string> diff_lines = roundtrip_and_get_diff_lines(fs::path(TEST_DATA_DIR) / "test_fdm.ini");
    remove_lines_which_start_with(
        diff_lines,
        {
            "# generated",
            "compatible_printers_condition_cummulative",
            "default_filament_profile",
            "default_print_profile",
            "filament_settings_id",
            "print_host",
            "print_settings_id",
            "printhost_apikey",
            "printhost_cafile",
            "printer_settings_id",

            // In slicer 3 filament_load_time and filament_unload_time became filament_change_time.
            // Also, 4 new parameters were introduced:
            // - enable_pressure_advance_during_ramming
            // - stuck_filament_detection
            // - filament_ramming_initial_delay
            // - filament_ramming_temperature_delta
            "enable_pressure_advance_during_ramming",
            "filament_change_time",
            "filament_load_time",
            "filament_ramming_initial_delay",
            "filament_ramming_temperature_delta",
            "filament_unload_time",
            "stuck_filament_detection",

            // Since PrusaSlicer 3.0.0, brim_type is the primary on/off control, and brim_width defaults to 5mm.
            // Legacy configs with brim_width = 0 (brim off) are migrated to brim_type = no_brim
            // with brim_width = 5, so these two keys intentionally differ after a roundtrip.
            "brim_type",
            "brim_width",

            // Since PrusaSlicer 3.0.0, support_material is a single enum (SupportMode) that replaces the
            // legacy support_material + support_material_auto bool pair. support_material_auto is consumed
            // by the migration and dropped, so these two keys intentionally differ after a roundtrip.
            "support_material",
            "support_material_auto",
        }
    );
    for (const std::string& line : diff_lines) {
        std::cout << line << "\n";
    }
    REQUIRE(diff_lines.empty());
}

class Zip {
public:

    using FileData = std::vector<std::byte>;

    /** Load zip files to memory */
    Zip(const char* filename) {
        const auto mz_zip_deleter{[](mz_zip_archive* ptr){
            mz_zip_reader_end(ptr);
            delete ptr;
        }};
        const auto mz_zip_creator{[](){
            auto result{new mz_zip_archive};
            memset(result, 0, sizeof(*result));
            return result;
        }};

        const std::unique_ptr<mz_zip_archive, decltype(mz_zip_deleter)>
            zip_archive{mz_zip_creator(), mz_zip_deleter};

        if (!mz_zip_reader_init_file(zip_archive.get(), filename, 0)) {
            throw std::runtime_error{"Could not open zip file!"};
        }

        const mz_uint num_files{mz_zip_reader_get_num_files(zip_archive.get())};


        for (mz_uint i = 0; i < num_files; i++) {
            mz_zip_archive_file_stat file_stat;
            if (!mz_zip_reader_file_stat(zip_archive.get(), i, &file_stat)) {
                throw std::runtime_error{"Failed to read filename from zip!"};
            }

            const auto mz_file_data_deleter{[](void* ptr){
                mz_free(ptr);
            }};
            size_t file_data_size;
            const std::unique_ptr<void, decltype(mz_file_data_deleter)>
                file_data{mz_zip_reader_extract_to_heap(zip_archive.get(), i, &file_data_size, 0), mz_file_data_deleter};

            if (!file_data) {
                throw std::runtime_error{"Failed to read file from zip!"};
            }

            const auto data{static_cast<std::byte*>(file_data.get())};
            m_files.insert({file_stat.m_filename, FileData(data, data + file_data_size)});
        }
    }

    const std::map<std::string, FileData>& files() const {
        return m_files;
    }

private:
    std::map<std::string, FileData> m_files;
};

std::string to_string(const Zip::FileData& data) {
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

std::istringstream as_istream(const Zip::FileData& data) {
    return std::istringstream{to_string(data)};
}

std::vector<std::string> compare_files_by_lines(const Zip::FileData& a, const Zip::FileData& b) {
    std::istringstream a_stream{as_istream(a)};
    std::istringstream b_stream{as_istream(b)};
    return compare_files_by_lines(a_stream, b_stream);
}

std::string to_string(const std::vector<std::string>& vec) {
    std::ostringstream oss;
    for (const std::string& s : vec) {
        oss << s << "\n";
    }
    return oss.str();
}

const std::string config_file{"Metadata/Slic3r_PE.config"};
const std::string model_config_file{"Metadata/Slic3r_PE_model.config"};
const std::string config_ranges_file{"Metadata/Prusa_Slicer_layer_config_ranges.xml"};
const std::string model_file{"3D/3dmodel.model"};

const std::map<std::string, std::vector<std::string>> fdm_whitelist{
    {config_file, {
        "; first_layer_infill_speed",
        "; compatible_printers_condition_cummulative",
        "; default_filament_profile",
        "; default_print_profile",
        "; extruder",
        "; filament_settings_id",
        "; generated",
        "; print_host",
        "; print_settings_id",
        "; printhost_apikey",
        "; printhost_cafile",
        "; printer_settings_id",
        "; enable_pressure_advance_during_ramming",
        "; filament_change_time",
        "; filament_load_time",
        "; filament_ramming_initial_delay",
        "; filament_ramming_temperature_delta",
        "; filament_unload_time",
        "; stuck_filament_detection",
        "; brim_type",
        "; brim_width",
        "; support_material",
        "; support_material_auto",
    }},
    {model_config_file, {
        "<metadata type=\"volume\" key=\"matrix\"",
        "<metadata type=\"object\" key=\"matrix\"",
        "<metadata type=\"volume\" key=\"wipe_into_infill\" value=\"0\"/>",
        "<metadata type=\"object\" key=\"extruder\" value=\"0\"/>",
        "<metadata type=\"object\" key=\"wipe_into_objects\" value=\"0\"/>",
    }},
    {config_ranges_file, {
        "<option opt_key=\"wipe_into_infill\">0</option>"
    }},
    {model_file, {
        "<metadata name=\"Application\">",
        "<metadata name=\"Description\">",
        "<metadata name=\"Title\">",
        "<metadata name=\"CreationDate\">",
        "<metadata name=\"ModificationDate\">",
        "<vertex x="
    }},
};

const std::map<std::string, std::vector<std::string>> sla_whitelist{
    {config_file, {
        "; generated",
        "; printer_model",
        "; material_vendor",
        "; default_sla_print_profile",
        "; default_sla_material_profile",
        "; compatible_prints",
        "; compatible_printers_condition_cummulative",
        "; printer_notes",
        "; printer_settings_id",
        "; printer_variant",
        "; print_host",
        "; printhost_apikey",
        "; printhost_cafile",
        "; sla_material_settings_id",
        "; sla_print_settings_id"
    }},
    {model_config_file, {
        "<metadata type=\"volume\" key=\"matrix\"",
        "<metadata type=\"object\" key=\"matrix\"",

        // Historicaly SLA could contain FDM keys. Now it always contains them.
        "<metadata type=\"object\" key=\"extruder\" value=\"0\"/>",
        "<metadata type=\"object\" key=\"extruder\" value=\"0\"/>",
        "<metadata type=\"volume\" key=\"wipe_into_infill\" value=\"0\"/>",
        "<metadata type=\"object\" key=\"wipe_into_objects\" value=\"0\"/>",
    }},
    {model_file, {
        "<metadata name=\"Application\">",
        "<metadata name=\"Description\">",
        "<metadata name=\"Title\">",
        "<metadata name=\"CreationDate\">",
        "<metadata name=\"ModificationDate\">",
        "<vertex x="
    }},
};

const std::vector<std::string> fdm_files_to_compare{
    "[Content_Types].xml",
    //"Metadata/thumbnail.png",
    "_rels/.rels",
    model_file,
    config_ranges_file,
    "Metadata/Prusa_Slicer_custom_gcode_per_print_z.xml",
    "Metadata/Prusa_Slicer_wipe_tower_information.xml",
    config_file,
    model_config_file
};

const std::vector<std::string> sla_files_to_compare{
    "[Content_Types].xml",
    //"Metadata/thumbnail.png",
    "_rels/.rels",
    model_file,
    "Metadata/Slic3r_PE_sla_drain_holes.txt",
    config_file,
    model_config_file
};

void compare_3mfs(const Zip& a, const Zip& b, const Domain::PrinterTechnology technology) {
    const auto& files_to_comapare{
        technology == PrinterTechnology::FFF ? fdm_files_to_compare : sla_files_to_compare
    };
    for (const std::string& filename : files_to_comapare) {
        std::vector<std::string> diff_lines{
            compare_files_by_lines(a.files().at(filename), b.files().at(filename))
        };
        INFO(filename);
        if (technology == PrinterTechnology::FFF && fdm_whitelist.contains(filename)) {
            remove_lines_which_start_with(diff_lines, fdm_whitelist.at(filename));
        }
        if (technology == PrinterTechnology::SLA && sla_whitelist.contains(filename)) {
            remove_lines_which_start_with(diff_lines, sla_whitelist.at(filename));
        }
        INFO(to_string(diff_lines));
        CHECK(diff_lines.empty());
    }
}

constexpr bool debug_files{false};

TEST_CASE("Legacy FDM 3MF roundtrip", "[config]")
{
    for (const std::string& filename : std::vector<std::string>{"fdm_roundtrip1.3mf", "fdm_roundtrip2.3mf"}) {
        Domain::Model model;
        Domain::ConfigPack cfg;
        Biz::LegacyPresetMetadata preset_metadata;
        boost::optional<Slic3r::Semver> prusaslicer_generator_version;
        Domain::WipeTowersOnBeds wipe_towers;
        Domain::CustomGCodesOnBeds custom_gcodes;
        Biz::VirtualExtrudersConfig virtual_extruders_config;
        const fs::path test_file_path{(fs::path(TEST_DATA_DIR) / fs::path{filename})};
        Slic3rLegacy::load_3mf_legacy(
            test_file_path.string().c_str(),
            cfg,
            preset_metadata,
            &model,
            true,
            prusaslicer_generator_version,
            wipe_towers,
            custom_gcodes,
            virtual_extruders_config
        );

        // Hw printer config is NOT loaded, this mock config must match, what is in the 3mf.
        const Domain::Preset::HwPrinterConfig hw_config{Test::create_dummy_hw_config(5, 0.4)};

        TempFile tmp_file;
        const std::string file_path{debug_files ? "new_fdm.3mf" : tmp_file.get_path()};
        Slic3rLegacy::store_3mf_legacy(
            file_path.c_str(),
            &model,
            std::get<Domain::ConfigPackFDM>(cfg),
            hw_config,
            false,
            wipe_towers,
            custom_gcodes
        );

        const Zip original_3mf{test_file_path.string().c_str()};
        const Zip new_3mf{file_path.c_str()};
        compare_3mfs(original_3mf, new_3mf, PrinterTechnology::FFF);
    }
}

TEST_CASE("Legacy SLA 3MF roundtrip", "[config]")
{
    for (const std::string& filename : std::vector<std::string>{"sla_roundtrip1.3mf", "sla_roundtrip2.3mf"}) {
        Domain::Model model;
        Domain::ConfigPack cfg;
        Biz::LegacyPresetMetadata preset_metadata;
        boost::optional<Slic3r::Semver> prusaslicer_generator_version;
        Domain::WipeTowersOnBeds wipe_towers;
        Domain::CustomGCodesOnBeds custom_gcodes;
        Biz::VirtualExtrudersConfig virtual_extruders_config;
        const fs::path test_file_path{(fs::path(TEST_DATA_DIR) / fs::path{filename})};
        Slic3rLegacy::load_3mf_legacy(test_file_path.string().c_str(), cfg, preset_metadata, &model, true, prusaslicer_generator_version, wipe_towers, custom_gcodes, virtual_extruders_config);

        // Hw printer config is NOT loaded, this mock config must match, what is in the 3mf.
        const Domain::Preset::HwPrinterConfig hw_config{Test::create_dummy_hw_config(5, 0.4)};

        TempFile tmp_file;
        const std::string file_path{debug_files ? "new_sla.3mf" : tmp_file.get_path()};
        Slic3rLegacy::store_3mf_legacy(
            file_path.c_str(),
            &model,
            std::get<Domain::ConfigPackSLA>(cfg),
            hw_config,
            false,
            wipe_towers,
            custom_gcodes
        );

        const Zip original_3mf{test_file_path.string().c_str()};
        const Zip new_3mf{file_path.c_str()};
        compare_3mfs(original_3mf, new_3mf, PrinterTechnology::SLA);
    }
}

}
