#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <Slic3r/Biz/IObservableList.hpp>

using namespace Slic3r;
using namespace Slic3r::Biz;

TEST_CASE("WeakerPointer empty")
{
    WeakerPointer<int> weaker_foo;

    REQUIRE(weaker_foo.get() == nullptr);
    REQUIRE(!weaker_foo.is_valid());
}

TEST_CASE("WeakerPointer raw")
{
    int* foo = new int(5);
    WeakerPointer<int> weaker_foo(foo);

    REQUIRE(*weaker_foo.get() == 5);
    REQUIRE(weaker_foo.is_valid());
    delete foo;
    REQUIRE(weaker_foo.is_valid());
}

TEST_CASE("WeakerPointer weak_ptr")
{
    std::shared_ptr<int> foo = std::make_shared<int>(5);
    WeakerPointer<int> weaker_foo(foo);

    REQUIRE(*weaker_foo.get() == 5);
    REQUIRE(weaker_foo.is_valid());
    foo.reset();
    REQUIRE(!weaker_foo.is_valid());
}
