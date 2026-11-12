#include <iostream>
#include <boost/nowide/filesystem.hpp>
#include <boost/filesystem.hpp>
#include <catch2/catch_test_macros.hpp>
#include "Slic3r/TestUtils/TestData.hpp"
#include "Slic3r/Biz/Yaml/Yaml.hpp"
#include "Slic3r/Biz/Yaml/YamlSlic3rTypes.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_templated.hpp>
#include <tl/expected.hpp>

namespace Tests {
struct Ver
{
    int major{0};
    int minor{0};
    std::optional<int> patch;

    friend bool operator==(const Ver& lhs, const Ver& rhs)
    {
        return lhs.major == rhs.major && lhs.minor == rhs.minor && lhs.patch == rhs.patch;
    }

    friend bool operator!=(const Ver& lhs, const Ver& rhs)
    {
        return !(lhs == rhs);
    }
};

struct Item
{
    std::string id;

    bool operator==(const Item& rhs) const
    {
        return id == rhs.id;
    }
};

struct MyData
{
    Ver version;
    int a{0};
    std::string b;

    std::vector<Item> items;
    std::optional<int> opt_int;
    std::variant<float, int, std::string> param;

    friend bool operator==(const MyData& lhs, const MyData& rhs)
    {
        return lhs.version == rhs.version
            && lhs.a == rhs.a
            && lhs.b == rhs.b
            && lhs.items == rhs.items
            && lhs.opt_int == rhs.opt_int
            && lhs.param == rhs.param;
    }

    friend bool operator!=(const MyData& lhs, const MyData& rhs)
    {
        return !(lhs == rhs);
    }
};

struct Condition
{
    Slic3r::Domain::Expr::ExprAst condition;
};

struct VecData
{
    std::vector<uint8_t> data;
};

struct ValueData
{
    Slic3r::Domain::Preset::PresetValueMap presets;
    Slic3r::Domain::Preset::FeatureValueMap features;
};

struct SourceLocatedData
{
    Slic3r::Domain::Preset::SourceLocated<std::string> name;
};

} // namespace Tests

STRUCT_DESC_SIMPLE(Tests::Ver, major, minor, patch);
STRUCT_DESC_SIMPLE(Tests::Item, id);
STRUCT_DESC_SIMPLE(Tests::MyData, version, a, b, items, opt_int, param);

STRUCT_DESC_SIMPLE(Tests::Condition, condition);
STRUCT_DESC_SIMPLE(Tests::VecData, data);
STRUCT_DESC_SIMPLE(Tests::ValueData, presets, features);
STRUCT_DESC_SIMPLE(Tests::SourceLocatedData, name);

namespace Tests {

struct Renamed
{
    std::string value;
};

struct WithImplicit
{
    int x{0};
    int y{99};
};

struct ShapeCircle
{
    double radius{0.0};
};

struct ShapeRect
{
    double w{0.0};
    double h{0.0};
};
enum class Color
{
    Red,
    Green,
    Blue
};

struct WithColor
{
    Color color{Color::Red};
};
enum class Direction
{
    North,
    South
};

struct WithDirection
{
    Direction dir{Direction::North};
};

struct MapData
{
    std::map<std::string, int> counts;
};

struct IntsData
{
    Slic3r::Domain::Preset::Ints ints;
};

struct OptIntsData
{
    Slic3r::Domain::Preset::OptInts opt_ints;
};

struct JsonData
{
    Slic3r::Domain::JsonValue data;
};

struct BoolData
{
    bool flag{false};
};

} // namespace Tests

STRUCT_DESC(Tests::Renamed, (value, "my_value", , ))
STRUCT_DESC(Tests::WithImplicit, FIELD_DESC_SIMPLE(x), FIELD_DESC_IMPLICIT_VALUE(y, 99))
STRUCT_DESC_SIMPLE(Tests::ShapeCircle, radius)
STRUCT_DESC_SIMPLE(Tests::ShapeRect, w, h)
STRUCT_DESC_SIMPLE(Tests::WithColor, color)
ENUM_DESC(Tests::Direction, ("north", North), ("south", South))
STRUCT_DESC_SIMPLE(Tests::WithDirection, dir)
STRUCT_DESC_SIMPLE(Tests::MapData, counts)
STRUCT_DESC_SIMPLE(Tests::IntsData, ints)
STRUCT_DESC_SIMPLE(Tests::OptIntsData, opt_ints)
STRUCT_DESC_SIMPLE(Tests::JsonData, data)
STRUCT_DESC_SIMPLE(Tests::BoolData, flag)

