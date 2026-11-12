#pragma once

#include <regex>
#include <vector>
#include <boost/variant/recursive_variant.hpp>
#include "Slic3r/Domain/Expr/RegEx.hpp"

#include "Slic3r/Assert.hpp"

/**
 * @brief Data structures for holding parsed expression if form of AST (abstract syntax tree).
 *
 * AST is output of expression parsing from string and serve as input into the expression
 * evaluation. The main class holding expression is `ExprAst`
 */
namespace Slic3r::Domain::Expr {

struct Binary;
struct Unary;
struct FuncCall;
struct VarRef;

using ExprAst = boost::variant<
    bool,
    double,
    std::string,
    RegEx,
    boost::recursive_wrapper<Binary>,
    boost::recursive_wrapper<Unary>,
    boost::recursive_wrapper<FuncCall>,
    boost::recursive_wrapper<VarRef>
>;

bool equals_to(const ExprAst& lhs, const ExprAst& rhs);

std::ostream& operator<<(std::ostream& os, const ExprAst& v);
std::string to_string(const ExprAst& expr);

enum class BinaryOp
{
    Add, Subtract, Multiply, Divide,
    Eq, NotEq, RegExMatch,
    Lt, LtEq, Gt, GtEq,
    And, Or,
};

struct Binary
{
    BinaryOp op{BinaryOp::Add};
    ExprAst left;
    ExprAst right;

    Binary() = default;
    Binary(const Binary&) = default;
    Binary(Binary&&) = default;

    Binary& operator=(Binary&&) = default;
    Binary& operator=(const Binary&) = default;

    Binary(BinaryOp op, ExprAst left, ExprAst right)
        : op(op), left(std::move(left)), right(std::move(right))
    {}

};

enum class UnaryOp
{
    Plus, Minus, Not
};

struct Unary
{
    UnaryOp op;
    ExprAst expr;

    Unary() = default;
    Unary(const Unary&) = default;
    Unary(Unary&&) = default;

    Unary& operator=(const Unary&) = default;
    Unary& operator=(Unary&&) = default;

    Unary(UnaryOp op, ExprAst expr)
        : op(op), expr(std::move(expr))
    {}

};

struct FuncCall
{
    std::string name;
    std::vector<ExprAst> args;

    FuncCall() = default;
    FuncCall(const FuncCall&) = default;
    FuncCall(FuncCall&&) = default;

    FuncCall& operator=(const FuncCall&) = default;
    FuncCall& operator=(FuncCall&&) = default;

    FuncCall(std::string name, std::vector<ExprAst> args)
        : name(std::move(name)), args(std::move(args))
    {}

};

struct VarRef
{
    std::string name;

    VarRef() = default;
    VarRef(const VarRef&) = default;
    VarRef(VarRef&&) = default;

    VarRef& operator=(const VarRef&) = default;
    VarRef& operator=(VarRef&&) = default;

    explicit VarRef(std::string name)
        : name(std::move(name))
    {}

};

struct ExprPrinter : boost::static_visitor<std::ostream&>
{
    explicit ExprPrinter(std::ostream& os) : os(os) {}

    std::ostream& operator()(bool val);
    std::ostream& operator()(double val);
    std::ostream& operator()(const std::string& val);
    std::ostream& operator()(const RegEx& val);
    std::ostream& operator()(const Binary& val);
    std::ostream& operator()(const Unary& val);
    std::ostream& operator()(const FuncCall& val);
    std::ostream& operator()(const VarRef& val);

protected:
    std::ostream& os;
};


} // namespace Slic3r::Domain::Expr
