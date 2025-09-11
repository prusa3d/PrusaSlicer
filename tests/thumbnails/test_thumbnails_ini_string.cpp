#include <catch2/catch_test_macros.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <libslic3r/GCode/Thumbnails.hpp>

using namespace Slic3r;
using namespace GCodeThumbnails;


static std::string empty_thumbnails()
{
    return "thumbnails = \n"
            "thumbnails_format = ";
}

static std::string valid_thumbnails()
{
    return  "thumbnails = 160x120/JPG, 23x78/QOI, 230x780/JPG\n"
            "thumbnails_format = JPG";
}

static std::string valid_thumbnails2()
{
    return  "thumbnails = 160x120/PNG, 23x78/QOi, 320x240/PNg, 230x780/JPG\n"
            "thumbnails_format = pnG";
}

static std::string valid_thumbnails3()
{
    return  "thumbnails = 160x120/JPG, 23x78/QOI, 230x780/JPG";
}

static std::string old_valid_thumbnails()
{
    return  "thumbnails = 160x120\n"
            "thumbnails_format = JPG";
}

static std::string old_valid_thumbnails2()
{
    return  "thumbnails = 160x120, 23x78, 320x240\n"
            "thumbnails_format = PNG";
}

static std::string old_invalid_thumbnails()
{
    return  "thumbnails = 160x\n"
            "thumbnails_format = JPG";
}

static std::string old_invalid_thumbnails2()
{
    return  "thumbnails = 160x120, 23*78, 320x240\n"
            "thumbnails_format = PNG";
}

static std::string out_of_range_thumbnails()
{
    return  "thumbnails = 1160x1200/PNG, 23x78/QOI, 320x240/PNG, 230x780/JPG\n"
            "thumbnails_format = PNG";
}

static std::string out_of_range_thumbnails2()
{
    return "thumbnails = 1160x120/PNG, 23x78/QOI, -320x240/PNG, 230x780/JPG\n"
            "thumbnails_format = PNG";
}

static std::string invalid_ext_thumbnails()
{
    return "thumbnails = 1160x120/PNk, 23x78/QOI, 320x240/PNG, 230x780/JPG\n"
            "thumbnails_format = QOI";
}

static std::string invalid_ext_thumbnails2()
{
    return "thumbnails = 1160x120/PNG, 23x78/QO, 320x240/PNG, 230x780/JPG\n"
            "thumbnails_format = PNG";
}

static std::string invalid_val_thumbnails()
{
    return "thumbnails = 1160x/PNg, 23x78/QOI, 320x240/PNG, 230x780/JPG\n"
            "thumbnails_format = JPG";
}

static std::string invalid_val_thumbnails2()
{
    return "thumbnails = x120/PNg, 23x78/QOI, 320x240/PNG, 230x780/JPG\n"
            "thumbnails_format = PNG";
}

static std::string invalid_val_thumbnails3()
{
    return "thumbnails = 1x/PNg, 23x78/QOI, 320x240/PNG, 230x780/JPG\n"
            "thumbnails_format = qoi";
}

static std::string invalid_val_thumbnails4()
{
    return "thumbnails = 123*78/QOI, 320x240/PNG, 230x780/JPG\n"
            "thumbnails_format = jpG";
}

static DynamicPrintConfig thumbnails_config()
{
    DynamicPrintConfig config;
    config.apply_only(FullPrintConfig::defaults() , { "thumbnails", "thumbnails_format" });
     
    return config;
}

TEST_CASE("Validate Empty Thumbnails", "[Thumbnails in Config]") {
    DynamicPrintConfig config = thumbnails_config();

    auto test_loaded_config = [](DynamicPrintConfig& config) {
        REQUIRE(config.opt<ConfigOptionString>("thumbnails")->empty());
        REQUIRE(config.option("thumbnails_format")->getInt() == (int)GCodeThumbnailsFormat::PNG);
    };

    SECTION("Load empty init_data") {
        REQUIRE_NOTHROW(config.load_from_ini_string("", Enable));
        test_loaded_config(config);
    }

    SECTION("Load empty format and empty thumbnails") {
        REQUIRE_THROWS_AS(config.load_from_ini_string(empty_thumbnails(), Enable), BadOptionValueException);
        test_loaded_config(config);
    }
}