namespace Yaml = Slic3r::Biz::Yaml;

TEST_CASE("Load Yaml from file into MyData", "[yaml]")
{
    const std::string filename = Tests::get_datadir().string() + "/presets/test.yaml";
    Tests::MyData data;
    try {
        Yaml::YamlAdapter::Document doc = Yaml::parse_file(filename.c_str());
        data                            = Yaml::parse_struct_unwrap<Tests::MyData>(doc);
        REQUIRE(data.a == 42);
        REQUIRE(data.b == "answer");
        REQUIRE(data.version.major == 1);
        REQUIRE(data.version.minor == 12);
        REQUIRE(*data.version.patch == 321);
        REQUIRE(data.opt_int.has_value() == false);
        REQUIRE(data.items.size() == 3);
        REQUIRE(data.items[0].id == "1");
        REQUIRE(data.items[1].id == "2");
        REQUIRE(data.items[2].id == "3");
        REQUIRE(data.param.index() == 0);
        REQUIRE(std::get<float>(data.param) == Catch::Approx(3.14));
    } catch (const Yaml::ParseError& e) {
        std::cerr << e.what() << std::endl;
    }
}

TEST_CASE("Load Yaml from file into Version", "[yaml]")
{
    boost::nowide::nowide_filesystem();

    size_t files_processed           = 0;
    boost::filesystem::path yaml_dir = Tests::get_datadir() / "yaml";
    for (auto it : boost::filesystem::directory_iterator{yaml_dir}) {
        if (!it.is_regular_file())
            continue;
        const auto filename = it.path().string();
        Tests::Item data;
        try {
            Yaml::YamlAdapter::Document doc = Yaml::parse_file(filename.c_str());
            INFO("File loaded");
            INFO(filename.c_str());
            REQUIRE(doc == true);
            auto result = Yaml::parse_struct<Tests::Item>(doc);
            REQUIRE(result.has_value() == true);
            INFO("has result");
            data = result.value<Tests::Item>();
            REQUIRE(data.id == "x");
            files_processed++;

        } catch (const Yaml::ParseError& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
    }

    REQUIRE(files_processed == 2);
}

TEST_CASE("Load Yaml from string", "[yaml]")
{
    std::string yaml_ver_no_minor = R"(
major: 1
)";
    std::string yaml_ver_ok       = R"(
major: 3
minor: 2
patch: 321
)";

    Yaml::YamlAdapter::Document doc = Yaml::parse_string(yaml_ver_no_minor);

    REQUIRE_THROWS_MATCHES(
        Yaml::parse_struct_unwrap<Tests::Ver>(doc),
        Yaml::ParseError,
        // Catch::Matchers::ContainsSubstring("Required field 'minor' not found")
        Catch::Matchers::MessageMatches(
            Catch::Matchers::ContainsSubstring("Required field 'minor' not found")
        )
    );

    doc = Yaml::parse_string(yaml_ver_ok);
    Tests::Ver ver;
    REQUIRE_NOTHROW(ver = Yaml::parse_struct_unwrap<Tests::Ver>(doc));
    REQUIRE(ver.major == 3);
    REQUIRE(ver.minor == 2);
    REQUIRE(ver.patch == 321);
}

TEST_CASE("ExprAst parsing", "[yaml]")
{
    std::string yaml = R"(
condition: 'tool.nozzle_diameter >= 0.2'
)";

    Yaml::YamlAdapter::Document doc = Yaml::parse_string(yaml);
    Tests::Condition condition      = Yaml::parse_struct_unwrap<Tests::Condition>(doc);
    REQUIRE(
        Slic3r::Domain::Expr::to_string(condition.condition) == "(tool.nozzle_diameter >= 0.2)"
    );
}

