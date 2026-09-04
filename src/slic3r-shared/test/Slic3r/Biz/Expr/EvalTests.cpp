#include <iostream>

#include <catch2/catch_test_macros.hpp>
#include <boost/variant/get.hpp>

#include "Slic3r/Biz/Expr/Parser.hpp"
#include "Slic3r/Biz/Expr/Eval.hpp"

#include <cmath>

using namespace Slic3r::Biz::Expr;
using namespace Slic3r::Domain::Expr;

TEST_CASE("Expression eval")
{
    Parser p;
    Eval e;
    Value v;

    const auto eval = [&](const char* source, const ValueMap& extra_vars = {}) {
        return e.eval(p.parse(source), extra_vars);
    };

    SECTION("Constant")
    {
        v = eval("123456789");
        double w = boost::get<double>(v);
        REQUIRE(w == 123456789);
    }

    SECTION("Arithmetic ops")
    {
        v = eval("2 + 3 * 4");
        REQUIRE(boost::get<double>(v) == 14);
        v = eval("2 - 3 / 4");
        REQUIRE(boost::get<double>(v) == 1.25f);
    }

    e.set_var("number", "321321890");
    e.set_var("not_number", "321321890x");
    SECTION("Regex ops")
    {
        try {
            v = eval("/^\\d+$/");
            REQUIRE(boost::get<RegEx>(v).source() == "^\\d+$");

            v = eval("number =~ /^\\d+$/");
            REQUIRE(boost::get<bool>(v) == true);

            v = eval("not_number =~ /\\d+/");
            REQUIRE(boost::get<bool>(v) == false);

            v = eval("not_number !~ /\\d+/");
            REQUIRE(boost::get<bool>(v) == true);

            v = eval("number !~ /\\d+/");
            REQUIRE(boost::get<bool>(v) == false);
        } catch (const std::exception& e) {
            std::cerr << e.what() << std::endl;
            FAIL(e.what());
        }
    }

    SECTION("Logic ops")
    {
        e.set_var("name", "abc");
        v = eval("name == \"abc\"");
        REQUIRE(boost::get<bool>(v) == true);

        v = eval("name == \"abcd\"");
        REQUIRE(boost::get<bool>(v) == false);

        e.set_var("name", "abcd");
        v = eval("name == \"abc\"");
        REQUIRE(boost::get<bool>(v) == false);
        e.set_var("printer.tool_count", double{1});
        v = eval("printer.tool_count == 1");
        REQUIRE(boost::get<bool>(v) == true);
        e.set_var("tool.nozzle_diameter", 0.4);
        e.set_var("tool.high_flow_nozzle", true);
        v = eval("tool.nozzle_diameter == 0.4");
        REQUIRE(boost::get<bool>(v) == true);
        v = eval("tool.nozzle_diameter <= 0.4");
        REQUIRE(boost::get<bool>(v) == true);
        v = eval("tool.nozzle_diameter <= 0.3");
        REQUIRE(boost::get<bool>(v) == false);
        v = eval("tool.nozzle_diameter > 0.4");
        REQUIRE(boost::get<bool>(v) == false);
        v = eval("tool.nozzle_diameter >= 0.4");
        REQUIRE(boost::get<bool>(v) == true);
        v = eval("tool.nozzle_diameter >= 0.41");
        REQUIRE(boost::get<bool>(v) == false);
        v = eval("tool.nozzle_diameter != 0.4");
        REQUIRE(boost::get<bool>(v) == false);

        v = eval("tool.nozzle_diameter <= 0.4 and tool.high_flow_nozzle");
        REQUIRE(boost::get<bool>(v) == true);
        v = eval("tool.nozzle_diameter <= 0.3 or tool.high_flow_nozzle");
        REQUIRE(boost::get<bool>(v) == true);
        v = eval("not (tool.nozzle_diameter < 0.4 or tool.high_flow_nozzle)");
        REQUIRE(boost::get<bool>(v) == false);
    }

    std::function min = [](double x, double y) -> double { return std::min(x, y); };
    e.reg_function("min", min);

    SECTION("Function call")
    {
        v = eval("min(42, 10)");
        REQUIRE(boost::get<double>(v) == 10);
        v = eval("min(42, 10) == 10");
        REQUIRE(boost::get<bool>(v) == true);
    }

}
