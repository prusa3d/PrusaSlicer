#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Config/CategoryUtils.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"

#include <set>

using namespace Slic3r;
using namespace Slic3r::Domain;

namespace {

// Locations of the printer, print and material settings dialogs.
bool is_settings_dialog_location(const ConfigLocation& location)
{
    if (const auto* fdm = std::get_if<FDMConfigLocation>(&location)) {
        return *fdm == FDMConfigLocation::Printer || *fdm == FDMConfigLocation::Print
            || *fdm == FDMConfigLocation::Tool || *fdm == FDMConfigLocation::Filament;
    }
    if (const auto* sla = std::get_if<SLAConfigLocation>(&location)) {
        return *sla == SLAConfigLocation::Printer || *sla == SLAConfigLocation::Print
            || *sla == SLAConfigLocation::Material;
    }
    return false;
}

void check_categories(const ConfigDefinitions& defs, const PrinterTechnology technology)
{
    std::set<ConfigItemDef::Category> categories;
    for (const ConfigItemDef& def : defs.defs()) {
        if (def.category != ConfigItemDef::Category::Hidden
            && is_settings_dialog_location(def.location))
        {
            categories.insert(def.category);
        }
    }
    REQUIRE(!categories.empty());

    for (const ConfigItemDef::Category category : categories) {
        INFO(static_cast<int>(category));
        // Panics for a category it does not know.
        CHECK_NOTHROW(App::CategoryUtils::category_render_icon(category, technology));
    }
}

} // namespace

TEST_CASE("Every settings dialog category has an icon", "[CategoryUtils]")
{
    SECTION("FFF") { check_categories(get_defs_fdm(), PrinterTechnology::FFF); }
    SECTION("SLA") { check_categories(get_defs_sla(), PrinterTechnology::SLA); }
}