TEST_CASE("Vector parsing", "[yaml]")
{
    std::string yaml = R"(
data: [0]
)";

    Yaml::YamlAdapter::Document doc = Yaml::parse_string(yaml);
    Tests::VecData vec              = Yaml::parse_struct_unwrap<Tests::VecData>(doc);
    REQUIRE(vec.data.size() == 1);
}

TEST_CASE("Slic3r Types", "[yaml]")
{
    using Slic3r::Domain::Percentage;
    using Slic3r::Domain::Vec2d;
    using Slic3r::Domain::Vec2ds;
    using Slic3r::Domain::Preset::Bools;
    using Slic3r::Domain::Preset::Doubles;
    using Slic3r::Domain::Preset::FeatureValue;
    using Slic3r::Domain::Preset::PresetValue;
    using Slic3r::Domain::Preset::Strings;
    SECTION("PresetValue and FeatureValue")
    {
        std::string yaml = R"(
features:
  bool: true
  num: 42.1
  str: Hello world
presets:
  vec2: '0x0'
  vec2s: ['0x0']
  bool: true
  bools: [true, false]
  num: 42.1
  nums: [1, 2, 3]
  str: Hello
  strs: ['Hello', 'world']
  mono: null
  percent: '12%'
  percents: ['12%']
)";
        try {
            auto doc              = Yaml::parse_string(yaml);
            Tests::ValueData data = Yaml::parse_struct_unwrap<Tests::ValueData>(doc);
            REQUIRE(data.features.find("bool")->second == FeatureValue{true});
            REQUIRE(data.features.find("num")->second == FeatureValue{42.1});
            REQUIRE(data.features.find("str")->second == FeatureValue{"Hello world"});

            REQUIRE(data.presets.find("vec2")->second == PresetValue{Vec2d{0, 0}});
            REQUIRE(data.presets.find("vec2s")->second == PresetValue{Vec2ds{Vec2d{0, 0}}});
            REQUIRE(data.presets.find("bool")->second == PresetValue{true});
            REQUIRE(data.presets.find("bools")->second == PresetValue{Bools{true, false}});
            REQUIRE(data.presets.find("num")->second == PresetValue{42.1});
            REQUIRE(data.presets.find("nums")->second == PresetValue{Doubles{1, 2, 3}});
            REQUIRE(data.presets.find("str")->second == PresetValue{"Hello"});
            REQUIRE(data.presets.find("strs")->second == PresetValue{Strings{"Hello", "world"}});
            REQUIRE(data.presets.find("mono")->second == PresetValue{std::monostate{}});
            REQUIRE(data.presets.find("percent")->second == PresetValue{Percentage{12}});
            REQUIRE(
                data.presets.find("percents")->second
                == PresetValue{Slic3r::Domain::Preset::FloatOrPercentages{Percentage{12}}}
            );

        } catch (Yaml::ParseError& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        } catch (std::exception& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
    }
    SECTION("SourceLocated")
    {
        std::string yaml = R"(
# some comment
name: 'Abc'
)";
        try {
            auto doc  = Yaml::parse_string(yaml);
            auto data = Yaml::parse_struct_unwrap<Tests::SourceLocatedData>(doc);
            REQUIRE(data.name.value == "Abc");
            REQUIRE(data.name.source_location.column == 7);
            REQUIRE(data.name.source_location.line == 3);
            REQUIRE(data.name.source_location.file == "<string>");
        } catch (Yaml::ParseError& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        } catch (std::exception& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
    }
}

TEST_CASE("Minimal roundtrip", "[yaml]")
{
    using namespace Slic3r::Biz::Yaml;
    using namespace Tests;
    const Item orig_item{"aaa"};

    SECTION("Node-level round trip")
    {
        auto node        = Details::TypeTraits<Item>::serialize(orig_item);
        auto parsed_item = parse_struct<Item>(node.value());

        REQUIRE(parsed_item == orig_item);
    }

    SECTION("Verify output")
    {
        auto node    = Details::TypeTraits<Item>::serialize(orig_item);
        auto emitter = YamlAdapter::create_emitter(node.value());
        auto yaml{YamlAdapter::emitter_output(emitter)};
        auto expected = "id: aaa";
        REQUIRE(yaml.find(expected) != std::string::npos);
    }
}

