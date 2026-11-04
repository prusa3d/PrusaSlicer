#include <catch2/catch_test_macros.hpp>
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include <boost/filesystem.hpp>
#include <algorithm>
#include <memory>

using namespace Slic3r;

TEST_CASE("Camera settings belong to physical printers", "[Camera]")
{
    const auto& physical_keys = PhysicalPrinter::printer_options();
    const auto& preset_keys = Preset::printer_options();
    for (const char* key : {"camera_type", "camera_url", "camera_device"}) {
        CHECK(std::find(physical_keys.begin(), physical_keys.end(), key) != physical_keys.end());
        CHECK(std::find(preset_keys.begin(), preset_keys.end(), key) == preset_keys.end());
    }
    std::unique_ptr<DynamicPrintConfig> defaults(
        DynamicPrintConfig::new_from_defaults_keys(physical_keys));
    PhysicalPrinter first("First", *defaults);
    PhysicalPrinter second("Second", *defaults);
    CHECK(first.config.opt_string("camera_type") == "none");
    first.config.opt_string("camera_type") = "rtsp";
    first.config.opt_string("camera_url") = "rtsp://camera-a/stream";
    second.config.opt_string("camera_type") = "usb";
    second.config.opt_string("camera_device") = "local-camera-id";
    CHECK(first.config.opt_string("camera_type") == "rtsp");
    CHECK(first.config.opt_string("camera_device").empty());
    CHECK(second.config.opt_string("camera_url").empty());
    CHECK(defaults->opt_string("camera_type") == "none");

    SECTION("Profile serialization preserves source and device identifiers") {
        for (const PhysicalPrinter* printer : {&first, &second}) {
            std::string ini;
            for (const char* key : {"camera_type", "camera_url", "camera_device"})
                ini += std::string(key) + " = " + printer->config.opt_serialize(key) + "\n";
            DynamicPrintConfig loaded(*defaults);
            loaded.load_from_ini_string(ini, ForwardCompatibilitySubstitutionRule::Disable);
            for (const char* key : {"camera_type", "camera_url", "camera_device"})
                CHECK(loaded.opt_string(key) == printer->config.opt_string(key));
        }
    }
    SECTION("Removing a camera does not change another printer or its host") {
        first.config.opt_string("print_host") = "printer-a";
        first.config.opt_string("camera_type") = "none";
        first.config.opt_string("camera_url").clear();
        first.config.opt_string("camera_device").clear();
        CHECK(first.config.opt_string("print_host") == "printer-a");
        CHECK(second.config.opt_string("camera_type") == "usb");
        CHECK(second.config.opt_string("camera_device") == "local-camera-id");
    }
    SECTION("Older profiles receive the no-camera default") {
        DynamicPrintConfig legacy(*defaults);
        legacy.load_from_ini_string("print_host = printer-b\n",
            ForwardCompatibilitySubstitutionRule::Disable);
        CHECK(legacy.opt_string("camera_type") == "none");
        CHECK(legacy.opt_string("camera_url").empty());
        CHECK(legacy.opt_string("camera_device").empty());
    }
}

TEST_CASE("Saving camera edits preserves physical-printer host settings", "[Camera]")
{
    struct TemporaryDirectory
    {
        boost::filesystem::path path = boost::filesystem::temp_directory_path() /
            boost::filesystem::unique_path("prusa-camera-%%%%-%%%%");
        TemporaryDirectory() { boost::filesystem::create_directories(path / "physical_printer"); }
        ~TemporaryDirectory() { boost::system::error_code error; boost::filesystem::remove_all(path, error); }
    } temporary;
    PresetBundle bundle;
    auto& printers = bundle.physical_printers;
    PresetsConfigSubstitutions substitutions;
    printers.load_printers(temporary.path.string(), "physical_printer", substitutions,
        ForwardCompatibilitySubstitutionRule::Disable);
    PhysicalPrinter original("Camera test", printers.default_config());
    original.preset_names.insert("Test preset");
    original.config.opt_string("print_host") = "printer-host";
    original.config.opt_string("printhost_apikey") = "test-only-key";
    printers.save_printer(original);

    // save_printer consumes the edited config. Pass a copy of a stored printer,
    // as the camera editor does, so the live collection is never moved into itself.
    PhysicalPrinter edited = printers.get_selected_printer();
    edited.config.opt_string("camera_type") = "http";
    edited.config.opt_string("camera_url") = "http://camera-host/stream";
    printers.save_printer(edited);
    DynamicPrintConfig saved;
    saved.load_from_ini((temporary.path / "physical_printer/Camera test.ini").string(),
        ForwardCompatibilitySubstitutionRule::Disable);
    CHECK(saved.opt_string("print_host") == "printer-host");
    CHECK(saved.opt_string("printhost_apikey") == "test-only-key");
    CHECK(saved.opt_string("camera_type") == "http");
    CHECK(saved.opt_string("camera_url") == "http://camera-host/stream");
    CHECK(printers.get_selected_printer().preset_names.count("Test preset") == 1);
}
