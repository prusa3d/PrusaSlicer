
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/Domain/ConfigValue.hpp"

using namespace Catch;

using Slic3r::Domain::ConfigValue;
using Slic3r::Domain::EnumWrapper;
using Slic3r::Domain::EnumVectorWrapper;
using Slic3r::Domain::EnumValueDefs;


enum class TestEnum {
    One,
    Two
};

const EnumValueDefs enum_def{{
    {int(TestEnum::One), "one", "One"},
    {int(TestEnum::Two), "two", "Two"},
}};

TEST_CASE("ConfigValue holds correct value when costructed with basic type", "[ConfigValue]") {
    const ConfigValue value{10};
    CHECK(value.get<int>() == 10);
}

TEST_CASE("ConfigValue can be compared", "[ConfigValue]") {
    const ConfigValue value{std::optional{2}};
    CHECK(value == ConfigValue{std::optional{2}});
    CHECK(value != ConfigValue{std::optional<int>{std::nullopt}});
}

TEST_CASE("ConfigValue can be a vector", "[ConfigValue]") {
    const ConfigValue value{std::vector<double>{10.0, 20.0}};
    CHECK(value.get<std::vector<double>>() == std::vector<double>{10.0, 20.0});
}

TEST_CASE("ConfigValue can be an enum", "[ConfigValue]") {
    const ConfigValue value{EnumWrapper{TestEnum::Two, &enum_def}};
    CHECK(value.get<TestEnum>() == TestEnum::Two);
}

TEST_CASE("ConfigValue can be an enum vector", "[ConfigValue]") {
    const ConfigValue value{EnumVectorWrapper{
        std::vector{TestEnum::One, TestEnum::Two}, &enum_def
    }};
    CHECK(value.get<std::vector<TestEnum>>() == std::vector{TestEnum::One, TestEnum::Two});
}

TEST_CASE("ConfigValue can be set", "[ConfigValue]") {
    ConfigValue value{1};
    value.set(3);
    CHECK(value.get<int>() == 3);
}

TEST_CASE("ConfigValue enum can be set", "[ConfigValue]") {
    ConfigValue value{EnumWrapper{
        TestEnum::Two, &enum_def
    }};
    value.set(TestEnum::One);
    CHECK(value.get<TestEnum>() == TestEnum::One);
}

TEST_CASE("Enum vector values can be updated", "[ConfigValue]") {
    ConfigValue value{EnumVectorWrapper{
        std::vector{TestEnum::One, TestEnum::Two}, &enum_def
    }};

    value.set(std::vector{TestEnum::Two});
    CHECK(value.get<std::vector<TestEnum>>() == std::vector{TestEnum::Two});
}

TEST_CASE("ConfigValue can be re-assigned", "[ConfigValue]") {
    ConfigValue value{10};
    value = ConfigValue{20};
    CHECK(value.get<int>() == 20);
}

TEST_CASE("ConfigValue can be visited", "[ConfigValue]") {
    const auto visitor{[](auto&& value) {
        if constexpr (std::is_same_v<EnumWrapper, std::remove_cvref_t<decltype(value)>>) {
            return true;
        }
        return false;
    }};

    CHECK(ConfigValue{EnumWrapper{TestEnum::One, &enum_def}}.visit(visitor));
    CHECK(!ConfigValue{10}.visit(visitor));
}