TEST_CASE("Nested roundtrip", "[yaml]")
{
    using namespace Slic3r::Biz::Yaml;
    using namespace Tests;
    const MyData orig_data{.version = Ver{1, 2}, .items = {Item{"x1"}, Item{"x2"}}};

    auto node        = Details::TypeTraits<MyData>::serialize(orig_data);
    auto parsed_data = parse_struct<MyData>(node.value());

    REQUIRE(parsed_data == orig_data);
}

TEST_CASE("Multi-document YAML", "[yaml]")
{
    const std::string yaml = R"(---
major: 1
minor: 0
---
major: 2
minor: 5
)";

    std::vector<Tests::Ver> versions;
    Yaml::parse_all_documents_in_string(
        yaml,
        [&](const Yaml::YamlAdapter::Document& doc)
        { versions.push_back(Yaml::parse_struct_unwrap<Tests::Ver>(doc)); }
    );

    REQUIRE(versions.size() == 2);
    REQUIRE(versions[0].major == 1);
    REQUIRE(versions[0].minor == 0);
    REQUIRE(versions[1].major == 2);
    REQUIRE(versions[1].minor == 5);
}

TEST_CASE("Discriminated struct parsing", "[yaml]")
{
    SECTION("Dispatch to circle")
    {
        auto doc = Yaml::parse_string(R"(
type: circle
radius: 5.0
)");
        Tests::ShapeCircle circle;
        bool got_circle = false;
        Yaml::parse_structs_by_discriminant(
            doc.root(),
            "type",
            std::tuple<const char*, std::function<void(Tests::ShapeCircle&&)>>{
                "circle",
                [&](Tests::ShapeCircle&& s)
                {
                    circle     = s;
                    got_circle = true;
                }
            },
            std::tuple<const char*, std::function<void(Tests::ShapeRect&&)>>{
                "rect",
                [](Tests::ShapeRect&&) {}
            }
        );
        REQUIRE(got_circle);
        REQUIRE(circle.radius == Catch::Approx(5.0));
    }

    SECTION("Unknown discriminant throws")
    {
        auto doc = Yaml::parse_string(R"(
type: triangle
w: 1.0
)");
        REQUIRE_THROWS_MATCHES(
            Yaml::parse_structs_by_discriminant(
                doc.root(),
                "type",
                std::tuple<const char*, std::function<void(Tests::ShapeCircle&&)>>{
                    "circle",
                    [](Tests::ShapeCircle&&) {}
                },
                std::tuple<const char*, std::function<void(Tests::ShapeRect&&)>>{
                    "rect",
                    [](Tests::ShapeRect&&) {}
                }
            ),
            Yaml::ParseError,
            Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("Allowed values"))
        );
    }
}

