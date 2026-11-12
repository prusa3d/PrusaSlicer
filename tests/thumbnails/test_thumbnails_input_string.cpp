#include <catch2/catch_test_macros.hpp>
#include <test_utils.hpp>

#include "Slic3r/Domain/enum_bitmask.hpp"

#include <libslic3r/GCode/Thumbnails.hpp>

using namespace Slic3r;
using namespace GCodeThumbnails;


// Test Thumbnails lines

static std::string empty_thumbnails()
{
    return "";
}

static std::string valid_thumbnails()
{
    return "160x120/PNG, 23x78/QOI, 230x780/JPG";
}

static std::string valid_thumbnails2()
{
    return "160x120/PNG, 23x78/QOi, 320x240/PNg, 230x780/JPG";
}

static std::string out_of_range_thumbnail()
{
    return "160x1200/PNG, 23x78/QOI, 320x240/PNG, 230x780/JPG";
}

static std::string out_of_range_thumbnail2()
{
    return "160x120/PNG, 23x78/QOI, -320x240/PNG, 230x780/JPG";
}

static std::string invalid_ext_thumbnail()
{
    return "160x120/PNk, 23x78/QOI, 320x240/PNG, 230x780/JPG";
}

static std::string invalid_ext_thumbnail2()
{
    return "160x120/PNG, 23x78/QO, 320x240/PNG, 230x780/JPG";
}

static std::string invalid_val_thumbnail()
{
    return "160x/PNg, 23x78/QOI, 320x240/PNG, 230x780/JPG";
}

static std::string invalid_val_thumbnail2()
{
    return "x120/PNg, 23x78/QOI, 320x240/PNG, 230x780/JPG";
}

static std::string invalid_val_thumbnail3()
{
    return "x/PNg, 23x78/QOI, 320x240/PNG, 230x780/JPG";
}

static std::string invalid_val_thumbnail4()
{
    return "23*78/QOI, 320x240/PNG, 230x780/JPG";
}


TEST_CASE("Empty Thumbnails", "[Thumbnails]") {
    auto parsing_result = parse_request(empty_thumbnails());
    REQUIRE(parsing_result.has_value());
    REQUIRE(parsing_result->formats.empty());
    REQUIRE(parsing_result->sizes.empty());
}

TEST_CASE("Valid Thumbnails", "[Thumbnails]") {

    SECTION("Test 1") {
        auto parsing_result = parse_request(valid_thumbnails());
        REQUIRE(parsing_result.has_value());
        REQUIRE(parsing_result->formats.size() == 3);
        REQUIRE(parsing_result->sizes.size() == 3);
    }

    SECTION("Test 2") {
        auto parsing_result = parse_request(valid_thumbnails2());
        REQUIRE(parsing_result.has_value());
        REQUIRE(parsing_result->formats.size() == 4);
        REQUIRE(parsing_result->sizes.size() == 4);
    }
}

TEST_CASE("Out of range Thumbnails", "[Thumbnails]") {

    SECTION("Test 1") {
        auto parsing_result = parse_request(out_of_range_thumbnail());
        REQUIRE(!parsing_result.has_value());
        REQUIRE(parsing_result.error().has(ThumbnailError::OutOfRange));
    }

    SECTION("Test 2") {
        auto parsing_result = parse_request(out_of_range_thumbnail2());
        REQUIRE(!parsing_result.has_value());
        REQUIRE(parsing_result.error().has(ThumbnailError::OutOfRange));
    }
}

TEST_CASE("Invalid extention Thumbnails", "[Thumbnails]") {

    SECTION("Test 1") {
        auto parsing_result = parse_request(invalid_ext_thumbnail());
        REQUIRE(!parsing_result.has_value());
        REQUIRE(parsing_result.error().has(ThumbnailError::InvalidExt));
    }

    SECTION("Test 2") {
        auto parsing_result = parse_request(invalid_ext_thumbnail2());
        REQUIRE(!parsing_result.has_value());
        REQUIRE(parsing_result.error().has(ThumbnailError::InvalidExt));
    }
}

TEST_CASE("Invalid value Thumbnails", "[Thumbnails]") {

    SECTION("Test 1") {
        auto parsing_result = parse_request(invalid_val_thumbnail());
        REQUIRE(!parsing_result.has_value());
        REQUIRE(parsing_result.error().has(ThumbnailError::InvalidVal));
    }

    SECTION("Test 2") {
        auto parsing_result = parse_request(invalid_val_thumbnail2());
        REQUIRE(!parsing_result.has_value());
        REQUIRE(parsing_result.error().has(ThumbnailError::InvalidVal));
    }

    SECTION("Test 3") {
        auto parsing_result = parse_request(invalid_val_thumbnail3());
        REQUIRE(!parsing_result.has_value());
        REQUIRE(parsing_result.error().has(ThumbnailError::InvalidVal));
    }

    SECTION("Test 4") {
        auto parsing_result = parse_request(invalid_val_thumbnail4());
        REQUIRE(!parsing_result.has_value());
        REQUIRE(parsing_result.error().has(ThumbnailError::InvalidVal));
    }
}
