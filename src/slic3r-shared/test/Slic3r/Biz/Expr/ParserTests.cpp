#include <catch2/catch_test_macros.hpp>
#include <Slic3r/Biz/Expr/Parser.hpp>
#include <boost/variant/get.hpp>
#include <iostream>

TEST_CASE("Parser tests")
{
    using namespace Slic3r::Domain::Expr;
    using namespace Slic3r::Biz::Expr;

    ExprAst expr;
    Parser parser;

    SECTION("constants")
    {
        expr = parser.parse("3.0");
        REQUIRE(boost::get<double>(expr) == 3.0f);
        expr = parser.parse("\"3.0\"");
        REQUIRE(boost::get<std::string>(expr) == "3.0");
        expr = parser.parse(R"("\"hello\"")");
        REQUIRE(boost::get<std::string>(expr) == R"("hello")");
        expr = parser.parse("true");
        REQUIRE(boost::get<bool>(expr) == true);
        expr = parser.parse("false");
        REQUIRE(boost::get<bool>(expr) == false);
        expr = parser.parse("/a+\\//");
        REQUIRE(boost::get<RegEx>(expr).source() == "a+/");
    }

    SECTION("variable")
    {
        expr = parser.parse("printer.base_model");
        REQUIRE(boost::get<VarRef>(expr).name == "printer.base_model");
    }

    SECTION("arithmetic expression")
    {
        expr = parser.parse("2 + 3 * 4");
        {
            const Binary root = boost::get<Binary>(expr);
            REQUIRE(root.op == BinaryOp::Add);
            REQUIRE(boost::get<double>(root.left) == 2);
            const Binary node = boost::get<Binary>(root.right);
            REQUIRE(node.op == BinaryOp::Multiply);
            REQUIRE(boost::get<double>(node.left) == 3);
            REQUIRE(boost::get<double>(node.right) == 4);
        }

        expr = parser.parse("2 / 3 - 4");
        {
            const Binary root = boost::get<Binary>(expr);
            REQUIRE(root.op == BinaryOp::Subtract);
            REQUIRE(boost::get<double>(root.right) == 4);
            const Binary node = boost::get<Binary>(root.left);
            REQUIRE(node.op == BinaryOp::Divide);
            REQUIRE(boost::get<double>(node.left) == 2);
            REQUIRE(boost::get<double>(node.right) == 3);
        }
    }

    SECTION("function call")
    {
        expr = parser.parse("func(1, \"a\", false)");
        {
            const FuncCall root = boost::get<FuncCall>(expr);
            REQUIRE(root.name == "func");
            REQUIRE(root.args.size() == 3);
            REQUIRE(boost::get<double>(root.args[0]) == 1.0f);
            REQUIRE(boost::get<std::string>(root.args[1]) == "a");
            REQUIRE(boost::get<bool>(root.args[2]) == false);
        }
    }

    SECTION("unary ops")
    {
        expr = parser.parse("+3");
        {
            const Unary root = boost::get<Unary>(expr);
            REQUIRE(root.op == UnaryOp::Plus);
            REQUIRE(boost::get<double>(root.expr) == 3);
        }

        for (std::string_view source : {"not true", "!true"}) {
            expr = parser.parse(source);
            {
                const Unary root = boost::get<Unary>(expr);
                REQUIRE(root.op == UnaryOp::Not);
                REQUIRE(boost::get<bool>(root.expr) == true);
            }
        }
    }

    SECTION("simple logical ops")
    {
        expr = parser.parse("name == \"x\"");
        {
            const Binary root = boost::get<Binary>(expr);
            REQUIRE(root.op == BinaryOp::Eq);
            const VarRef node = boost::get<VarRef>(root.left);
            REQUIRE(node.name == "name");
            REQUIRE(boost::get<std::string>(root.right) == "x");
        }

        for (std::string_view source : {"x && y", "x and y"}) {
            expr = parser.parse(source);
            {
                const auto root = boost::get<Binary>(expr);
                REQUIRE(root.op == BinaryOp::And);
                REQUIRE(boost::get<VarRef>(root.left).name == "x");
                REQUIRE(boost::get<VarRef>(root.right).name == "y");
            }
        }

        for (std::string_view source : {"x || y", "x or y"}) {
            expr = parser.parse(source);
            {
                const auto root = boost::get<Binary>(expr);
                REQUIRE(root.op == BinaryOp::Or);
                REQUIRE(boost::get<VarRef>(root.left).name == "x");
                REQUIRE(boost::get<VarRef>(root.right).name == "y");
            }
        }
    }

    SECTION("regex match")
    {
        expr = parser.parse(R"(name =~ /a+\./)");
        {
            auto root = boost::get<Binary>(expr);
            REQUIRE(root.op == BinaryOp::RegExMatch);

            REQUIRE(boost::get<VarRef>(root.left).name == "name");
            REQUIRE(boost::get<RegEx>(root.right).source() == "a+\\.");
        }

        expr = parser.parse(R"(name !~ /a+\./)");
        {
            auto root = boost::get<Unary>(expr);
            REQUIRE(root.op == UnaryOp::Not);
            auto child = boost::get<Binary>(root.expr);
            REQUIRE(child.op == BinaryOp::RegExMatch);

            REQUIRE(boost::get<VarRef>(child.left).name == "name");
            REQUIRE(boost::get<RegEx>(child.right).source() == "a+\\.");
        }
    }

    SECTION("complex expressions")
    {

        for (auto [source, op] : {
            std::make_tuple("val < 3 + sum(x, 4)", BinaryOp::Lt),
            std::make_tuple("val > 3 + sum(x, 4)", BinaryOp::Gt),
            std::make_tuple("val <= 3 + sum(x, 4)", BinaryOp::LtEq),
            std::make_tuple("val >= 3 + sum(x, 4)", BinaryOp::GtEq),
            std::make_tuple("val == 3 + sum(x, 4)", BinaryOp::Eq),
            std::make_tuple("val != 3 + sum(x, 4)", BinaryOp::NotEq),
        }) {
            expr = parser.parse(source);
            {
                const auto root = boost::get<Binary>(expr);
                REQUIRE(root.op == op);
                REQUIRE(boost::get<VarRef>(root.left).name == "val");
                const auto node1 = boost::get<Binary>(root.right);
                REQUIRE(node1.op == BinaryOp::Add);
                REQUIRE(boost::get<double>(node1.left) == 3);
                const auto node2 = boost::get<FuncCall>(node1.right);
                REQUIRE(node2.name == "sum");
                REQUIRE(node2.args.size() == 2);
                REQUIRE(boost::get<VarRef>(node2.args[0]).name == "x");
                REQUIRE(boost::get<double>(node2.args[1]) == 4);
            }
        }

        expr = parser.parse("a + 3 < val and val < b * 6");
        {
            const auto root = boost::get<Binary>(expr);
            REQUIRE(root.op == BinaryOp::And);

            const auto node_left = boost::get<Binary>(root.left);
            REQUIRE(node_left.op == BinaryOp::Lt);

            const auto node_left_left = boost::get<Binary>(node_left.left);
            REQUIRE(node_left_left.op == BinaryOp::Add);
            REQUIRE(boost::get<VarRef>(node_left_left.left).name == "a");
            REQUIRE(boost::get<double>(node_left_left.right) == 3);

            REQUIRE(boost::get<VarRef>(node_left.right).name == "val");

            const auto node_right = boost::get<Binary>(root.right);
            REQUIRE(node_right.op == BinaryOp::Lt);
            REQUIRE(boost::get<VarRef>(node_right.left).name == "val");
            const auto node_right_right = boost::get<Binary>(node_right.right);
            REQUIRE(node_right_right.op == BinaryOp::Multiply);
            REQUIRE(boost::get<VarRef>(node_right_right.left).name == "b");
            REQUIRE(boost::get<double>(node_right_right.right) == 6);
        }

        for (std::string_view source : {"not a or b", "! a || b"}) {
            expr = parser.parse(source);
            {
                const auto root = boost::get<Binary>(expr);
                REQUIRE(root.op == BinaryOp::Or);
                const auto left = boost::get<Unary>(root.left);
                REQUIRE(left.op == UnaryOp::Not);
                REQUIRE(boost::get<VarRef>(left.expr).name == "a");
                REQUIRE(boost::get<VarRef>(root.right).name == "b");
            }
        }

        for (std::string_view source : {"not (a or b)", "! (a || b)"}) {
            expr = parser.parse(source);
            {
                const auto root = boost::get<Unary>(expr);
                REQUIRE(root.op == UnaryOp::Not);
                const auto node = boost::get<Binary>(root.expr);
                REQUIRE(node.op == BinaryOp::Or);
                REQUIRE(boost::get<VarRef>(node.left).name == "a");
                REQUIRE(boost::get<VarRef>(node.right).name == "b");
            }
        }

        expr = parser.parse("printer.base_model=~/(COREONE|MK4)/ and (tool.nozzle_diameter == 0.4 or tool.nozzle_diameter == 0.6) and ! (tool.nozzle_diameter == 0.6 and tool.nozzle_high_flow)");
        {
            const auto root = boost::get<Binary>(expr);
            REQUIRE(root.op == BinaryOp::And);
            const auto left = boost::get<Binary>(root.left);
            REQUIRE(left.op == BinaryOp::And);
            const auto right = boost::get<Unary>(root.right);
            REQUIRE(right.op == UnaryOp::Not);
            const auto left_left = boost::get<Binary>(left.left);
            REQUIRE(left_left.op == BinaryOp::RegExMatch);
            const auto left_right = boost::get<Binary>(left.right);
            REQUIRE(left_right.op == BinaryOp::Or);
        }
    }

    SECTION("error reporting")
    {
        REQUIRE_THROWS_AS(expr = parser.parse("x >="), ParseError);
        REQUIRE_THROWS_AS(expr = parser.parse("<= a"), ParseError);
        REQUIRE_THROWS_AS(expr = parser.parse("a ==\""), ParseError);
#if 0
        try {
            expr = parser.parse("\tx >=");
        } catch (ParseError &e) {
            std::cout << e.what() << std::endl;
        }
#endif
    }
}
