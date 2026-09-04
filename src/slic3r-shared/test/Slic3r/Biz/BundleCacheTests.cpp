#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <vector>

#include "Slic3r/Biz/CerealUtils.hpp"
#include <cereal/archives/binary.hpp>

#include "boost/variant.hpp"

#include "Slic3r/Domain/Expr/ExprAst.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Preset/Bundle.hpp"
#include "Slic3r/Domain/ConfigDef.hpp"
#include "Slic3r/Domain/ConfigValue.hpp"
#include "Slic3r/Domain/ConfigBoxesFDM.hpp"
#include "Slic3r/Domain/Config.hpp"


namespace Slic3r {

template <typename T>
T test_serialization_roundtrip(const T& original)
{
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    {
        cereal::BinaryOutputArchive oarchive(ss);
        oarchive(original);
        // The archive is flushed to the sstream on destruction.
    }
    T deserialized;
    {
        ss.seekg(0);
        cereal::BinaryInputArchive iarchive(ss);
        iarchive(deserialized);
    }
    return deserialized;
}


TEST_CASE("ExprAst (Variant) Serialization Roundtrip", "[Serialization][Expr]")
{
    using namespace Domain;
    
    SECTION("Simple Binary Expression") {
        // (var_a == 10.5)
        Expr::ExprAst original = Expr::Binary(
            Expr::BinaryOp::Eq, 
            Expr::VarRef("var_a"), 
            10.5
        );

        auto deserialized = test_serialization_roundtrip(original);
        
        const auto& original_re = boost::get<Expr::Binary>(original);
        const auto& deserialized_re = boost::get<Expr::Binary>(deserialized);

        REQUIRE(deserialized_re.op == original_re.op);
        REQUIRE(boost::get<Expr::VarRef>(deserialized_re.left).name == boost::get<Expr::VarRef>(original_re.left).name);
        REQUIRE(boost::get<double>(deserialized_re.right) == boost::get<double>(original_re.right));
    }
}



TEST_CASE("Preset::Bundle Serialization Roundtrip", "[Serialization][Preset]")
{
    Domain::Preset::Bundle original;
    original.vendor_bundles["MyVendor"].vendor_data.info.name = "MyVendor";
    original.vendor_bundles["MyVendor"].presets[Domain::Preset::PresetKind::FdmPrinter].push_back({});
    original.vendor_bundles["MyVendor"].presets[Domain::Preset::PresetKind::FdmPrinter][0].id = "MyPrinter";
    original.printer_configs["MyPrinterConfig"] = Domain::Preset::HwPrinterConfig{
        "MyPrinterConfig",
        "",
        std::nullopt,
        "",
        "",
        "",
        "My Printer",
        "My Printer",
        Domain::PrinterTechnology::FFF,
        {"MP-1", "MP-Base"},
    };
    original.evaluated_presets["MyPrinterConfig"].push_back({});
    auto& eval_preset = original.evaluated_presets["MyPrinterConfig"][0];
    eval_preset.preset.id = "MyEvaluatedPreset";
    eval_preset.preset.name = "Default Quality";
    eval_preset.preset.values.emplace<Domain::PrinterSettings>();

    auto deserialized = test_serialization_roundtrip(original);

    REQUIRE(deserialized.vendor_bundles.size() == 1);
    REQUIRE(deserialized.vendor_bundles.count("MyVendor"));
    const auto& vendor_bundle = deserialized.vendor_bundles.at("MyVendor");
    REQUIRE(vendor_bundle.vendor_data.info.name == "MyVendor");
    REQUIRE(vendor_bundle.presets.at(Domain::Preset::PresetKind::FdmPrinter).size() == 1);
    REQUIRE(vendor_bundle.presets.at(Domain::Preset::PresetKind::FdmPrinter)[0].id == "MyPrinter");

    REQUIRE(deserialized.printer_configs.size() == 1);
    REQUIRE(deserialized.printer_configs.count("MyPrinterConfig"));
    const auto& printer_config = deserialized.printer_configs.at("MyPrinterConfig");
    REQUIRE(printer_config.id == "MyPrinterConfig");
    REQUIRE(printer_config.name == "My Printer");
    REQUIRE(printer_config.technology == Domain::PrinterTechnology::FFF);
    REQUIRE(printer_config.model.model == "MP-1");
    REQUIRE(printer_config.model.base_model == "MP-Base");

    REQUIRE(deserialized.evaluated_presets.size() == 1);
    REQUIRE(deserialized.evaluated_presets.count("MyPrinterConfig"));
    const auto& eval_presets = deserialized.evaluated_presets.at("MyPrinterConfig");
    REQUIRE(eval_presets.size() == 1);
    const auto& deserialized_eval_preset = eval_presets[0];
    REQUIRE(deserialized_eval_preset.preset.id == "MyEvaluatedPreset");
    REQUIRE(deserialized_eval_preset.preset.name == "Default Quality");
    REQUIRE(std::holds_alternative<Domain::PrinterSettings>(deserialized_eval_preset.preset.values));
}



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
    auto def{defs.add("print_config_item", typeid(int))};
    def->category = ConfigItemDef::Category::Hidden;
    def->location = Print;
    def->init_fn = []() { return ConfigValue{1}; };

