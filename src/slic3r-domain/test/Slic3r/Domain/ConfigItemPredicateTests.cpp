#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Domain/ConfigItemPredicate.hpp"
#include "Slic3r/Domain/ConfigValue.hpp"

#include <map>
#include <string>

using Slic3r::Domain::all_of;
using Slic3r::Domain::any_of;
using Slic3r::Domain::ConfigItemLookup;
using Slic3r::Domain::ConfigItemPredicate;
using Slic3r::Domain::ConfigValue;
using Slic3r::Domain::EnumValueDefs;
using Slic3r::Domain::EnumWrapper;
using Slic3r::Domain::evaluate;
using Slic3r::Domain::negate;
using Slic3r::Domain::Percentage;
using Slic3r::Domain::when_all_enabled;
using Slic3r::Domain::when_any_enabled;
using Slic3r::Domain::when_disabled;
using Slic3r::Domain::when_enabled;
using Slic3r::Domain::when_enum_in;
using Slic3r::Domain::when_enum_is;
using Slic3r::Domain::when_greater_than;
using Slic3r::Domain::when_positive;

namespace {

enum class PerimeterGenerator
{
    Classic,
    Arachne
};

const EnumValueDefs perimeter_generator_def{{
    {int(PerimeterGenerator::Classic), "classic", "Classic"},
    {int(PerimeterGenerator::Arachne), "arachne", "Arachne"},
}};

/// Stands in for the config while a rule is evaluated.
class FakeLookup : public ConfigItemLookup
{
public:
    const ConfigValue* find_value(const std::string& key) const override
    {
        const auto it = m_values.find(key);
        return it == m_values.end() ? nullptr : &it->second;
    }

    template <typename T>
    void set(const std::string& key, const T& value)
    {
        m_values.insert_or_assign(key, ConfigValue{value});
    }

private:
    std::map<std::string, ConfigValue> m_values;
};

} // namespace

TEST_CASE("An unset predicate means the setting always applies", "[ConfigItemPredicate]")
{
    const FakeLookup cfg;
    CHECK(evaluate(ConfigItemPredicate{}, cfg));
}

TEST_CASE("A predicate naming an absent setting reads false, not throws", "[ConfigItemPredicate]")
{
    const FakeLookup cfg;
    CHECK_FALSE(when_enabled("no_such_setting")(cfg));
    CHECK(when_disabled("no_such_setting")(cfg));
    CHECK_FALSE(when_positive("no_such_setting")(cfg));
}

TEST_CASE("A predicate reading a wrong-typed value falls back", "[ConfigItemPredicate]")
{
    FakeLookup cfg;
    cfg.set("avoid_crossing_perimeters", 3.0); // a double where a bool is expected
    CHECK_FALSE(when_enabled("avoid_crossing_perimeters")(cfg));
}

TEST_CASE("Travel avoidance strategies rule each other out", "[ConfigItemPredicate]")
{
    // The two strategies are mutually exclusive and the detour length only
    // applies to one of them, which is what these three rules encode.
    const ConfigItemPredicate curled = when_disabled("avoid_crossing_perimeters");
    const ConfigItemPredicate perimeters = when_disabled("avoid_crossing_curled_overhangs");
    const ConfigItemPredicate max_detour = when_enabled("avoid_crossing_perimeters");

    FakeLookup cfg;
    cfg.set("avoid_crossing_perimeters", false);
    cfg.set("avoid_crossing_curled_overhangs", false);

    SECTION("both are offered while neither is chosen")
    {
        CHECK(curled(cfg));
        CHECK(perimeters(cfg));
        CHECK_FALSE(max_detour(cfg));
    }

    SECTION("choosing perimeters rules out curled overhangs and discloses the detour length")
    {
        cfg.set("avoid_crossing_perimeters", true);
        CHECK_FALSE(curled(cfg));
        CHECK(perimeters(cfg));
        CHECK(max_detour(cfg));
    }

    SECTION("choosing curled overhangs rules out perimeters")
    {
        cfg.set("avoid_crossing_curled_overhangs", true);
        CHECK(curled(cfg));
        CHECK_FALSE(perimeters(cfg));
        CHECK_FALSE(max_detour(cfg));
    }
}

