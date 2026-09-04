#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>

#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/TestUtils.hpp"

using namespace Catch;
using namespace Catch::Matchers;

using Slic3r::Domain::ConfigDefinitions;
using Slic3r::Domain::ConfigItemDef;
using Slic3r::Domain::ConfigValue;
using Slic3r::Domain::ConfigBox;
using Slic3r::Domain::FullConfig;
using Slic3r::Domain::PartialConfig;
using Slic3r::Domain::BoxRef;
using Slic3r::Domain::BoxRefs;
using Slic3r::Domain::BoxOrBoxesVector;
using Slic3r::Domain::EnumValueDefs;
using Slic3r::Domain::EnumWrapper;

using Slic3r::Domain::FDMConfigLocation::Print;
using Slic3r::Domain::FDMConfigLocation::Filament;
using Slic3r::Domain::FDMConfigLocation::Object;
using Locations = std::set<Slic3r::Domain::ConfigLocation>;

enum class TestEnum {
    One,
    Two,
    Three
};

const EnumValueDefs test_enum_def{
    { int(TestEnum::One), "one", "One" },
    { int(TestEnum::Two), "two", "Two" },
    { int(TestEnum::Three), "three", "Three" }
};

void init(ConfigDefinitions& defs) {
    auto def{defs.add("print_config_item_with_object_override", typeid(int))};
    def->location = Print;
    def->category = ConfigItemDef::Category::Hidden;
    def->overrides_in = Locations{Object};
    def->init_fn = []() { return ConfigValue{0}; };

    def = defs.add("object_config_item", typeid(int));
    def->category = ConfigItemDef::Category::Hidden;
    def->location = Object;
    def->init_fn = []() { return ConfigValue{111}; };

    def = defs.add("print_config_item", typeid(int));
    def->category = ConfigItemDef::Category::Hidden;
    def->location = Print;
    def->init_fn = []() { return ConfigValue{1}; };

    def = defs.add("print_config_item_with_filament_override", typeid(int));
    def->category = ConfigItemDef::Category::Hidden;
    def->location = Print;
    def->overrides_in = {Filament};
    def->init_fn = []() { return ConfigValue{2}; };

    def = defs.add("filament_config_item", typeid(int));
    def->category = ConfigItemDef::Category::Hidden;
    def->location = Filament;
    def->init_fn = []() { return ConfigValue{3}; };

    def = defs.add("print_enum_config_items_with_filament_override", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = {Filament};
    def->init_fn = []() { return ConfigValue{EnumWrapper{TestEnum::Two, &test_enum_def}}; };
    def->category = ConfigItemDef::Category::Hidden;
}

const ConfigDefinitions& defs() {
    static ConfigDefinitions defs_var{{Print, Filament, Object}, init};
    return defs_var;
}

struct TestPrintSettings : ConfigBox {
    TestPrintSettings(): ConfigBox(defs(), Print) {}
};

struct TestFilamentSettings : ConfigBox {
    TestFilamentSettings(): ConfigBox(defs(), Filament) {}
};

struct TestObjectSettings : ConfigBox {
    TestObjectSettings(): ConfigBox(defs(), Object) {}
};

TEST_CASE("Config opt has default value and can be set", "[Config]") {
    TestPrintSettings print_settings;
    CHECK(print_settings.items.opt("print_config_item").get<int>() == 1);
    print_settings.items.opt("print_config_item").set(200);
    CHECK(print_settings.items.opt("print_config_item").get<int>() == 200);
}

TEST_CASE("Config override can be set, obtained, disabled and enabled", "[Config]") {
    TestFilamentSettings filament_settings;
    REQUIRE(!filament_settings.overrides.get("print_config_item_with_filament_override"));
    filament_settings.overrides.set("print_config_item_with_filament_override", 100);
    REQUIRE(filament_settings.overrides.get("print_config_item_with_filament_override"));
    CHECK(filament_settings.overrides.get("print_config_item_with_filament_override")->get<int>() == 100);
    filament_settings.overrides.disable("print_config_item_with_filament_override");
    CHECK(!filament_settings.overrides.get("print_config_item_with_filament_override"));
    filament_settings.overrides.enable("print_config_item_with_filament_override");
    REQUIRE(filament_settings.overrides.get("print_config_item_with_filament_override"));
    CHECK(filament_settings.overrides.get("print_config_item_with_filament_override")->get<int>() == 100);
}

TEST_CASE("All overriden items can be obtained", "[Config]") {
    TestFilamentSettings box;
    box.overrides.set("print_config_item_with_filament_override", 100);
    REQUIRE(box.overrides.overridden_items().size() == 1);
    CHECK(box.overrides.overridden_items().at(0).get().get<int>() == 100);
}

TEST_CASE("Diff keys returns different overriden keys") {
    TestFilamentSettings box_a;
    box_a.overrides.set("print_config_item_with_filament_override", 100);
    TestFilamentSettings box_b;
    auto result{box_a.diff_keys(box_b)};
    REQUIRE(result.size() == 1);
    CHECK(result.front() == "print_config_item_with_filament_override");
    box_b.overrides.set("print_config_item_with_filament_override", 100);
    result = box_a.diff_keys(box_b);
    REQUIRE(result.empty());
    box_b.overrides.set("print_config_item_with_filament_override", 90);
    result = box_a.diff_keys(box_b);
    REQUIRE(result.size() == 1);
    CHECK(result.front() == "print_config_item_with_filament_override");
}

namespace {
template<typename T>
BoxRefs convert_to_box_refs(
    const std::vector<T>& settings
)
{
    BoxRefs result;
    result.insert(result.end(), settings.cbegin(), settings.cend());
    return result;
}

BoxOrBoxesVector get_input(
    const TestPrintSettings& test_box, const std::vector<TestFilamentSettings>& boxes_with_overrides
)
{
    BoxOrBoxesVector result;

    result.push_back(test_box);
    result.push_back(convert_to_box_refs(boxes_with_overrides));
    return result;
}
}

struct TestFullConfig : public FullConfig
{
    TestFullConfig(
        const TestPrintSettings& test_box,
        const std::vector<TestFilamentSettings>& boxes_with_overrides) :
        FullConfig{
            get_input(test_box, boxes_with_overrides),
            {},
            Slic3r::Test::build_fff_printer_config(3, {})}
    {}
};

TEST_CASE("Full config can be created from boxes", "[Config]")
{
    TestPrintSettings print_settings;
    print_settings.items.opt("print_config_item_with_filament_override").set(22);
    std::vector<TestFilamentSettings> filament_settings(3);
    filament_settings.at(0).overrides.set("print_config_item_with_filament_override", -10);
    filament_settings.at(2).overrides.set("print_config_item_with_filament_override", -20);

    const TestFullConfig full_config{print_settings, filament_settings};
    const auto result{full_config.get<std::vector<int>>("print_config_item_with_filament_override")};
    const std::vector<int> expected{-10, 22, -20};
    CHECK(result == expected);
    CHECK_THAT(
        full_config.keys(),
        UnorderedEquals(std::vector<std::string>{
            "nozzle_diameter",
            "nozzle_high_flow",
            "print_config_item_with_object_override",
            "print_config_item",
            "print_config_item_with_filament_override",
            "filament_config_item",
            "print_enum_config_items_with_filament_override",
        })
    );
}

TEST_CASE("Full config enum override works as expected", "[Config]")
{
    TestPrintSettings print_settings;
    print_settings.items.opt("print_enum_config_items_with_filament_override");
    std::vector<TestFilamentSettings> filament_settings(3);

    filament_settings.at(0).overrides.set("print_enum_config_items_with_filament_override", TestEnum::One);
    filament_settings.at(2).overrides.set("print_enum_config_items_with_filament_override", TestEnum::Three);

    const TestFullConfig full_config{print_settings, filament_settings};
    const auto result{full_config.get<std::vector<TestEnum>>("print_enum_config_items_with_filament_override")};
    const std::vector<TestEnum> expected{TestEnum::One, TestEnum::Two, TestEnum::Three};
    CHECK(result == expected);
}

struct TestPartialConfig : public PartialConfig
{
    TestPartialConfig(
        const TestObjectSettings& boxes_with_overrides
    )
        : PartialConfig{{std::ref(boxes_with_overrides)}, {}}
    {}
};

TEST_CASE("Parial config works as expected", "[Config]")
{
    TestPrintSettings print_settings;
    std::vector<TestFilamentSettings> filament_settings(3);
    const TestFullConfig full_config{print_settings, filament_settings};

    TestObjectSettings object_settings;
    object_settings.overrides.set("print_config_item_with_object_override", 12);
    const TestPartialConfig partial_config{object_settings};

    CHECK(partial_config.get<int>("print_config_item_with_object_override") == std::optional{12});
    CHECK(partial_config.get<int>("object_config_item") == std::optional{111});
    CHECK(!partial_config.get<int>("print_config_item"));
    CHECK(!partial_config.get<int>("print_config_item"));
    CHECK(!partial_config.get<int>("print_config_item_with_filament_override"));
    CHECK(!partial_config.get<int>("filament_config_item"));
    CHECK(!partial_config.get<TestEnum>("print_enum_config_items_with_filament_override"));
}