TEST_CASE("Enum parsing", "[yaml]")
{
    SECTION("magic_enum auto: case-insensitive parse")
    {
        auto doc    = Yaml::parse_string("color: Green");
        auto result = Yaml::parse_struct_unwrap<Tests::WithColor>(doc);
        REQUIRE(result.color == Tests::Color::Green);

        doc    = Yaml::parse_string("color: green");
        result = Yaml::parse_struct_unwrap<Tests::WithColor>(doc);
        REQUIRE(result.color == Tests::Color::Green);

        doc = Yaml::parse_string("color: Purple");
        REQUIRE_THROWS_MATCHES(
            Yaml::parse_struct_unwrap<Tests::WithColor>(doc),
            Yaml::ParseError,
            Catch::Matchers::MessageMatches(
                Catch::Matchers::ContainsSubstring("Invalid enum value")
            )
        );
    }

    SECTION("ENUM_DESC manual: name mapping and roundtrip")
    {
        auto doc    = Yaml::parse_string("dir: north");
        auto result = Yaml::parse_struct_unwrap<Tests::WithDirection>(doc);
        REQUIRE(result.dir == Tests::Direction::North);

        doc    = Yaml::parse_string("dir: south");
        result = Yaml::parse_struct_unwrap<Tests::WithDirection>(doc);
        REQUIRE(result.dir == Tests::Direction::South);

        auto node = Slic3r::Biz::Yaml::Details::TypeTraits<Tests::WithDirection>::serialize(result);
        auto emitter = Yaml::YamlAdapter::create_emitter(*node);
        auto yaml    = std::string{Yaml::YamlAdapter::emitter_output(emitter)};
        REQUIRE(yaml.find("south") != std::string::npos);

        doc = Yaml::parse_string("dir: east");
        REQUIRE_THROWS_MATCHES(
            Yaml::parse_struct_unwrap<Tests::WithDirection>(doc),
            Yaml::ParseError,
            Catch::Matchers::MessageMatches(
                Catch::Matchers::ContainsSubstring("Invalid enum value")
            )
        );
    }
}

TEST_CASE("Map roundtrip", "[yaml]")
{
    SECTION("Parse map")
    {
        auto doc    = Yaml::parse_string(R"(
counts:
  a: 1
  b: 2
)");
        auto result = Yaml::parse_struct_unwrap<Tests::MapData>(doc);
        REQUIRE(result.counts.size() == 2);
        REQUIRE(result.counts.at("a") == 1);
        REQUIRE(result.counts.at("b") == 2);
    }

    SECTION("Empty map")
    {
        auto doc    = Yaml::parse_string("counts: {}");
        auto result = Yaml::parse_struct_unwrap<Tests::MapData>(doc);
        REQUIRE(result.counts.empty());
    }

    SECTION("Roundtrip")
    {
        const Tests::MapData orig{.counts = {{"x", 10}, {"y", 20}}};
        auto yaml   = Yaml::write_string(orig);
        auto doc    = Yaml::parse_string(yaml);
        auto parsed = Yaml::parse_struct_unwrap<Tests::MapData>(doc);
        REQUIRE(parsed.counts == orig.counts);
    }
}

TEST_CASE("Optional roundtrip", "[yaml]")
{
    SECTION("Absent optional omitted from serialization")
    {
        const Tests::Ver ver{1, 0, std::nullopt};
        auto node    = Slic3r::Biz::Yaml::Details::TypeTraits<Tests::Ver>::serialize(ver);
        auto emitter = Yaml::YamlAdapter::create_emitter(*node);
        auto yaml    = std::string{Yaml::YamlAdapter::emitter_output(emitter)};
        REQUIRE(yaml.find("patch") == std::string::npos);
    }

    SECTION("Present optional included in serialization and roundtrips")
    {
        const Tests::Ver ver{1, 0, 7};
        auto node    = Slic3r::Biz::Yaml::Details::TypeTraits<Tests::Ver>::serialize(ver);
        auto emitter = Yaml::YamlAdapter::create_emitter(*node);
        auto yaml    = std::string{Yaml::YamlAdapter::emitter_output(emitter)};
        REQUIRE(yaml.find("patch") != std::string::npos);

        auto parsed_doc = Yaml::parse_string(yaml);
        auto parsed     = Yaml::parse_struct_unwrap<Tests::Ver>(parsed_doc);
        REQUIRE(parsed == ver);
    }
}

TEST_CASE("Custom field name", "[yaml]")
{
    SECTION("Parse with custom YAML key")
    {
        auto doc    = Yaml::parse_string("my_value: hello");
        auto result = Yaml::parse_struct_unwrap<Tests::Renamed>(doc);
        REQUIRE(result.value == "hello");
    }

    SECTION("Serialize uses custom YAML key")
    {
        const Tests::Renamed orig{"world"};
        auto node    = Slic3r::Biz::Yaml::Details::TypeTraits<Tests::Renamed>::serialize(orig);
        auto emitter = Yaml::YamlAdapter::create_emitter(*node);
        auto yaml    = std::string{Yaml::YamlAdapter::emitter_output(emitter)};
        REQUIRE(yaml.find("my_value") != std::string::npos);
        REQUIRE(yaml.find("world") != std::string::npos);
    }

    SECTION("Roundtrip")
    {
        const Tests::Renamed orig{"round"};
        auto node   = Slic3r::Biz::Yaml::Details::TypeTraits<Tests::Renamed>::serialize(orig);
        auto parsed = Yaml::parse_struct<Tests::Renamed>(*node);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->value == orig.value);
    }
}

