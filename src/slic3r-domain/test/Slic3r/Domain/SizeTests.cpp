#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Slic3r/Domain/Size.hpp"

using namespace Slic3r::Domain;

TEST_CASE("Size comparison")
{
    Size a(10, 50);
    Size b;
    b.width  = 10;
    b.height = 50;

    REQUIRE(a == b);
    REQUIRE(a.width == b.width);
    REQUIRE(a.height == b.height);
}

TEST_CASE("Size scale IgnoreAspectRatio")
{
    Size a(10, 50);

    a.scale(Size(100, 100), Size::ScaleMode::IgnoreAspectRatio);

    REQUIRE(a.width == 100);
    REQUIRE(a.height == 100);
}

TEST_CASE("Size scale KeepAspectRatio")
{
    Size a(10, 50);

    a.scale(Size(100, 100), Size::ScaleMode::KeepAspectRatio);

    REQUIRE(a.width == 20);
    REQUIRE(a.height == 100);
}

TEST_CASE("Size scaled IgnoreAspectRatio")
{
    Size a(10, 50);

    Size b = a.scaled(Size(100, 100), Size::ScaleMode::IgnoreAspectRatio);

    REQUIRE(a.width == 10);
    REQUIRE(a.height == 50);
    REQUIRE(b.width == 100);
    REQUIRE(b.height == 100);
}

TEST_CASE("Size scaled KeepAspectRatio")
{
    Size a(10, 50);

    Size b = a.scaled(Size(100, 100), Size::ScaleMode::KeepAspectRatio);

    REQUIRE(a.width == 10);
    REQUIRE(a.height == 50);
    REQUIRE(b.width == 20);
    REQUIRE(b.height == 100);
}

TEST_CASE("Size space")
{
    Size a(10, 50);

    REQUIRE(a.space() == 500);
}