TEST_CASE("Numeric rules accept every numeric storage", "[ConfigItemPredicate]")
{
    FakeLookup cfg;

    SECTION("percentage")
    {
        cfg.set("fill_density", Percentage{0.0});
        CHECK_FALSE(when_positive("fill_density")(cfg));
        cfg.set("fill_density", Percentage{15.0});
        CHECK(when_positive("fill_density")(cfg));
    }

    SECTION("int")
    {
        cfg.set("perimeters", 0);
        CHECK_FALSE(when_positive("perimeters")(cfg));
        cfg.set("perimeters", 2);
        CHECK(when_positive("perimeters")(cfg));
    }

    SECTION("double, against a threshold")
    {
        cfg.set("mmu_segmented_region_max_width", 1.5);
        CHECK(when_greater_than("mmu_segmented_region_max_width", 1.0)(cfg));
        CHECK_FALSE(when_greater_than("mmu_segmented_region_max_width", 2.0)(cfg));
    }
}

TEST_CASE("Enum rules select on the underlying value", "[ConfigItemPredicate]")
{
    FakeLookup cfg;
    cfg.set(
        "perimeter_generator",
        EnumWrapper{PerimeterGenerator::Arachne, &perimeter_generator_def}
    );

    const ConfigItemPredicate arachne =
        when_enum_is("perimeter_generator", int(PerimeterGenerator::Arachne));

    CHECK(arachne(cfg));
    CHECK(when_enum_in(
        "perimeter_generator",
        {int(PerimeterGenerator::Classic), int(PerimeterGenerator::Arachne)}
    )(cfg));

    cfg.set(
        "perimeter_generator",
        EnumWrapper{PerimeterGenerator::Classic, &perimeter_generator_def}
    );
    CHECK_FALSE(arachne(cfg));
}

TEST_CASE("thin_walls needs perimeters and the classic generator", "[ConfigItemPredicate]")
{
    const ConfigItemPredicate arachne =
        when_enum_is("perimeter_generator", int(PerimeterGenerator::Arachne));
    const ConfigItemPredicate thin_walls = all_of({when_positive("perimeters"), negate(arachne)});

    FakeLookup cfg;
    cfg.set("perimeters", 2);

    cfg.set(
        "perimeter_generator",
        EnumWrapper{PerimeterGenerator::Classic, &perimeter_generator_def}
    );
    CHECK(thin_walls(cfg));

    cfg.set(
        "perimeter_generator",
        EnumWrapper{PerimeterGenerator::Arachne, &perimeter_generator_def}
    );
    CHECK_FALSE(thin_walls(cfg));

    cfg.set(
        "perimeter_generator",
        EnumWrapper{PerimeterGenerator::Classic, &perimeter_generator_def}
    );
    cfg.set("perimeters", 0);
    CHECK_FALSE(thin_walls(cfg));
}

TEST_CASE("Combinators treat no inputs as no constraint", "[ConfigItemPredicate]")
{
    FakeLookup cfg;
    cfg.set("a", true);
    cfg.set("b", false);

    CHECK(all_of({})(cfg));
    CHECK_FALSE(any_of({})(cfg));

    CHECK(when_any_enabled({"a", "b"})(cfg));
    CHECK_FALSE(when_all_enabled({"a", "b"})(cfg));

    cfg.set("b", true);
    CHECK(when_all_enabled({"a", "b"})(cfg));

    // Negating nothing stays nothing, so the setting keeps applying.
    CHECK_FALSE(static_cast<bool>(negate(ConfigItemPredicate{})));
    CHECK(evaluate(negate(ConfigItemPredicate{}), cfg));
}