TEST_CASE("Implicit field value", "[yaml]")
{
    SECTION("Absent field gets implicit value")
    {
        auto doc    = Yaml::parse_string("x: 5");
        auto result = Yaml::parse_struct_unwrap<Tests::WithImplicit>(doc);
        REQUIRE(result.x == 5);
        REQUIRE(result.y == 99);
    }

    SECTION("Present field overrides implicit value")
    {
        auto doc    = Yaml::parse_string("x: 5\ny: 7");
        auto result = Yaml::parse_struct_unwrap<Tests::WithImplicit>(doc);
        REQUIRE(result.x == 5);
        REQUIRE(result.y == 7);
    }

    SECTION("Implicit value is omitted from serialization")
    {
        const Tests::WithImplicit val{5, 99};
        auto node    = Slic3r::Biz::Yaml::Details::TypeTraits<Tests::WithImplicit>::serialize(val);
        auto emitter = Yaml::YamlAdapter::create_emitter(*node);
        auto yaml    = std::string{Yaml::YamlAdapter::emitter_output(emitter)};
        REQUIRE(yaml.find("x") != std::string::npos);
        REQUIRE(yaml.find("y") == std::string::npos);
    }

    SECTION("Non-implicit value is included in serialization")
    {
        const Tests::WithImplicit val{5, 7};
        auto node    = Slic3r::Biz::Yaml::Details::TypeTraits<Tests::WithImplicit>::serialize(val);
        auto emitter = Yaml::YamlAdapter::create_emitter(*node);
        auto yaml    = std::string{Yaml::YamlAdapter::emitter_output(emitter)};
        REQUIRE(yaml.find("y") != std::string::npos);
    }
}

TEST_CASE("JSON types", "[yaml]")
{
    using Slic3r::Domain::JsonArray;
    using Slic3r::Domain::JsonObject;
    using Slic3r::Domain::JsonValue;

    SECTION("Scalar bool")
    {
        auto doc    = Yaml::parse_string("data: true");
        auto result = Yaml::parse_struct_unwrap<Tests::JsonData>(doc);
        REQUIRE(std::holds_alternative<bool>(result.data));
        REQUIRE(std::get<bool>(result.data) == true);
    }

    SECTION("Scalar double")
    {
        auto doc    = Yaml::parse_string("data: 42.5");
        auto result = Yaml::parse_struct_unwrap<Tests::JsonData>(doc);
        REQUIRE(std::holds_alternative<double>(result.data));
        REQUIRE(std::get<double>(result.data) == Catch::Approx(42.5));
    }

    SECTION("Scalar string")
    {
        auto doc    = Yaml::parse_string("data: hello");
        auto result = Yaml::parse_struct_unwrap<Tests::JsonData>(doc);
        REQUIRE(std::holds_alternative<std::string>(result.data));
        REQUIRE(std::get<std::string>(result.data) == "hello");
    }

    SECTION("Array")
    {
        auto doc    = Yaml::parse_string("data: [1, true, x]");
        auto result = Yaml::parse_struct_unwrap<Tests::JsonData>(doc);
        REQUIRE(std::holds_alternative<JsonArray>(result.data));
        const auto& arr = std::get<JsonArray>(result.data);
        REQUIRE(arr.size() == 3);
        REQUIRE(std::holds_alternative<double>(arr[0]));
        REQUIRE(std::holds_alternative<bool>(arr[1]));
        REQUIRE(std::holds_alternative<std::string>(arr[2]));
    }

    SECTION("Object")
    {
        auto doc    = Yaml::parse_string("data:\n  a: 1\n  b: hello");
        auto result = Yaml::parse_struct_unwrap<Tests::JsonData>(doc);
        REQUIRE(std::holds_alternative<JsonObject>(result.data));
        const auto& obj = std::get<JsonObject>(result.data);
        REQUIRE(obj.size() == 2);
        REQUIRE(std::holds_alternative<double>(obj.at("a")));
        REQUIRE(std::holds_alternative<std::string>(obj.at("b")));
    }

    SECTION("Array roundtrip")
    {
        auto doc     = Yaml::parse_string("data: [1, false]");
        auto parsed  = Yaml::parse_struct_unwrap<Tests::JsonData>(doc);
        auto node    = Slic3r::Biz::Yaml::Details::TypeTraits<Tests::JsonData>::serialize(parsed);
        auto parsed2 = Yaml::parse_struct<Tests::JsonData>(*node);
        REQUIRE(parsed2.has_value());
        REQUIRE(std::holds_alternative<JsonArray>(parsed2->data));
        REQUIRE(std::get<JsonArray>(parsed2->data).size() == 2);
    }
}