    def = defs.add("print_config_item_with_filament_override", typeid(int));
    def->category = ConfigItemDef::Category::Hidden;
    def->location = Print;
    def->overrides_in = {Filament};
    def->init_fn = []() { return ConfigValue{1}; };

    def = defs.add("filament_config_item", typeid(int));
    def->category = ConfigItemDef::Category::Hidden;
    def->location = Filament;
    def->init_fn = []() { return ConfigValue{1}; };

    def = defs.add("print_enum_config_items_with_filament_override", typeid(EnumWrapper));
    def->category = ConfigItemDef::Category::Hidden;
    def->location = Print;
    def->overrides_in = {Filament};
    def->init_fn = []() { return ConfigValue{EnumWrapper{TestEnum::One, &test_enum_def}}; };
}

const ConfigDefinitions& defs() {
    static ConfigDefinitions defs_var{{Print, Filament}, init};
    return defs_var;
}

struct TestPrintSettings : ConfigBox {
    TestPrintSettings(): ConfigBox(defs(), Print) {}
};

struct TestFilamentSettings : ConfigBox {
    TestFilamentSettings(): ConfigBox(defs(), Filament) {}
};



TEST_CASE("ConfigBox Serialization Roundtrip", "[Config]")
{
    TestPrintSettings orig_print;
    TestFilamentSettings orig_filament;
    
    orig_print.items.opt("print_config_item").set(2);
    orig_print.items.opt("print_config_item_with_filament_override").set(2);
    orig_print.items.opt("print_enum_config_items_with_filament_override").set(TestEnum::Two);

    orig_filament.items.opt("filament_config_item").set<int>(2);
    orig_filament.overrides.set("print_config_item_with_filament_override", 3);
    orig_filament.overrides.set("print_enum_config_items_with_filament_override", TestEnum::Three);
    orig_filament.overrides.disable("print_config_item_with_filament_override");
    orig_filament.overrides.enable("print_enum_config_items_with_filament_override");


    auto deser_print = test_serialization_roundtrip(orig_print);
    auto deser_filament = test_serialization_roundtrip(orig_filament);

    REQUIRE(deser_print.items.opt("print_config_item").get<int>() == 2);
    REQUIRE(deser_print.items.opt("print_config_item_with_filament_override").get<int>() == 2);
    REQUIRE(deser_print.items.opt("print_enum_config_items_with_filament_override").get<TestEnum>() == TestEnum::Two);

    REQUIRE(deser_filament.items.opt("filament_config_item").get<int>() == 2);
    REQUIRE(deser_filament.overrides.get("print_config_item_with_filament_override") == std::nullopt);
    REQUIRE(deser_filament.overrides.get("print_enum_config_items_with_filament_override")->get<TestEnum>() == TestEnum::Three);
}



} // namespace Slic3r
