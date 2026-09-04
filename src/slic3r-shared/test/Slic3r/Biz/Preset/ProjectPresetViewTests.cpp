#include "Slic3r/App/Preview/PreviewCameraGizmo.hpp"

#include <catch2/catch_test_macros.hpp>
#include <range/v3/range/conversion.hpp>
#include "Slic3r/Biz/Preset/ProjectPresetView.hpp"

using namespace Slic3r::Biz::Preset;
using namespace Slic3r;

namespace {
Domain::Preset::HwPrinterConfig& add_hw_config(Domain::Preset::PrinterConfigs& dest, const std::string& id)
{
    dest[id] = Domain::Preset::HwPrinterConfig{.id=id, .tool_count = 1, .tools={{}}};
    return dest[id];
}

template <typename T>
struct IsStdPairWithSecondBool : std::false_type {};

template <typename T>
struct IsStdPairWithSecondBool<std::pair<T,bool>> : std::true_type {};

template<typename R>
std::vector<std::string> range_to_ids(R&& range)
{
    return ranges::to_vector(
        range
        | ranges::views::transform(
            []<typename T>(const T& item) -> const std::string&
            {
                if constexpr (IsStdPairWithSecondBool<T>::value) {
                    return item.first.get().id;
                } else {
                    return item.id;
                }
            }
        )
    );
}

}

TEST_CASE("HwPrinterConfigProjectView", "[preset]")
{
    SECTION("Empty view")
    {
        Domain::Preset::Bundle bundle;
        RuntimePresets runtime;
        HwPrinterConfigProjectView view{bundle, runtime};
        REQUIRE(std::ranges::empty(view.items()) == true);
    }

    SECTION("Single in bundle")
    {
        Domain::Preset::Bundle bundle;
        RuntimePresets runtime;

        add_hw_config(bundle.printer_configs, "id1");

        HwPrinterConfigProjectView view{bundle, runtime};
        REQUIRE(range_to_ids(view.items()) == std::vector<std::string>{"id1"});
    }

    SECTION("Single in runtime")
    {
        Domain::Preset::Bundle bundle;
        RuntimePresets runtime;

        add_hw_config(runtime.printer_configs, "id1");

        HwPrinterConfigProjectView view{bundle, runtime};
        REQUIRE(range_to_ids(view.items()) == std::vector<std::string>{"id1"});
    }
    SECTION("One in both")
    {
        Domain::Preset::Bundle bundle;
        RuntimePresets runtime;

        add_hw_config(bundle.printer_configs, "id1");
        add_hw_config(runtime.printer_configs, "id2");

        HwPrinterConfigProjectView view{bundle, runtime};
        REQUIRE(range_to_ids(view.items()) == std::vector<std::string>{"id1", "id2"});
    }
}

TEST_CASE("PrinterPresetProjectView", "[preset]")
{
    SECTION("Empty view")
    {
        for (const bool preset_in_bundle : {true, false}) {
            INFO((preset_in_bundle ? "Testing preset in bundle" : "Testing preset in runtime"));
            Domain::Preset::Bundle bundle;
            RuntimePresets runtime;
            add_hw_config(preset_in_bundle ? bundle.printer_configs : runtime.printer_configs, "id1");
            PrinterPresetProjectView view{bundle, runtime, "id1"};
            REQUIRE(ranges::empty(view.items()) == true);
        }
    }

    SECTION("Single item view")
    {
        for (const bool preset_in_bundle : {true, false}) {
            INFO((preset_in_bundle ? "Testing preset in bundle" : "Testing preset in runtime"));
            Domain::Preset::Bundle bundle;
            RuntimePresets runtime;
            add_hw_config(preset_in_bundle ? bundle.printer_configs : runtime.printer_configs, "id1");

            if (preset_in_bundle)
                bundle.evaluated_presets["id1"] = std::vector{Domain::Preset::EvaluatedPrinterPreset{.preset = {.id = "id1"}}};
            else
                runtime.printer["id1"] = std::vector{Domain::Preset::EvaluatedPrinterPreset::Preset{.id = "id1"}};

            PrinterPresetProjectView view{bundle, runtime, "id1"};
            REQUIRE(range_to_ids(view.items()) == std::vector<std::string>{"id1"});
        }
    }
    SECTION("Two item view")
    {
        Domain::Preset::Bundle bundle;
        RuntimePresets runtime;
        add_hw_config(bundle.printer_configs, "id1");
        add_hw_config(runtime.printer_configs, "id2");

        bundle.evaluated_presets["id1"] = std::vector{Domain::Preset::EvaluatedPrinterPreset{.preset = {.id = "id1"}}};
        runtime.printer["id1"] = std::vector{Domain::Preset::EvaluatedPrinterPreset::Preset{.id = "id2"}};

        PrinterPresetProjectView view{bundle, runtime, "id1"};
        REQUIRE(range_to_ids(view.items()) == std::vector<std::string>{"id1", "id2"});

    }
}

