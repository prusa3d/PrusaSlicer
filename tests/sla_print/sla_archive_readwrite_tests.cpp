#include <catch2/catch_test_macros.hpp>
#include <test_utils.hpp>

#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Format/SLAArchiveFormatRegistry.hpp"
#include "libslic3r/Format/SLAArchiveWriter.hpp"
#include "libslic3r/Format/SLAArchiveReader.hpp"
#include "libslic3r/Format/ZipperArchiveImport.hpp"
#include "libslic3r/FileReader.hpp"

#include <boost/filesystem.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <regex>
#include <sstream>

using namespace Slic3r;

TEST_CASE("Archive export test", "[sla_archives]") {
    auto registry = registered_sla_archives();

    for (const char * pname : {"20mm_cube", "extruder_idler"})
    for (const ArchiveEntry &entry : registry) {
        INFO(std::string("Testing archive type: ") + entry.id + " -- writing...");
        SLAPrint print;
        SLAFullPrintConfig fullcfg;

        auto m = FileReader::load_model(TEST_DATA_DIR PATH_SEPARATOR + std::string(pname) + ".obj");

        fullcfg.printer_technology.setInt(ptSLA); // FIXME this should be ensured
        fullcfg.set("sla_archive_format", entry.id);
        fullcfg.set("supports_enable", false);
        fullcfg.set("pad_enable", false);

        DynamicPrintConfig cfg;
        cfg.apply(fullcfg);

        print.set_status_callback([](const PrintBase::SlicingStatus&) {});
        print.apply(m, cfg);
        print.process();

        ThumbnailsList thumbnails;
        auto outputfname = std::string("output_") + pname + "." + entry.ext;

        print.export_print(outputfname, thumbnails, pname);

        // Not much can be checked about the archives...
        REQUIRE(boost::filesystem::exists(outputfname));

        double vol_written = m.mesh().volume();

        if (entry.rdfactoryfn) {
            INFO(std::string("Testing archive type: ") + entry.id + " -- reading back...");
            indexed_triangle_set its;
            DynamicPrintConfig cfg;

            try {
                // Leave format_id deliberetaly empty, guessing should always
                // work here.
                import_sla_archive(outputfname, "", its, cfg);
            } catch (...) {
                REQUIRE(false);
            }

            // its_write_obj(its, (outputfname + ".obj").c_str());

            REQUIRE(!cfg.empty());
            REQUIRE(!its.empty());

            double vol_read = its_volume(its);
            double rel_err  = std::abs(vol_written - vol_read) / vol_written;
            REQUIRE(rel_err < 0.1);
        }
    }
}