TEST_CASE("Validate New Thumbnails", "[Thumbnails in Config]") {

    DynamicPrintConfig config = thumbnails_config();

    auto test_loaded_config = [](DynamicPrintConfig& config, GCodeThumbnailsFormat format) {
        REQUIRE(!config.opt<ConfigOptionString>("thumbnails")->empty());
        REQUIRE(config.option("thumbnails_format")->getInt() == (int)format);
    };

    SECTION("Test 1 (valid)") {
        REQUIRE_NOTHROW(config.load_from_ini_string(valid_thumbnails(), Enable));
        test_loaded_config(config, GCodeThumbnailsFormat::JPG);
    }

    SECTION("Test 2 (valid)") {
        REQUIRE_NOTHROW(config.load_from_ini_string(valid_thumbnails2(), Enable));
        test_loaded_config(config, GCodeThumbnailsFormat::PNG);
    }

    SECTION("Test 3 (valid)") {
        REQUIRE_NOTHROW(config.load_from_ini_string(valid_thumbnails3(), Enable));
        test_loaded_config(config, GCodeThumbnailsFormat::PNG);
    }


    SECTION("Test 1 (out_of_range)") {
        REQUIRE_THROWS_AS(config.load_from_ini_string(out_of_range_thumbnails(), Enable), BadOptionValueException);
        test_loaded_config(config, GCodeThumbnailsFormat::PNG);
    }

    SECTION("Test 2 (out_of_range)") {
        REQUIRE_THROWS_AS(config.load_from_ini_string(out_of_range_thumbnails2(), Enable), BadOptionValueException);
        test_loaded_config(config, GCodeThumbnailsFormat::PNG);
    }


    SECTION("Test 1 (invalid_ext)") {
        REQUIRE_THROWS_AS(config.load_from_ini_string(invalid_ext_thumbnails(), Enable), BadOptionValueException);
        test_loaded_config(config, GCodeThumbnailsFormat::QOI);
    }

    SECTION("Test 2 (invalid_ext)") {
        REQUIRE_THROWS_AS(config.load_from_ini_string(invalid_ext_thumbnails2(), Enable), BadOptionValueException);
        test_loaded_config(config, GCodeThumbnailsFormat::PNG);
    }


    SECTION("Test 1 (invalid_val)") {
        REQUIRE_THROWS_AS(config.load_from_ini_string(invalid_val_thumbnails(), Enable), BadOptionValueException);
        test_loaded_config(config, GCodeThumbnailsFormat::JPG);
    }

    SECTION("Test 2 (invalid_val)") {
        REQUIRE_THROWS_AS(config.load_from_ini_string(invalid_val_thumbnails2(), Enable), BadOptionValueException);
        test_loaded_config(config, GCodeThumbnailsFormat::PNG);
    }

    SECTION("Test 3 (invalid_val)") {
        REQUIRE_THROWS_AS(config.load_from_ini_string(invalid_val_thumbnails3(), Enable), BadOptionValueException);
        test_loaded_config(config, GCodeThumbnailsFormat::PNG);
    }

    SECTION("Test 4 (invalid_val)") {
        REQUIRE_THROWS_AS(config.load_from_ini_string(invalid_val_thumbnails4(), Enable), BadOptionValueException);
        test_loaded_config(config, GCodeThumbnailsFormat::PNG);
    }
}

TEST_CASE("Validate Old Thumbnails", "[Thumbnails in Config]") {

    DynamicPrintConfig config = thumbnails_config();

    auto test_loaded_config = [](DynamicPrintConfig& config, GCodeThumbnailsFormat format) {
        REQUIRE(!config.opt<ConfigOptionString>("thumbnails")->empty());
        REQUIRE(config.option("thumbnails_format")->getInt() == (int)format);
    };

    SECTION("Test 1 (valid)") {
        REQUIRE_NOTHROW(config.load_from_ini_string(old_valid_thumbnails(), Enable));
        test_loaded_config(config, GCodeThumbnailsFormat::JPG);
    }

    SECTION("Test 2 (valid)") {
        REQUIRE_NOTHROW(config.load_from_ini_string(old_valid_thumbnails2(), Enable));
        test_loaded_config(config, GCodeThumbnailsFormat::PNG);
    }

    SECTION("Test 1 (invalid)") {
        REQUIRE_THROWS_AS(config.load_from_ini_string(old_invalid_thumbnails(), Enable), BadOptionValueException);
        test_loaded_config(config, GCodeThumbnailsFormat::JPG);
    }

    SECTION("Test 2 (invalid)") {
        REQUIRE_THROWS_AS(config.load_from_ini_string(old_invalid_thumbnails2(), Enable), BadOptionValueException);
        test_loaded_config(config, GCodeThumbnailsFormat::PNG);
    }
}



        

