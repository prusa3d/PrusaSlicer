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
using Slic3r::Domain::EffectiveValueMap;
using Slic3r::Domain::EffectiveValue;

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

    def = defs.add("print_config_item_with_filament_and_object_override", typeid(int));
    def->category = ConfigItemDef::Category::Hidden;
    def->location = Print;
    def->overrides_in = {Filament, Object};
    def->init_fn = []() { return ConfigValue{4}; };

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
            "print_config_item_with_filament_and_object_override",
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

TEST_CASE("Filament overrides are recorded in effective values", "[Config]") {
    TestPrintSettings print_settings;
    print_settings.items.opt("print_config_item_with_filament_override").set(22);

    std::vector<TestFilamentSettings> filament_settings(3);
    filament_settings.at(0).overrides.set("print_config_item_with_filament_override", -10);
    filament_settings.at(2).overrides.set("print_config_item_with_filament_override", -20);

    const TestFullConfig full_config{print_settings, filament_settings};
    const EffectiveValueMap& effective_values{full_config.effective_values()};

    REQUIRE(effective_values.contains("print_config_item_with_filament_override"));
    const EffectiveValue& effective_value{effective_values.at("print_config_item_with_filament_override")};

    const EffectiveValue expected_value{{
        {
            EffectiveValue::SetBy{Print},
            ConfigValue{22},
        },
        {
            EffectiveValue::OverriddenBy{Filament, {0, 2}},
            ConfigValue{
                std::vector<int>{-10, 22, -20},
            },
        },
    }};

    CHECK(effective_value == expected_value);
}

TEST_CASE("Filament settings are recorded in effective values", "[Config]") {
    TestPrintSettings print_settings;
    std::vector<TestFilamentSettings> filament_settings(3);
    filament_settings.at(0).items.opt("filament_config_item").set(1);
    filament_settings.at(1).items.opt("filament_config_item").set(2);
    filament_settings.at(2).items.opt("filament_config_item").set(3);

    const TestFullConfig full_config{print_settings, filament_settings};
    const EffectiveValueMap& effective_values{full_config.effective_values()};

    REQUIRE(effective_values.contains("filament_config_item"));
    const EffectiveValue& effective_value{effective_values.at("filament_config_item")};
    REQUIRE(effective_value.modifications.size() == 1);

    const EffectiveValue expected_value{{
        {
            EffectiveValue::SetBy{Filament},
            ConfigValue{std::vector<int>{1, 2, 3}},
        },
    }};

    CHECK(effective_value == expected_value);
}


TEST_CASE("Partial config effective values record object overrides", "[Config]")
{
    TestObjectSettings object_settings;
    object_settings.overrides.set("print_config_item_with_object_override", 12);
    const TestPartialConfig partial_config{object_settings};
    const EffectiveValueMap& effective_values{partial_config.effective_values()};

    REQUIRE(effective_values.contains("print_config_item_with_object_override"));
    const EffectiveValue& effective_value{effective_values.at("print_config_item_with_object_override")};
    REQUIRE(effective_value.modifications.size() == 1);

    const EffectiveValue expected_value{{
        {
            EffectiveValue::OverriddenBy{Object},
            ConfigValue{12},
        },
    }};

    CHECK(effective_value == expected_value);
}

BoxRefs filament_refs(const std::vector<TestFilamentSettings>& boxes)
{
    return BoxRefs{boxes.begin(), boxes.end()};
}

TEST_CASE("Partial config from per slot boxes records partially defined slots", "[Config]")
{
    std::vector<TestFilamentSettings> filament_settings(3);
    filament_settings.at(2).overrides.set("print_config_item_with_filament_override", -20);
    const PartialConfig partial_config{
        {filament_refs(filament_settings)}, Slic3r::Test::build_fff_printer_config(3, {})};

    CHECK(
        partial_config.get<std::vector<int>>("print_config_item_with_filament_override")
        == std::optional{std::vector<int>{-20, -20, -20}});
    CHECK(partial_config.get<std::vector<int>>("filament_config_item")
          == std::optional{std::vector<int>{3, 3, 3}});
    const std::map<std::string, std::set<std::size_t>> expected{
        {"print_config_item_with_filament_override", {2}}};
    CHECK(partial_config.partially_defined_slots() == expected);
}

TEST_CASE("Partial configs differ by partially defined slots", "[Config]")
{
    std::vector<TestFilamentSettings> sparse(3);
    sparse.at(2).overrides.set("print_config_item_with_filament_override", -20);
    std::vector<TestFilamentSettings> full(3);
    for (TestFilamentSettings& box : full) {
        box.overrides.set("print_config_item_with_filament_override", -20);
    }
    const Slic3r::Domain::Preset::HwPrinterConfig hw_config{
        Slic3r::Test::build_fff_printer_config(3, {})};
    const PartialConfig sparse_config{{filament_refs(sparse)}, hw_config};
    const PartialConfig full_config{{filament_refs(full)}, hw_config};

    CHECK(sparse_config.values() == full_config.values());
    CHECK(full_config.partially_defined_slots().empty());
    CHECK(sparse_config != full_config);
    CHECK(sparse_config.hash() != full_config.hash());
    CHECK(
        sparse_config.diff_keys(full_config)
        == std::vector<std::string>{"print_config_item_with_filament_override"});
}

TEST_CASE("Config view merges partially defined slots element-wise", "[Config]")
{
    const std::string key{"print_config_item_with_filament_and_object_override"};
    TestPrintSettings print_settings;
    print_settings.items.opt(key).set(22);
    const std::vector<TestFilamentSettings> filament_settings(3);
    const auto full_config{std::make_shared<TestFullConfig>(print_settings, filament_settings)};

    TestObjectSettings object_settings;
    object_settings.overrides.set(key, 5);
    const PartialConfig object_config{{object_settings}, full_config->hw_config()};

    std::vector<TestFilamentSettings> slot_boxes(3);
    slot_boxes.at(2).overrides.set(key, -20);
    const PartialConfig slot_config{{filament_refs(slot_boxes)}, full_config->hw_config()};

    SECTION("over the full config")
    {
        Slic3r::Domain::ConfigView view{full_config, {slot_config}};
        view.finalize();
        CHECK(view.get<std::vector<int>>(key) == std::vector<int>{22, 22, -20});
    }

    SECTION("over an object override")
    {
        Slic3r::Domain::ConfigView view{full_config, {object_config, slot_config}};
        view.finalize();
        CHECK(view.get<std::vector<int>>(key) == std::vector<int>{5, 5, -20});
    }

    SECTION("an object override above a slot override replaces every slot")
    {
        Slic3r::Domain::ConfigView view{full_config, {slot_config, object_config}};
        view.finalize();
        CHECK(view.get<std::vector<int>>(key) == std::vector<int>{5, 5, 5});
    }
}
