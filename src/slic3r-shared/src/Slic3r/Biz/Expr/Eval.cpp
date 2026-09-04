#include "Slic3r/Biz/Expr/Eval.hpp"

#include <sstream>

#include <boost/variant/static_visitor.hpp>
#include <boost/variant/apply_visitor.hpp>
#include <boost/variant/get.hpp>
#include <fmt/format.h>

#include <Slic3r/Log.hpp>

namespace Slic3r::Biz::Expr {

namespace {

using namespace Domain::Expr;

template <typename T>
T safe_get(const Value& v, const char* op_name)
{
    if (v.type() != typeid(T)) {
        std::ostringstream os;
        os << v;

        throw EvalError(
            fmt::format(
                "Operation {} expecting value of type {} but instead have value {} of type {}",
                op_name,
                v.type().name(),
                os.str(),
                typeid(T).name()
            )
        );
    }
    return boost::get<T>(v);
}

struct Evaluator : boost::static_visitor<Value>
{
    Evaluator(const ValueMap& base_vars, const ValueMap& extra_vars, const FuncMap& funcs)
        : m_base_vars(base_vars), m_extra_vars(extra_vars), m_functions(funcs)
    {}

    Value operator()(const std::string& v) const
    {
        return v;
    }

    Value operator()(const RegEx& v) const
    {
        return v;
    }

    Value operator()(double v) const
    {
        return v;
    }

    Value operator()(bool v) const
    {
        return v;
    }

    Value operator()(const Binary& v) const
    {
        Value lhs = boost::apply_visitor(*this, v.left);

        // lazy evaluation: making this function instead of directly evaluating it here
        // is an optimization for cases like (true || rhs) and (false && rhs) where the rhs
        // evaluation can be skipped.
        auto eval_rhs = [&]() {
            return boost::apply_visitor(*this, v.right);
        };

        switch (v.op) {
        case BinaryOp::Add:
            return safe_get<double>(lhs, "+") + safe_get<double>(eval_rhs(), "+");
        case BinaryOp::Subtract:
            return safe_get<double>(lhs, "-") - safe_get<double>(eval_rhs(), "-");
        case BinaryOp::Multiply:
            return safe_get<double>(lhs, "*") * safe_get<double>(eval_rhs(), "*");
        case BinaryOp::Divide:
            return safe_get<double>(lhs, "/") / safe_get<double>(eval_rhs(), "/");

        case BinaryOp::Eq:
            return lhs == eval_rhs();
        case BinaryOp::NotEq:
            return lhs != eval_rhs();
        case BinaryOp::Lt:
            return lhs < eval_rhs();
        case BinaryOp::Gt:
            return lhs > eval_rhs();
        case BinaryOp::LtEq:
            return lhs <= eval_rhs();
        case BinaryOp::GtEq:
            return lhs >= eval_rhs();

        case BinaryOp::And:
            return safe_get<bool>(lhs, "&&") && safe_get<bool>(eval_rhs(), "&&");
        case BinaryOp::Or:
            return safe_get<bool>(lhs, "||") || safe_get<bool>(eval_rhs(), "||");
        case BinaryOp::RegExMatch:
            return safe_get<RegEx>(eval_rhs(), "=~").match(safe_get<std::string>(lhs, "=~"));
        }

        UNREACHABLE("Unknown binary op");
    }

    Value operator()(const Unary& v) const
    {
        Value expr = boost::apply_visitor(*this, v.expr);
        // TODO: type mismatched error handling
        switch (v.op) {
        case UnaryOp::Not:
            return !safe_get<bool>(expr, "!");
        case UnaryOp::Plus:
            return expr;
        case UnaryOp::Minus:
            return -safe_get<double>(expr, "unary -");
        }

        UNREACHABLE("Unknown unary op");
    }

    Value operator()(const VarRef& v) const
    {
        auto it = m_extra_vars.find(v.name);
        if (it != m_extra_vars.end())
            return it->second;
        it = m_base_vars.find(v.name);
        if (it == m_base_vars.end())
            throw EvalError(fmt::format("Unknown variable '{}'", v.name));
        return it->second;
    }

    Value operator()(const FuncCall& v) const
    {
        auto it = m_functions.find(v.name);
        if (it == m_functions.end())
            throw EvalError(fmt::format("Unknown function '{}'", v.name));
        ValueList args;
        for (const auto& arg : v.args)
            args.emplace_back(boost::apply_visitor(*this, arg));
        return it->second(args);
    }

private:
    const ValueMap& m_base_vars;
    const ValueMap& m_extra_vars;
    const FuncMap& m_functions;
};

} // anonymous namespace

std::ostream& operator<<(std::ostream& os, const Value& v)
{
    ExprPrinter printer(os);
    boost::apply_visitor(printer, v);
    return os;
}

std::string to_string(const Value& v)
{
    std::ostringstream os;
    os << v;
    return os.str();
}

Value Eval::eval(const Expr& expr, const ValueMap& extra_vars) const
{
    Evaluator evaluator(m_vars, extra_vars, m_functions);
    auto ret = boost::apply_visitor(evaluator, expr);

    if (m_debug_output != nullptr) {
        m_debug_output(fmt::format("Evaluating expression: '{}'  with result {}", to_string(expr), to_string(ret)));
        m_debug_output(fmt::format("Variables:"));
        for (const auto& [k, v] : m_vars)
            m_debug_output(fmt::format("\t{}: {}", k, to_string(v)));
        for (const auto& [k, v] : extra_vars)
            m_debug_output(fmt::format("\t{} (extra): {}", k, to_string(v)));
    }

    return ret;
}

} // namespace Slic3r::Biz::Expr