TEST_CASE("PrintPresetProjectView", "[preset]")
{
    SECTION("Empty view")
    {
        Domain::Preset::Bundle bundle;
        RuntimePresets runtime;
        add_hw_config(bundle.printer_configs, "id1");


        PrintPresetProjectView view{bundle, runtime, "id1", "printer1"};
        REQUIRE(ranges::empty(view.items()) == true);
    }

    SECTION("Single item view")
    {
        for (const bool preset_in_bundle : {true, false}) {
            INFO((preset_in_bundle ? "Testing preset in bundle" : "Testing preset in runtime"));
            Domain::Preset::Bundle bundle;
            RuntimePresets runtime;
            add_hw_config(bundle.printer_configs, "id1");

            if (preset_in_bundle) {
                bundle.evaluated_presets["id1"] = std::vector{Domain::Preset::EvaluatedPrinterPreset{
                    .preset = {.id = "printer1"},
                    .prints = {Domain::Preset::EvaluatedPrintPreset{{.id = "print1"}, {}, {}}}
                }};
            } else {
                runtime.print[{"id1", "printer1"}] = std::vector{Domain::Preset::EvaluatedPrintPreset::Preset{.id="print1"}};
            }

            PrintPresetProjectView view{bundle, runtime, "id1", "printer1"};
            REQUIRE(range_to_ids(view.items()) == std::vector<std::string>{"print1"});
        }
    }

    SECTION("Two items view")
    {
        Domain::Preset::Bundle bundle;
        RuntimePresets runtime;
        add_hw_config(bundle.printer_configs, "id1");

        bundle.evaluated_presets["id1"] = std::vector{Domain::Preset::EvaluatedPrinterPreset{
            .preset = {.id = "printer1"},
            .prints = {Domain::Preset::EvaluatedPrintPreset{{.id = "print1"}, {}, {}}}
        }};
        runtime.print[{"id1", "printer1"}] = std::vector{Domain::Preset::EvaluatedPrintPreset::Preset{.id="print2"}};

        PrintPresetProjectView view{bundle, runtime, "id1", "printer1"};
        REQUIRE(range_to_ids(view.items()) == std::vector<std::string>{"print1", "print2"});
    }
}