TEST_CASE("PM7M archive content verification", "[sla_archives]") {
    SLAPrint print;
    SLAFullPrintConfig fullcfg;

    auto m = FileReader::load_model(TEST_DATA_DIR PATH_SEPARATOR + std::string("20mm_cube.obj"));

    fullcfg.printer_technology.setInt(ptSLA);
    fullcfg.set("supports_enable", false);
    fullcfg.set("pad_enable", false);

    fullcfg.set_deserialize_strict({
        {"sla_archive_format", "pm7m"},
        {"display_pixels_x", "6480"},
        {"display_pixels_y", "3600"},
        {"display_width", "298.08"},
        {"display_height", "165.6"},
        {"display_orientation", "landscape"},
        {"display_mirror_x", "1"},
        {"display_mirror_y", "0"},
        {"max_print_height", "300"},
    });

    DynamicPrintConfig cfg;
    cfg.apply(fullcfg);

    print.set_status_callback([](const PrintBase::SlicingStatus &) {});
    print.apply(m, cfg);
    print.process();

    ThumbnailsList thumbnails;
    std::string fname = "output_pm7m_test.pm7m";

    print.export_print(fname, thumbnails, "20mm_cube");

    REQUIRE(boost::filesystem::exists(fname));

    // includes={""} matches all entries, excludes={} excludes none.
    // Note: read_zipper_archive lowercases entry names.
    auto arch = read_zipper_archive(fname, {""}, {});

    auto find_entry = [&](const std::string &substr) -> const EntryBuffer * {
        for (const auto &e : arch.entries)
            if (e.fname.find(substr) != std::string::npos)
                return &e;
        return nullptr;
    };

    SECTION("ZIP contains all expected entries") {
        REQUIRE(find_entry("anycubic_photon_resins.pwsp") != nullptr);
        REQUIRE(find_entry("print_info.json") != nullptr);
        REQUIRE(find_entry("layers_controller.conf") != nullptr);
        REQUIRE(find_entry("software_info.conf") != nullptr);
        REQUIRE(find_entry("scene.slice") != nullptr);
        REQUIRE(find_entry("layer_images/layer_") != nullptr);
    }

    SECTION("Settings JSON has correct machine parameters") {
        const EntryBuffer *pwsp = find_entry("anycubic_photon_resins.pwsp");
        REQUIRE(pwsp != nullptr);

        std::string json_str(pwsp->buf.begin(), pwsp->buf.end());
        std::istringstream ss(json_str);
        boost::property_tree::ptree pt;
        boost::property_tree::json_parser::read_json(ss, pt);

        CHECK(pt.get<std::string>("machine_type.name") == "Anycubic Photon Mono M7 Max");
        CHECK(pt.get<int>("machine_type.res_x") == 6480);
        CHECK(pt.get<int>("machine_type.res_y") == 3600);
        CHECK(pt.get<std::string>("machine_type.key_suffix") == "pm7m");
        CHECK(pt.get<std::string>("machine_type.key_image_format") == "pw0Img");

        // the printer deserializes "version" fields as strings, not numbers.
        // Verify they are quoted in the raw JSON output.
        CHECK(json_str.find("\"version\": \"3\"") != std::string::npos);
    }

    SECTION("Layers JSON has correct structure") {
        const EntryBuffer *layers = find_entry("layers_controller.conf");
        REQUIRE(layers != nullptr);

        std::string json_str(layers->buf.begin(), layers->buf.end());
        std::istringstream ss(json_str);
        boost::property_tree::ptree pt;
        boost::property_tree::json_parser::read_json(ss, pt);

        int count = pt.get<int>("count");
        CHECK(count > 0);

        auto &paras = pt.get_child("paras");
        CHECK(paras.size() == static_cast<size_t>(count));

        // Verify first layer entry has required fields
        auto first = paras.begin();
        REQUIRE(first != paras.end());
        CHECK(first->second.get<float>("exposure_time") > 0.f);
        CHECK(first->second.get<int>("layer_index") == 0);
        CHECK(first->second.get<float>("layer_thickness") > 0.f);
    }

    SECTION("scene.slice has correct magic") {
        const EntryBuffer *scene = find_entry("scene.slice");
        REQUIRE(scene != nullptr);
        REQUIRE(scene->buf.size() >= 16);

        std::string magic(scene->buf.begin(), scene->buf.begin() + 13);
        CHECK(magic == "ANYCUBIC-PWSZ");
    }

    SECTION("Settings JSON has correct value types for the printer compatibility") {
        // - float/int fields must be unquoted numbers, not strings
        // - string fields (version, code) must be quoted
        // - user_resins must be [] not ""

        const EntryBuffer *pwsp = find_entry("anycubic_photon_resins.pwsp");
        REQUIRE(pwsp != nullptr);

        std::string json(pwsp->buf.begin(), pwsp->buf.end());

        // Helper: extract the JSON array content for a given key name.
        // Returns the text between [ and ] for patterns like "key": [...]
        auto extract_array = [&](const std::string &key) -> std::string {
            std::regex re("\"" + key + "\"\\s*:\\s*\\[([^\\]]*)\\]");
            std::smatch m;
            if (std::regex_search(json, m, re))
                return m[1].str();
            return {};
        };

        // Helper: check that every element in a JSON array is an unquoted
        // number (int or float, possibly negative). Fails if any element
        // is a quoted string like "0.024" instead of 0.024.
        auto check_numeric_array = [&](const std::string &key) {
            INFO("Checking numeric array: " + key);
            std::string arr = extract_array(key);
            REQUIRE(!arr.empty());
            // Each comma-separated element must be an unquoted number
            std::regex re_elem("\\s*(-?[0-9]+\\.?[0-9]*)\\s*");
            std::string remainder = arr;
            // Remove valid unquoted numbers and commas; nothing should be left
            remainder = std::regex_replace(remainder, re_elem, "");
            // Only commas and whitespace should remain after removing numbers
            std::string cleaned = std::regex_replace(remainder, std::regex("[,\\s]"), "");
            CHECK(cleaned.empty());
            // Specifically ensure no quoted strings exist in the array
            CHECK(arr.find('"') == std::string::npos);
        };

        // Helper: check that every element in a JSON array is an unquoted integer
        auto check_int_array = [&](const std::string &key) {
            INFO("Checking int array: " + key);
            std::string arr = extract_array(key);
            REQUIRE(!arr.empty());
            std::regex re_elem("\\s*(-?[0-9]+)\\s*");
            std::string remainder = std::regex_replace(arr, re_elem, "");
            std::string cleaned = std::regex_replace(remainder, std::regex("[,\\s]"), "");
            CHECK(cleaned.empty());
            CHECK(arr.find('"') == std::string::npos);
        };

        // Helper: check a scalar field is an unquoted number
        auto check_numeric_scalar = [&](const std::string &key) {
            INFO("Checking numeric scalar: " + key);
            // Match "key": <number> (no quotes around value)
            std::regex re_ok("\"" + key + "\"\\s*:\\s*-?[0-9]+\\.?[0-9]*\\s*[,}\\n]");
            CHECK(std::regex_search(json, re_ok));
            // Ensure it's NOT a quoted string
            std::regex re_bad("\"" + key + "\"\\s*:\\s*\"-?[0-9]");
            CHECK_FALSE(std::regex_search(json, re_bad));
        };

        // Helper: check a field is a quoted string
        auto check_string_field = [&](const std::string &key) {
            INFO("Checking string field: " + key);
            std::regex re("\"" + key + "\"\\s*:\\s*\"[^\"]*\"");
            CHECK(std::regex_search(json, re));
        };

        // machine_type float[] arrays
        check_numeric_array("prev_back_color");
        check_numeric_array("prev_model_color");
        check_numeric_array("prev_supports_color");
        check_numeric_array("prev2_back_color");
        check_numeric_array("cloudprev_back_color");

        // machine_type int[] arrays
        check_int_array("prev_image_size");
        check_int_array("prev2_image_size");
        check_int_array("cloudprev_imag_size");

        // machine_type scalar numerics
        check_numeric_scalar("res_x");
        check_numeric_scalar("res_y");
        check_numeric_scalar("xy_pixel");
        check_numeric_scalar("xy_pixel_y");
        check_numeric_scalar("max_samples");
        check_numeric_scalar("property");
        check_numeric_scalar("print_xsize");
        check_numeric_scalar("print_ysize");
        check_numeric_scalar("print_zsize");
        check_numeric_scalar("max_file_version");

        // firmware_calc_print_time_paras float[] arrays
        check_numeric_array("MACHINE_AXIS_STEPS_PER_UNIT");
        check_numeric_array("MACHINE_MAX_ACCELERATION");
        check_numeric_array("MACHINE_MAX_FEEDRATE");
        check_numeric_array("MACHINE_MODE_ACCELERATION");
        check_numeric_array("LAYER_COMPENSATE");
        check_numeric_array("HEIGHT_COMPENSATE");
        check_numeric_array("TIMES_COMPENSATE");

        // firmware_calc_exp_time_paras (the reported failure)
        check_numeric_array("precision_range_branch");
        check_numeric_array("precision_coeff_value"); // contains -0.2
        check_numeric_scalar("precision_per_volume");
        check_numeric_scalar("energy_coeff");
        check_numeric_scalar("machine_exposure_ton");

        // String fields that the printer expects as quoted strings
        check_string_field("version");
        check_string_field("code");

        // user_resins must be an empty array [], not ""
        {
            std::regex re_arr("\"user_resins\"\\s*:\\s*\\[\\s*\\]");
            CHECK(std::regex_search(json, re_arr));
        }

        // Float values must not have excessive precision noise
        // The printer firmware may reject values like 0.05000000074505806.
        // Verify that known float constants serialize cleanly.
        CHECK(json.find("45.999") == std::string::npos);     // xy_pixel should be 46
        CHECK(json.find("298.079") == std::string::npos);    // print_xsize should be 298.08
        CHECK(json.find("0.2800000") == std::string::npos);  // color values should be clean
        CHECK(json.find("0.05000000") == std::string::npos); // layer_thickness should be 0.05
        // No float should have more than 6 decimal digits in the settings JSON
        {
            std::regex re_long_float("-?[0-9]+\\.[0-9]{7,}");
            CHECK_FALSE(std::regex_search(json, re_long_float));
        }

        // Currency must use unicode escape, not literal UTF-8
        CHECK(json.find("\\u20ac") != std::string::npos);

        // JSON must end with a newline
        CHECK(!json.empty());
        CHECK(json.back() == '\n');
    }

    boost::filesystem::remove(fname);
}
