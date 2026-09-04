#include <iostream>
#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/Preset/PresetCollectionEvaluator.hpp"
#include "Slic3r/Biz/Preset/IO/PresetLoader.hpp"
#include "Slic3r/Biz/Yaml/Yaml.hpp"

#include "nlohmann/json.hpp"

using namespace Slic3r::Domain::Preset;
using namespace Slic3r::Biz::Preset;
using namespace Slic3r::Biz;



void collect_named_presets(IdentifiedPresets& named_presets, PresetNodePath path) {
    const auto& node = *path.back();
    if (!node.id.empty())
        named_presets[node.id] = path;

    for (const auto& child : node.variants) {
        PresetNodePath child_path = path;
        child_path.push_back(&child);
        collect_named_presets(named_presets, child_path);
    }
}

PresetCollectionEvaluator create_evaluator(const IO::PresetLoader& loader, PresetKind kind)
{
    static IdentifiedPresets named_presets;

    named_presets.clear();

    const auto& preset_nodes = loader.presets().find(kind)->second;
    for (const auto& p : preset_nodes)
        collect_named_presets(named_presets, {&p});

    return PresetCollectionEvaluator{preset_nodes, named_presets, {}, {}, "test"};
}


TEST_CASE("Preset Collection Evaluator", "[preset]")
{
    SECTION("simplest")
    {
        const char* yaml = R"(
kind: printer
id: '*common*'
values:
  a: 1
  b: "x"
  c: true
features:
  f1: 321
)";

        IO::PresetLoader loader;
        loader.load_from_string(yaml);
        auto eval = create_evaluator(loader, PresetKind::FdmPrinter);

        auto evals = eval.eval_preset({}, false);
        REQUIRE(evals.size() == 1);
        const auto& p = evals.front();
        REQUIRE(p.id == "*common*");
        REQUIRE(p.conditions.empty() == true);
        REQUIRE(p.values.size() == 3);
        REQUIRE(std::get<double>(p.values.find("a")->second) == 1);
        REQUIRE(std::get<std::string>(p.values.find("b")->second) == "x");
        REQUIRE(std::get<bool>(p.values.find("c")->second) == true);
        REQUIRE(std::get<double>(p.features.find("f1")->second) == 321);
    }

    SECTION("simple variants")
    {
        const char* yaml = R"(
kind: printer
id: '*common*'
values:
  a: 1
  b: "x"
  c: true
features:
  f1: 1
variants:
  - condition: 'tool.nozzle_high_flow'
    features:
      f2: 1
    values:
      c: false
      d: 1
  - values:
      d: 2
    features:
      f2: 2
)";

        IO::PresetLoader loader;
        loader.load_from_string(yaml);
        auto eval = create_evaluator(loader, PresetKind::FdmPrinter);

        for (const auto& [nozzle_high_flow, expected_c_value, expected_d_value, expected_condition_count, expected_neg_cond_count] :
             {std::make_tuple(true, false, 1, 1, 0), std::make_tuple(false, true, 2, 0, 1)})
        {
            Expr::ValueMap overrides;
            overrides["tool.nozzle_high_flow"] = nozzle_high_flow;
            auto evals                         = eval.eval_preset({overrides}, false);
            REQUIRE(evals.size() == 1);
            const auto& p = evals.front();
            REQUIRE(p.id == "*common*");
            REQUIRE(p.conditions.size() == expected_condition_count);
            REQUIRE(p.negative_conditions.size() == expected_neg_cond_count);
            REQUIRE(std::get<double>(p.values.find("a")->second) == 1);
            REQUIRE(std::get<std::string>(p.values.find("b")->second) == "x");
            REQUIRE(std::get<bool>(p.values.find("c")->second) == expected_c_value);
            REQUIRE(std::get<double>(p.values.find("d")->second) == expected_d_value);
            REQUIRE(std::get<double>(p.features.find("f1")->second) == 1);
            REQUIRE(std::get<double>(p.features.find("f2")->second) == expected_d_value);
        }
    }

    SECTION("simple multiple variants")
    {
        const char* yaml = R"(
kind: printer
id: '*common*'
values:
  a: 1
  b: "x"
  c: true
variants:
  - name: 'Var 1'
    values:
      c: false
      d: 1
  - name: 'Var 2'
    values:
      d: 2
)";

        IO::PresetLoader loader;
        loader.load_from_string(yaml);
        auto eval = create_evaluator(loader, PresetKind::FdmPrinter);

        auto evals = eval.eval_preset({}, false);
        REQUIRE(evals.size() == 2);

        for (const auto& [
            index, expected_name, expected_c_value, expected_d_value
        ] : {
            std::make_tuple(0, "Var 1", false, 1), std::make_tuple(1, "Var 2", true, 2)
        }) {

            const auto& p = evals[index];
            REQUIRE(p.root_id == "*common*");
            REQUIRE(p.id == expected_name);
            REQUIRE(p.name == expected_name);
            REQUIRE(p.conditions.empty() == true);
            REQUIRE(std::get<double>(p.values.find("a")->second) == 1);
            REQUIRE(std::get<std::string>(p.values.find("b")->second) == "x");
            REQUIRE(std::get<bool>(p.values.find("c")->second) == expected_c_value);
            REQUIRE(std::get<double>(p.values.find("d")->second) == expected_d_value);
        }
    }

    SECTION("simple inherits")
    {
        const char* yaml = R"(
kind: printer
id: '*common*'
values:
  a: 1
  b: "x"
  c: true
features:
  f1: 1
  f2: 1
---
kind: printer
id: 'Printer'
name: 'Printer'
inherits:
  - '*common*'
values:
  c: false
  d: "y"
features:
  f2: 2
)";

        IO::PresetLoader loader;
        try {
            loader.load_from_string(yaml);
        }
        catch (Yaml::ParseError& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
        auto eval = create_evaluator(loader, PresetKind::FdmPrinter);

        auto evals = eval.eval_preset({}, false);
        REQUIRE(evals.size() == 2);
        auto it = std::find_if(evals.begin(), evals.end(), [](const auto& p){ return p.name == "Printer"; });
        REQUIRE(it != evals.end());
        const auto& p = *it;
        REQUIRE(p.id == "Printer");
        REQUIRE(p.conditions.empty() == true);
        REQUIRE(p.values.size() == 4);
        REQUIRE(std::get<double>(p.values.find("a")->second) == 1);
        REQUIRE(std::get<std::string>(p.values.find("b")->second) == "x");
        REQUIRE(std::get<bool>(p.values.find("c")->second) == false);
        REQUIRE(std::get<std::string>(p.values.find("d")->second) == "y");
        REQUIRE(std::get<double>(p.features.find("f1")->second) == 1);
        REQUIRE(std::get<double>(p.features.find("f2")->second) == 2);
    }

    SECTION("simple inherits product")
    {
        const char* yaml = R"(
kind: printer
id: '*common*'
values:
  a: 1
  b: "x"
  c: true
variants:
  - name: '*common*@A'
    values:
      d: 1
    features:
      f1: 1
  - name: '*common*@B'
    values:
      d: 2
    features:
      f1: 2
---
kind: printer
id: 'Printer'
name: 'Printer'
inherits:
  - '*common*'
values:
  c: false
  e: 42
variants:
  - name: 'PrinterA'
    values:
      f: 1
    features:
      f2: 1
  - name: 'PrinterB'
    values:
      f: 2
    features:
      f2: 2
)";

        IO::PresetLoader loader;
        try {
            loader.load_from_string(yaml);
        }
        catch (Yaml::ParseError& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
        auto eval = create_evaluator(loader, PresetKind::FdmPrinter);

        auto evals = eval.eval_preset({}, false);
        REQUIRE(evals.size() == 4 + 2);
        for (const auto& [name, expected_d, expected_f] : {
            std::make_tuple("PrinterA@A", 1, 1),
            std::make_tuple("PrinterA@B", 2, 1),
            std::make_tuple("PrinterB@A", 1, 2),
            std::make_tuple("PrinterB@B", 2, 2),
        }) {
            auto it = std::find_if(evals.begin(), evals.end(), [&name](const auto& p){ return p.name == name; });
            REQUIRE(it != evals.end());
            const auto& p = *it;
            REQUIRE(p.id == name);
            REQUIRE(p.root_id == "Printer");
            REQUIRE(p.conditions.empty() == true);
            REQUIRE(p.values.size() == 6);
            REQUIRE(std::get<double>(p.values.find("a")->second) == 1);
            REQUIRE(std::get<std::string>(p.values.find("b")->second) == "x");
            REQUIRE(std::get<bool>(p.values.find("c")->second) == false);
            REQUIRE(std::get<double>(p.values.find("d")->second) == expected_d);
            REQUIRE(std::get<double>(p.values.find("e")->second) == 42);
            REQUIRE(std::get<double>(p.values.find("f")->second) == expected_f);

            REQUIRE(std::get<double>(p.features.find("f1")->second) == expected_d);
            REQUIRE(std::get<double>(p.features.find("f2")->second) == expected_f);
        }
    }

    SECTION("simple unconditional inherits")
    {
        const char* yaml = R"(
kind: printer
id: '*common*'
values:
  a: 1
  b: "x"
  c: true
features:
  f1: 1
variants:
  - id: '*common*@A'
    values:
      d: 1
    features:
      f2: 1
    variants:
      - id: '*common*@A1'
        values:
          g: 11
  - id: '*common*@B'
    features:
      f2: 2
    values:
      d: 2
---
kind: printer
values:
  a: 2
  d: 2
variants:
  - id: 'Printer'
    name: 'Printer'
    unconditional_inherits:
      - '*common*@A'
    values:
      c: false
      e: 42
)";

        IO::PresetLoader loader;
        try {
            loader.load_from_string(yaml);
        }
        catch (Yaml::ParseError& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
        auto eval = create_evaluator(loader, PresetKind::FdmPrinter);

        auto evals = eval.eval_preset({}, false);
        REQUIRE(evals.size() == 2 + 1);
        for (const auto& [name, expected_d] : {
            std::make_tuple("Printer", 1),
        }) {
            auto it = std::find_if(evals.begin(), evals.end(), [&name](const auto& p){ return p.name == name; });
            REQUIRE(it != evals.end());
            const auto& p = *it;
            REQUIRE(p.id == "Printer");
            REQUIRE(p.conditions.size() == 0);
            REQUIRE(p.values.size() == 5);
            REQUIRE(std::get<double>(p.values.find("a")->second) == 1);
            REQUIRE(std::get<std::string>(p.values.find("b")->second) == "x");
            REQUIRE(std::get<bool>(p.values.find("c")->second) == false);
            REQUIRE(std::get<double>(p.values.find("d")->second) == expected_d);
            REQUIRE(std::get<double>(p.values.find("e")->second) == 42);

            REQUIRE(std::get<double>(p.features.find("f1")->second) == 1);
            REQUIRE(std::get<double>(p.features.find("f2")->second) == expected_d);
        }
    }

    SECTION("mixing unconditional inherits with inherits")
    {
        const char* yaml = R"(
kind: printer
id: '*common*'
values:
  com_a: 1
  com_b: "x"
  com_c: true
features:
  com_f1: 1
  com_f4: 1
variants:
  - condition: 'tool.nozzle_diameter == 0.6'
    id: '*common*@A'
    features:
      com_f2: 2
    values:
      com_d: 2
  - condition: 'tool.nozzle_diameter == 0.4'
    id: '*common*@B'
    values:
      com_d: 1
    features:
      com_f2: 1
    variants:
      - condition: 'tool.nozzle_high_flow'
        id: '*common*@B1'
        values:
          com_c: false
        features:
          com_f2: 3
---
kind: printer
id: '*printer_common*'
values:
  p_com_a: 10
  p_com_b: "xx"
  p_com_c: true
---
kind: printer
id: '*printer_base*'
inherits:
- '*common*'
values:
  a: 1
  b: "x"
  c: true
features:
  f1: 1
variants:
  - id: '*printer_base*@A'
    unconditional_inherits:
    - '*printer_common*'
    values:
      d: 1
    features:
      f2: 1
    variants:
      - id: '*printer_base*@A1'
        values:
          g: 11
  - id: '*printer_base*@B'
    features:
      f2: 2
    values:
      d: 2
---
kind: printer
values:
  a: 2
  d: 2
variants:
  - id: 'Printer'
    name: 'Printer'
    unconditional_inherits:
      - '*printer_base*@A'
    values:
      c: false
      e: 42
      com_b: "y"
    features:
      com_f4: 2
)";

        IO::PresetLoader loader;
        try {
            loader.load_from_string(yaml);
        }
        catch (Yaml::ParseError& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
        auto eval = create_evaluator(loader, PresetKind::FdmPrinter);

        Expr::ValueMap values{
            {"tool.nozzle_diameter", 0.4},
            {"tool.nozzle_high_flow", true},
        };

        auto evals = eval.eval_preset({values}, false);
        REQUIRE(evals.size() == 3 + 2);

        auto it = std::find_if(evals.begin(), evals.end(), [](const auto& p){ return p.name == "Printer"; });
        REQUIRE(it != evals.end());
        const auto& p = *it;
        REQUIRE(p.id == "Printer");
        REQUIRE(p.conditions.size() == 0);
        REQUIRE(p.values.size() == 12);
        REQUIRE(std::get<double>(p.values.find("a")->second) == 1);
        REQUIRE(std::get<std::string>(p.values.find("b")->second) == "x");
        REQUIRE(std::get<bool>(p.values.find("c")->second) == false);
        REQUIRE(std::get<double>(p.values.find("d")->second) == 1);
        REQUIRE(std::get<double>(p.values.find("e")->second) == 42);

        REQUIRE(std::get<double>(p.features.find("f1")->second) == 1);
        REQUIRE(std::get<double>(p.features.find("f2")->second) == 1);

        REQUIRE(std::get<double>(p.values.find("com_a")->second) == 1);
        REQUIRE(std::get<std::string>(p.values.find("com_b")->second) == "y");
        REQUIRE(std::get<bool>(p.values.find("com_c")->second) == false);
        REQUIRE(std::get<double>(p.values.find("com_d")->second) == 1);
        REQUIRE(std::get<double>(p.features.find("com_f2")->second) == 3);
        REQUIRE(std::get<double>(p.features.find("com_f4")->second) == 2);

        REQUIRE(std::get<double>(p.values.find("p_com_a")->second) == 10);
        REQUIRE(std::get<std::string>(p.values.find("p_com_b")->second) == "xx");
        REQUIRE(std::get<bool>(p.values.find("p_com_c")->second) == true);
    }

    SECTION("product inherits")
    {
        const char* yaml = R"(
kind: printer
id: '*common*'
values:
  a: 1
  b: "x"
  c: true
features:
  f3: 1
variants:
  - id: '*common*@A'
    features:
      f1: 1
    values:
      d: 1
  - id: '*common*@B'
    features:
      f1: 2
    values:
      d: 2
---
kind: printer
id: '*commonPrusa*'
values:
  a: 2
variants:
  - id: '*commonPrusa*@A'
    values:
      f: 1
    features:
      f2: 1
  - id: '*commonPrusa*@B'
    values:
      f: 2
    features:
      f2: 2
---
kind: printer
id: 'Printer'
name: 'Printer'
inherits:
  - '*common*'
  - '*commonPrusa*'
values:
  c: false
  e: 42
features:
  f3: 2
)";

        IO::PresetLoader loader;
        try {
            loader.load_from_string(yaml);
        }
        catch (Yaml::ParseError& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
        auto eval = create_evaluator(loader, PresetKind::FdmPrinter);

        auto evals = eval.eval_preset({}, false);
        REQUIRE(evals.size() == 2 + 2 + 2 * 2);
        for (const auto& [name, expected_d, expected_f] : {
            std::make_tuple("Printer@A", 1, 1),
            std::make_tuple("Printer@B@A", 1, 2),
            std::make_tuple("Printer@A@B", 2, 1),
            std::make_tuple("Printer@B", 2, 2),
        }) {
            INFO("Preset: " << name);
            auto it = std::find_if(evals.begin(), evals.end(), [&name](const auto& p){ return p.id == name; });
            REQUIRE(it != evals.end());
            const auto& p = *it;
            REQUIRE(p.id == name);
            REQUIRE(p.conditions.size() == 0);
            REQUIRE(p.values.size() == 6);
            REQUIRE(std::get<double>(p.values.find("a")->second) == 2);
            REQUIRE(std::get<std::string>(p.values.find("b")->second) == "x");
            REQUIRE(std::get<bool>(p.values.find("c")->second) == false);
            REQUIRE(std::get<double>(p.values.find("d")->second) == expected_d);
            REQUIRE(std::get<double>(p.values.find("e")->second) == 42);
            REQUIRE(std::get<double>(p.values.find("f")->second) == expected_f);

            REQUIRE(std::get<double>(p.features.find("f1")->second) == expected_d);
            REQUIRE(std::get<double>(p.features.find("f2")->second) == expected_f);
            REQUIRE(std::get<double>(p.features.find("f3")->second) == 2);
        }
    }

    SECTION("name inheriting")
    {
        const char* yaml = R"(
kind: printer
id: '*common*'
name: '*common*'
---
kind: printer
inherits:
- '*common*'
)";
        IO::PresetLoader loader;
        try {
            loader.load_from_string(yaml);
        }
        catch (Yaml::ParseError& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
        auto eval = create_evaluator(loader, PresetKind::FdmPrinter);

        auto evals = eval.eval_preset({}, false);
        REQUIRE(evals.size() == 2);
        for (const auto& epc : evals) {
            REQUIRE(epc.name == "*common*");
        }
    }

    SECTION("failing root condition in super-class preset stops eval of sub-class preset")
    {
        const char* yaml = R"(
kind: printer
id: '*common*'
name: '*common*'
condition: 'a == 1'
values:
  x: 3
---
kind: printer
id: 'p1'
name: 'p1'
inherits:
- '*common*'
values:
  y: 3
)";
        IO::PresetLoader loader;
        try {
            loader.load_from_string(yaml);
        }
        catch (Yaml::ParseError& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
        auto eval = create_evaluator(loader, PresetKind::FdmPrinter);

        auto evals = eval.eval_preset({{{"a", 2.0}}}, false);
        REQUIRE(evals.size() == 0);
    }

    SECTION("failing root condition in super-super-class preset stops eval of sub-class preset")
    {
        const char* yaml = R"(
kind: printer
id: '*common*'
name: '*common*'
condition: 'a == 1'
values:
  x: 3
---
kind: printer
id: 'p1'
name: 'p1'
inherits:
- '*common*'
values:
  y: 3
---
kind: printer
id: 'p2'
name: 'p2'
inherits:
- 'p1'
values:
  y: 4
)";
        IO::PresetLoader loader;
        try {
            loader.load_from_string(yaml);
        }
        catch (Yaml::ParseError& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
        auto eval = create_evaluator(loader, PresetKind::FdmPrinter);

        auto evals = eval.eval_preset({{{"a", 2.0}}}, false);
        REQUIRE(evals.size() == 0);
    }

    SECTION("failing root condition in super-super-class preset can be overridden in top-level sub-class preset")
    {
        const char* yaml = R"(
kind: printer
id: '*common*'
name: '*common*'
condition: 'a == 1'
values:
  x: 3
---
kind: printer
id: 'p1'
name: 'p1'
inherits:
- '*common*'
values:
  y: 3
---
kind: printer
id: 'p2'
name: 'p2'
condition: 'a == 2'
inherits:
- 'p1'
values:
  y: 4
)";
        IO::PresetLoader loader;
        try {
            loader.load_from_string(yaml);
        }
        catch (Yaml::ParseError& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
        auto eval = create_evaluator(loader, PresetKind::FdmPrinter);

        auto evals = eval.eval_preset({{{"a", 2.0}}}, false);
        REQUIRE(evals.size() == 1);
    }

    SECTION("failing root condition in super-class preset stops eval of sub-class preset (with multi-inheritance)")
    {
        const char* yaml = R"(
kind: printer
id: '*common*'
name: '*common*'
condition: 'a == 1'
values:
  x: 3
---
kind: printer
id: '*common2*'
name: '*common2*'
values:
  z: 3
---
kind: printer
id: 'p1'
name: 'p1'
inherits:
- '*common*'
- '*common2*'
values:
  y: 3
)";
        IO::PresetLoader loader;
        try {
            loader.load_from_string(yaml);
        }
        catch (Yaml::ParseError& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
        auto eval = create_evaluator(loader, PresetKind::FdmPrinter);

        auto evals = eval.eval_preset({{{"a", 2.0}}}, false);
        REQUIRE(evals.size() == 1);
        REQUIRE(evals[0].name == "*common2*");
    }

    SECTION("failing non-root condition in super-class preset will not stop eval of sub-class preset")
    {
        const char* yaml = R"(
kind: printer
id: '*common*'
name: '*common*'
variants:
- condition: 'a == 1'
  values:
    x: 3
---
kind: printer
id: 'p1'
name: 'p1'
inherits:
- '*common*'
values:
  y: 3
)";
        IO::PresetLoader loader;
        try {
            loader.load_from_string(yaml);
        }
        catch (Yaml::ParseError& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
        auto eval = create_evaluator(loader, PresetKind::FdmPrinter);

        auto evals = eval.eval_preset({{{"a", 2.0}}}, false);
        REQUIRE(evals.size() == 2);
        REQUIRE(evals[0].values.find("x") == evals[0].values.end());
        REQUIRE(evals[1].values.find("x") == evals[1].values.end());
        REQUIRE(evals[1].values.at("y") == PresetValue{3.0});
    }

    SECTION("root condition of super-class preset can be overridden in sub-class preset")
    {
        const char* yaml = R"(
kind: printer
id: '*common*'
name: '*common*'
condition: 'a == 1'
values:
  x: 3
---
kind: printer
id: 'p1'
name: 'p1'
condition: 'a == 2'
inherits:
- '*common*'
values:
  y: 3
)";
        IO::PresetLoader loader;
        try {
            loader.load_from_string(yaml);
        }
        catch (Yaml::ParseError& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
        auto eval = create_evaluator(loader, PresetKind::FdmPrinter);

        auto evals = eval.eval_preset({{{"a", 2.0}}}, false);
        REQUIRE(evals.size() == 1);
    }

    SECTION("custom parameters")
    {
        const char* yaml = R"(
kind: printer
id: '*common*'
values:
  custom_parameters_printer: '{"key1": 1, "key2": true, "key3": null, "key4": null}'
features:
  f1: 1
  f2: 1
---
kind: printer
id: 'Printer'
name: 'Printer'
inherits:
  - '*common*'
values:
  custom_parameters_printer: '{"key1": 2, "key3": "ahoj", "key5": null}'
features:
  f2: 2
)"; 

        IO::PresetLoader loader;
        try {
            loader.load_from_string(yaml);
        }
        catch (Yaml::ParseError& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
        auto eval = create_evaluator(loader, PresetKind::FdmPrinter);

        auto evals = eval.eval_preset({}, false);
        auto it = std::find_if(evals.begin(), evals.end(), [](const auto& p){ return p.name == "Printer"; });
        const auto& cp = std::get<std::string>(it->values.at("custom_parameters_printer"));
        auto json = nlohmann::json::parse(cp);
        REQUIRE(json.at("key1").get<int>() == 2);
        REQUIRE(json.at("key2").get<bool>() == true);
        REQUIRE(json.at("key3").get<std::string>() == "ahoj");
        REQUIRE(json.at("key4").is_null());
        REQUIRE(json.at("key5").is_null());
    }

    SECTION("condition gathereing")
    {
        const char* yaml = R"(
kind: printer
id: '*common*'
values:
  a: 1
features:
  a: 1
variants:
  - condition: nozzle_diameter == 0.4
    values:
      b: 1
    features:
      b: 1
  - condition: nozzle_diameter == 0.3
    values:
      b: 2
    features:
      b: 2
  - condition: nozzle_diameter == 0.5
    values:
      b: 3
    features:
      b: 3
---
kind: printer
id: 'Printer'
name: 'Printer'
inherits:
  - '*common*'
values:
  c: 1
features:
  c: 1
variants:
  - condition: nozzle_high_flow
    values:
      d: 1
    features:
      d: 1
  - values:
      d: 2
    features:
      d: 2
)";

        IO::PresetLoader loader;
        try {
            loader.load_from_string(yaml);
        }
        catch (Yaml::ParseError& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
        auto eval = create_evaluator(loader, PresetKind::FdmPrinter);

        struct Input
        {
            double nozzle_diameter;
            bool nozzle_high_flow;
        };

        struct Output
        {
            double a,b,c,d;
            size_t cond_count, neg_cond_count;
        };

        using Parameters = std::vector<std::tuple<Input, Output>>;
        const auto params = Parameters{
            {Input{0.4, true}, Output{1, 1, 1, 1, 2, 0}},
            {Input{0.4, false}, Output{1, 1, 1, 2, 1, 1}},
            {Input{0.3, true}, Output{1, 2, 1, 1, 2, 1}},
            {Input{0.3, false}, Output{1, 2, 1, 2, 1, 2}},
            {Input{0.5, true}, Output{1, 3, 1, 1, 2, 2}},
            {Input{0.5, false}, Output{1, 3, 1, 2, 1, 3}},
        };
        for (auto& [input, expected_output] : params) {
            INFO("nozzle_diameter: " << input.nozzle_diameter);
            INFO("nozzle_high_flow: " << input.nozzle_high_flow);
            auto evals = eval.eval_preset({{{"nozzle_diameter", input.nozzle_diameter}, {"nozzle_high_flow", input.nozzle_high_flow}}}, true);
            auto it = std::find_if(evals.begin(), evals.end(), [](const auto& p){ return p.name == "Printer"; });

            REQUIRE(it != evals.end());

            REQUIRE(std::get<double>(it->values.at("a")) == expected_output.a);
            REQUIRE(std::get<double>(it->values.at("b")) == expected_output.b);
            REQUIRE(std::get<double>(it->values.at("c")) == expected_output.c);
            REQUIRE(std::get<double>(it->values.at("d")) == expected_output.d);

            REQUIRE(std::get<double>(it->features.at("a")) == expected_output.a);
            REQUIRE(std::get<double>(it->features.at("b")) == expected_output.b);
            REQUIRE(std::get<double>(it->features.at("c")) == expected_output.c);
            REQUIRE(std::get<double>(it->features.at("d")) == expected_output.d);

            REQUIRE(it->conditions.size() == expected_output.cond_count);
            REQUIRE(it->negative_conditions.size() == expected_output.neg_cond_count);
        }
    }
}
