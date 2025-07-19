#include <catch2/catch_test_macros.hpp>
#include <test_utils.hpp>

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
    auto [thumbnails, errors] = make_and_check_thumbnail_list(empty_thumbnails());
    REQUIRE(errors == enum_bitmask<ThumbnailError>());
    REQUIRE(thumbnails.empty());
}

TEST_CASE("Valid Thumbnails", "[Thumbnails]") {

    SECTION("Test 1") {
        auto [thumbnails, errors] = make_and_check_thumbnail_list(valid_thumbnails());
        REQUIRE(errors == enum_bitmask<ThumbnailError>());
        REQUIRE(thumbnails.size() == 3);
    }

    SECTION("Test 2") {
        auto [thumbnails, errors] = make_and_check_thumbnail_list(valid_thumbnails2());
        REQUIRE(errors == enum_bitmask<ThumbnailError>());
        REQUIRE(thumbnails.size() == 4);
    }
}

TEST_CASE("Out of range Thumbnails", "[Thumbnails]") {

    SECTION("Test 1") {
        auto [thumbnails, errors] = make_and_check_thumbnail_list(out_of_range_thumbnail());
        REQUIRE(errors != enum_bitmask<ThumbnailError>());
        REQUIRE(errors.has(ThumbnailError::OutOfRange));
        REQUIRE(thumbnails.size() == 3);
    }

    SECTION("Test 2") {
        auto [thumbnails, errors] = make_and_check_thumbnail_list(out_of_range_thumbnail2());
        REQUIRE(errors != enum_bitmask<ThumbnailError>());
        REQUIRE(errors.has(ThumbnailError::OutOfRange));
        REQUIRE(thumbnails.size() == 3);
    }
}

TEST_CASE("Invalid extention Thumbnails", "[Thumbnails]") {

    SECTION("Test 1") {
        auto [thumbnails, errors] = make_and_check_thumbnail_list(invalid_ext_thumbnail());
        REQUIRE(errors != enum_bitmask<ThumbnailError>());
        REQUIRE(errors.has(ThumbnailError::InvalidExt));
        REQUIRE(thumbnails.size() == 4);
    }

    SECTION("Test 2") {
        auto [thumbnails, errors] = make_and_check_thumbnail_list(invalid_ext_thumbnail2());
        REQUIRE(errors != enum_bitmask<ThumbnailError>());
        REQUIRE(errors.has(ThumbnailError::InvalidExt));
        REQUIRE(thumbnails.size() == 4);
    }
}

TEST_CASE("Invalid value Thumbnails", "[Thumbnails]") {

    SECTION("Test 1") {
        auto [thumbnails, errors] = make_and_check_thumbnail_list(invalid_val_thumbnail());
        REQUIRE(errors != enum_bitmask<ThumbnailError>());
        REQUIRE(errors.has(ThumbnailError::InvalidVal));
        REQUIRE(thumbnails.size() == 3);
    }

    SECTION("Test 2") {
        auto [thumbnails, errors] = make_and_check_thumbnail_list(invalid_val_thumbnail2());
        REQUIRE(errors != enum_bitmask<ThumbnailError>());
        REQUIRE(errors.has(ThumbnailError::InvalidVal));
        REQUIRE(thumbnails.size() == 3);
    }

    SECTION("Test 3") {
        auto [thumbnails, errors] = make_and_check_thumbnail_list(invalid_val_thumbnail3());
        REQUIRE(errors != enum_bitmask<ThumbnailError>());
        REQUIRE(errors.has(ThumbnailError::InvalidVal));
        REQUIRE(thumbnails.size() == 3);
    }

    SECTION("Test 4") {
        auto [thumbnails, errors] = make_and_check_thumbnail_list(invalid_val_thumbnail4());
        REQUIRE(errors != enum_bitmask<ThumbnailError>());
        REQUIRE(errors.has(ThumbnailError::InvalidVal));
        REQUIRE(thumbnails.size() == 2);
    }
}