TEST_CASE("Slic3r types roundtrip", "[yaml]")
{
    using namespace Slic3r::Biz::Yaml::Details;
    using Slic3r::Domain::FloatOrPercentage;
    using Slic3r::Domain::Percentage;
    using Slic3r::Domain::Vec2d;
    using Slic3r::Domain::Preset::Ints;
    using Slic3r::Domain::Preset::OptInts;

    SECTION("Vec2d roundtrip")
    {
        const Vec2d orig{3.0, 4.0};
        auto node = TypeTraits<Vec2d>::serialize(orig);
        REQUIRE(node.has_value());
        auto result = TypeTraits<Vec2d>::parse(*node);
        REQUIRE(result.has_value());
        REQUIRE(result->x() == Catch::Approx(orig.x()));
        REQUIRE(result->y() == Catch::Approx(orig.y()));
    }

    SECTION("Percentage roundtrip")
    {
        const Percentage orig{50.0};
        auto node = TypeTraits<Percentage>::serialize(orig);
        REQUIRE(node.has_value());
        auto result = TypeTraits<Percentage>::parse(*node);
        REQUIRE(result.has_value());
        REQUIRE(result->value == Catch::Approx(orig.value));
    }

    SECTION("FloatOrPercentage as plain float")
    {
        auto node   = Yaml::YamlAdapter::create_scalar_node("3.5");
        auto result = TypeTraits<FloatOrPercentage>::parse(node);
        REQUIRE(result.has_value());
        REQUIRE(!result->is_percentage());
        REQUIRE(result->float_value() == Catch::Approx(3.5));
    }

    SECTION("monostate roundtrip")
    {
        auto node = TypeTraits<std::monostate>::serialize(std::monostate{});
        REQUIRE(node.has_value());
        auto result = TypeTraits<std::monostate>::parse(*node);
        REQUIRE(result.has_value());
    }

    SECTION("PresetValue Ints")
    {
        auto doc    = Yaml::parse_string("ints: [1, 2, 3]");
        auto result = Yaml::parse_struct_unwrap<Tests::IntsData>(doc);
        REQUIRE(result.ints == Ints{1, 2, 3});
    }

    SECTION("PresetValue OptInts")
    {
        auto doc    = Yaml::parse_string("opt_ints: [1, null, 3]");
        auto result = Yaml::parse_struct_unwrap<Tests::OptIntsData>(doc);
        REQUIRE(result.opt_ints.size() == 3);
        REQUIRE(result.opt_ints[0] == 1);
        REQUIRE(!result.opt_ints[1].has_value());
        REQUIRE(result.opt_ints[2] == 3);
    }
}