TEST_CASE("ToolPrintPresetProjectView", "[preset]")
{
    SECTION("Empty view")
    {
        Domain::Preset::Bundle bundle;
        RuntimePresets runtime;
        add_hw_config(bundle.printer_configs, "id1");

        ToolPrintPresetProjectView view{bundle, runtime, "id1", "printer1", "print1", 0};
        REQUIRE(ranges::empty(view.items()) == true);
    }

    SECTION("Single item view")
    {
        for (const bool preset_in_bundle : {true, false}) {
            INFO((preset_in_bundle ? "Testing preset in bundle" : "Testing preset in runtime"));
            Domain::Preset::Bundle bundle;
            RuntimePresets runtime;
            const auto& hw_config = add_hw_config(bundle.printer_configs, "id1");

            bundle.evaluated_presets["id1"] = std::vector{Domain::Preset::EvaluatedPrinterPreset{
                .preset = {.id = "printer1"},
                .prints = {Domain::Preset::EvaluatedPrintPreset{{.id = "print1"}, {{}}, {}}}
            }};
            if (preset_in_bundle) {
                bundle.evaluated_presets["id1"].front().prints.front().tools.front().emplace_back(
                    Domain::Preset::EvaluatedToolPrintPreset::Preset{.id = "tool_print1"}
                );
            } else {
                runtime.add_tool_print({"id1", "printer1", "print1"}, hw_config, 0, {.id="tool_print1"});
            }

            ToolPrintPresetProjectView view{bundle, runtime, "id1", "printer1", "print1", 0};
            REQUIRE(range_to_ids(view.items()) == std::vector<std::string>{"tool_print1"});
        }
    }

    SECTION("Two items view")
    {
        Domain::Preset::Bundle bundle;
        RuntimePresets runtime;
        const auto& hw_config = add_hw_config(bundle.printer_configs, "id1");

        bundle.evaluated_presets["id1"] = std::vector{Domain::Preset::EvaluatedPrinterPreset{
            .preset = {.id = "printer1"},
            .prints = {Domain::Preset::EvaluatedPrintPreset{{.id = "print1"}, {{}}, {}}}
        }};
        bundle.evaluated_presets["id1"].front().prints.front().tools.front().emplace_back(
            Domain::Preset::EvaluatedToolPrintPreset::Preset{.id = "tool_print1"}
        );
        runtime.add_tool_print({"id1", "printer1", "print1"}, hw_config, 0, {.id="tool_print2"});

        ToolPrintPresetProjectView view{bundle, runtime, "id1", "printer1", "print1", 0};
        REQUIRE(range_to_ids(view.items()) == std::vector<std::string>{"tool_print1", "tool_print2"});
    }
}

TEST_CASE("MaterialPresetProjectView", "[preset]")
{
    SECTION("Empty view")
    {
        Domain::Preset::Bundle bundle;
        RuntimePresets runtime;
        add_hw_config(bundle.printer_configs, "id1");

        MaterialPresetProjectView view{bundle, runtime, "id1", "printer1", "print1", 0};
        REQUIRE(ranges::empty(view.items()) == true);
    }

    SECTION("Single item view")
    {
        for (const bool preset_in_bundle : {true, false}) {
            INFO((preset_in_bundle ? "Testing preset in bundle" : "Testing preset in runtime"));
            Domain::Preset::Bundle bundle;
            RuntimePresets runtime;
            const auto& hw_config = add_hw_config(bundle.printer_configs, "id1");

            bundle.evaluated_presets["id1"] = std::vector{Domain::Preset::EvaluatedPrinterPreset{
                .preset = {.id = "printer1"},
                .prints = {Domain::Preset::EvaluatedPrintPreset{{.id = "print1"}, {{}}, {{}}}}
            }};
            if (preset_in_bundle) {
                bundle.evaluated_presets["id1"].front().prints.front().materials.front().emplace_back(
                    Domain::Preset::EvaluatedMaterialPreset::Preset{.id = "material1"}
                );
            } else {
                runtime.add_material({"id1", "printer1", "print1"}, hw_config, 0, {.id="material1"});
            }

            MaterialPresetProjectView view{bundle, runtime, "id1", "printer1", "print1", 0};
            REQUIRE(range_to_ids(view.items()) == std::vector<std::string>{"material1"});
        }
    }

    SECTION("Two items view")
    {
        Domain::Preset::Bundle bundle;
        RuntimePresets runtime;
        const auto& hw_config = add_hw_config(bundle.printer_configs, "id1");

        bundle.evaluated_presets["id1"] = std::vector{Domain::Preset::EvaluatedPrinterPreset{
            .preset = {.id = "printer1"},
            .prints = {Domain::Preset::EvaluatedPrintPreset{{.id = "print1"}, {{}}, {{}}}}
        }};
        bundle.evaluated_presets["id1"].front().prints.front().materials.front().emplace_back(
            Domain::Preset::EvaluatedMaterialPreset::Preset{.id = "material1"}
        );
        runtime.add_material({"id1", "printer1", "print1"}, hw_config, 0, {.id="material2"});

        MaterialPresetProjectView view{bundle, runtime, "id1", "printer1", "print1", 0};
        REQUIRE(range_to_ids(view.items()) == std::vector<std::string>{"material1", "material2"});
    }
}