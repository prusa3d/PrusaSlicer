#include "Slic3r/Domain/ConfigDefsFDM.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace Slic3r::Domain;

namespace {

// Locations of the per-object (and per-volume) settings.
bool is_object_location(const ConfigLocation& location)
{
    if (const auto* fdm = std::get_if<FDMConfigLocation>(&location)) {
        return *fdm == FDMConfigLocation::Object || *fdm == FDMConfigLocation::Volume;
    }
    if (const auto* sla = std::get_if<SLAConfigLocation>(&location)) {
        return *sla == SLAConfigLocation::Object;
    }
    return false;
}

void check_object_override_option_groups(const ConfigDefinitions& defs)
{
    std::size_t checked{0};
    for (const ConfigItemDef& def : defs.defs()) {
        // OverrideSettingsDialog does not show the extruder category.
        if (def.category == ConfigItemDef::Category::Hidden
            || def.category == ConfigItemDef::Category::Object_Extruders
            || !std::ranges::any_of(def.overrides_in, is_object_location))
        {
            continue;
        }
        INFO(def.name);
        // OverrideSettingsDialog groups the options of a category by their option group and
        // panics on an unknown one.
        CHECK(def.option_group != ConfigItemDef::OptionGroup::Unknown);
        ++checked;
    }
    CHECK(checked > 0);
}

} // namespace

TEST_CASE("Every per-object override has an option group", "[ConfigDefs]")
{
    SECTION("FFF") { check_object_override_option_groups(get_defs_fdm()); }
    SECTION("SLA") { check_object_override_option_groups(get_defs_sla()); }
}

TEST_CASE("Material preset colors are hidden", "[ConfigDefs]")
{
    // The color is set at the project level, the preset color is only its fallback.
    const auto check_hidden = [](const ConfigDefinitions& defs, const std::string& name) {
        INFO(name);
        const auto it{std::ranges::find(defs.defs(), name, &ConfigItemDef::name)};
        REQUIRE(it != defs.defs().end());
        CHECK(it->category == ConfigItemDef::Category::Hidden);
    };
    SECTION("FFF") { check_hidden(get_defs_fdm(), "filament_colour"); }
    SECTION("SLA") { check_hidden(get_defs_sla(), "material_colour"); }
}