TEST_CASE("Parse error messages", "[yaml]")
{
    SECTION("Invalid bool value")
    {
        auto doc = Yaml::parse_string("flag: yes");
        REQUIRE_THROWS_MATCHES(
            Yaml::parse_struct_unwrap<Tests::BoolData>(doc),
            Yaml::ParseError,
            Catch::Matchers::MessageMatches(
                Catch::Matchers::ContainsSubstring("Invalid bool value")
            )
        );
    }

    SECTION("Invalid integer value")
    {
        auto doc = Yaml::parse_string("major: abc\nminor: 0");
        REQUIRE_THROWS_MATCHES(
            Yaml::parse_struct_unwrap<Tests::Ver>(doc),
            Yaml::ParseError,
            Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("Invalid"))
        );
    }

    SECTION("Node type mismatch: sequence where scalar expected")
    {
        auto doc = Yaml::parse_string("major: [1, 2]\nminor: 0");
        REQUIRE_THROWS_MATCHES(
            Yaml::parse_struct_unwrap<Tests::Ver>(doc),
            Yaml::ParseError,
            Catch::Matchers::MessageMatches(
                Catch::Matchers::ContainsSubstring("Node type mismatch")
            )
        );
    }

    SECTION("Invalid Vec2d: no x separator")
    {
        using Slic3r::Biz::Yaml::Details::TypeTraits;
        using Slic3r::Domain::Vec2d;
        auto node   = Yaml::YamlAdapter::create_scalar_node("1.5");
        auto result = TypeTraits<Vec2d>::parse(node);
        REQUIRE(!result.has_value());
        REQUIRE(result.error().message.find("Invalid Vec2d") != std::string::npos);
    }

    SECTION("Invalid Percentage: no percent sign")
    {
        using Slic3r::Biz::Yaml::Details::TypeTraits;
        using Slic3r::Domain::Percentage;
        auto node   = Yaml::YamlAdapter::create_scalar_node("10");
        auto result = TypeTraits<Percentage>::parse(node);
        REQUIRE(!result.has_value());
        REQUIRE(result.error().message.find("Invalid Percentage") != std::string::npos);
    }

    SECTION("PresetValue with mapping node")
    {
        auto doc = Yaml::parse_string(R"(
presets:
  bad: {a: 1}
features: {}
)");
        REQUIRE_THROWS_MATCHES(
            Yaml::parse_struct_unwrap<Tests::ValueData>(doc),
            Yaml::ParseError,
            Catch::Matchers::MessageMatches(
                Catch::Matchers::ContainsSubstring("preset value cannot be a YAML mapping")
            )
        );
    }
}

TEST_CASE("write_string and write_file", "[yaml]")
{
    const Tests::Item orig{"write-test-id"};

    SECTION("write_string produces correct YAML")
    {
        auto yaml = Yaml::write_string(orig);
        REQUIRE(yaml.find("id") != std::string::npos);
        REQUIRE(yaml.find("write-test-id") != std::string::npos);
    }

    SECTION("write_file roundtrip")
    {
        namespace fs = boost::filesystem;
        const std::string tmp_path =
            (fs::temp_directory_path() / "yaml_test_write_item.yaml").string();

        Yaml::write_file(orig, tmp_path.c_str());
        REQUIRE(fs::exists(tmp_path));

        auto doc    = Yaml::parse_file(tmp_path.c_str());
        auto result = Yaml::parse_struct_unwrap<Tests::Item>(doc);
        REQUIRE(result == orig);

        fs::remove(tmp_path);
    }

    SECTION("write_file to invalid path throws SerializationError")
    {
        REQUIRE_THROWS_AS(
            Yaml::write_file(orig, "/nonexistent_dir_xyz/file.yaml"),
            Yaml::SerializationError
        );
    }
}

TEST_CASE("Empty containers", "[yaml]")
{
    SECTION("Empty sequence parses to empty vector")
    {
        auto doc    = Yaml::parse_string("data: []");
        auto result = Yaml::parse_struct_unwrap<Tests::VecData>(doc);
        REQUIRE(result.data.empty());
    }

    SECTION("Empty map roundtrip")
    {
        const Tests::MapData orig{{}};
        auto yaml   = Yaml::write_string(orig);
        auto doc    = Yaml::parse_string(yaml);
        auto parsed = Yaml::parse_struct_unwrap<Tests::MapData>(doc);
        REQUIRE(parsed.counts.empty());
    }
}
