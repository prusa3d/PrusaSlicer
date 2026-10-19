#include "Slic3r/Domain/Preset/SelectedPreset.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace Slic3r::Domain;
using namespace Slic3r::Domain::Preset;

namespace {

EvaluatedPresetMetadata preset_metadata(const std::string& id)
{
    return {.root_id = id + "_root", .id = id, .name = id + " name"};
}

void check_metadata(const EvaluatedPresetMetadata& actual, const EvaluatedPresetMetadata& expected)
{
    CHECK(actual.root_id == expected.root_id);
    CHECK(actual.id == expected.id);
    CHECK(actual.name == expected.name);
}

} // namespace

TEST_CASE("SelectedPreset::make keeps the metadata of each preset", "[SelectedPreset]")
{
    SECTION("SLA")
    {
        const SelectedPresetMetadata metadata{
            .hw_config = HwPrinterConfig{.technology = PrinterTechnology::SLA, .tool_count = 1},
            .printer   = preset_metadata("printer"),
            .print     = preset_metadata("print"),
            .materials = {preset_metadata("material")},
        };

        const SelectedPreset selected = SelectedPreset::make(metadata, ConfigPackSLA{});

        REQUIRE(selected.technology() == PrinterTechnology::SLA);
        check_metadata(selected.printer.metadata(), metadata.printer);
        check_metadata(selected.print.metadata(), metadata.print);
        REQUIRE(selected.materials.size() == 1);
        check_metadata(selected.materials.front().metadata(), metadata.materials.front());
        CHECK(selected.tools.empty());

        const SelectedPresetMetadata roundtrip = selected.metadata();
        check_metadata(roundtrip.print, metadata.print);
    }

    SECTION("FFF")
    {
        const SelectedPresetMetadata metadata{
            .hw_config = HwPrinterConfig{.technology = PrinterTechnology::FFF, .tool_count = 1, .tools = {{}}},
            .printer   = preset_metadata("printer"),
            .print     = preset_metadata("print"),
            .tools     = {preset_metadata("tool")},
            .materials = {preset_metadata("material")},
        };

        const SelectedPreset selected = SelectedPreset::make(metadata, ConfigPackFDM{1});

        REQUIRE(selected.technology() == PrinterTechnology::FFF);
        check_metadata(selected.printer.metadata(), metadata.printer);
        check_metadata(selected.print.metadata(), metadata.print);
        REQUIRE(selected.tools.size() == 1);
        check_metadata(selected.tools.front().metadata(), metadata.tools.front());
        REQUIRE(selected.materials.size() == 1);
        check_metadata(selected.materials.front().metadata(), metadata.materials.front());
    }
}
