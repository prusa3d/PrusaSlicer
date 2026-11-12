#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include "Slic3r/Biz/Parser/IO.hpp"
#include "Slic3r/Domain/Types.hpp"

using namespace Catch;

using Slic3r::Biz::Parser::IO::Type;
using Slic3r::Biz::Parser::IO::Config;
using Slic3r::Biz::Parser::IO::is_vector;
using Slic3r::Biz::Parser::IO::is_scalar;
using Slic3r::Biz::Parser::IO::Value;
using Slic3r::Biz::Parser::IO::Vector;
using Slic3r::Biz::Parser::IO::Scalar;
using Slic3r::Domain::Percentage;
using Slic3r::Domain::Vec2d;

TEST_CASE("Vector is stored if std::vector<int> is passed to config", "[ParserIO]") {
    Config config;
    const std::vector<int> data{1, 2, 3};
    config.set("key", data);
    const Value& value{*config.option("key")};
    REQUIRE(is_vector(value));
    const Vector& vector{std::get<Vector>(value)};
    CHECK(vector.size() == 3);
    CHECK(vector.empty() == false);
    CHECK(vector.type() == Type::Ints);
    CHECK(vector.get<int>() == data);
}

TEST_CASE("Scalar is stored if Perecentage is passed to config", "[ParserIO]") {
    Config config;
    Percentage data;
    config.set("key", data);
    const Value& value{*config.option("key")};
    REQUIRE(is_scalar(value));
    const Scalar& scalar{std::get<Scalar>(value)};
    CHECK(scalar.type() == Type::Percent);
    CHECK(scalar.get<Percentage>() == data);
}

TEST_CASE("Vectors can be compared", "[ParserIO]") {
    const Vector vector{std::vector<int>{1, 2, 3}};
    const Vector other_vector{std::vector<int>{1, 2}};
    CHECK(vector == vector);
    CHECK(vector != other_vector);
}

TEST_CASE("Scalars can be compared", "[ParserIO]") {
    const Scalar scalar{1};
    const Scalar other_scalar{2};
    CHECK(scalar == scalar);
    CHECK(scalar != other_scalar);
}

enum class TestEnum {
    none,
    value,
    other
};

TEST_CASE("Scalar can be serialized to string", "[ParserIO]") {
    CHECK(Scalar{11.3}.serialize() == "11.3");
    CHECK(Scalar{42}.serialize() == "42");
    CHECK(Scalar{std::optional<int>{22}}.serialize() == "22");
    CHECK(Scalar{std::optional<int>{}}.serialize() == "nil");
    CHECK(Scalar{std::string{"hello\r\n\\"}}.serialize() == "hello\r\n\\");
    CHECK(Scalar{Percentage{10}}.serialize() == "10%");
    CHECK(Scalar{Vec2d{12.3, 4.56}}.serialize() == "12.3,4.56");
    CHECK(Scalar{true}.serialize() == "1");
    CHECK(Scalar{false}.serialize() == "0");
    CHECK(Scalar{TestEnum::value, "value"}.serialize() == "value");
}

TEST_CASE("Vactor at returns a reference a value if the index is in range", "[ParserIO]") {
    const Vector vector{std::vector<int>{1, 2, 3}};
    CHECK(vector.at(1).get<int>() == 2);
}

TEST_CASE("Vactor can be modified in place", "[ParserIO]") {
    Vector vector{std::vector<int>{1, 2, 3}};
    vector.at(1) = Scalar{4};
    CHECK(vector.at(1).get<int>() == 4);
}

TEST_CASE("Vactor at throw if the index is not in range", "[ParserIO]") {
    Vector vector{std::vector<int>{1, 2, 3}};
    CHECK_THROWS_AS(vector.at(10), std::out_of_range);
}

TEST_CASE("Stored enum scalar can be retrieved", "[ParserIO]") {
    const Scalar scalar{TestEnum::value, "value"};
    CHECK(scalar.get<TestEnum>() == TestEnum::value);
}

TEST_CASE("Stored enum vector can be retrieved", "[ParserIO]") {
    const Vector vector{std::vector{
        std::pair{TestEnum::value, "value"},
        std::pair{TestEnum::other, "other"},
    }};
    CHECK(vector.get<TestEnum>() == std::vector{TestEnum::value, TestEnum::other});
}


