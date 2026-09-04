#include "Slic3r/Domain/Expr/ExprAst.hpp"
#include <sstream>
#include <boost/variant/get.hpp>

namespace Slic3r::Domain::Expr {

namespace {

class IsEqualVisitor : public boost::static_visitor<bool>
{
    const ExprAst& other;

public:
    explicit IsEqualVisitor(const ExprAst& other) : other(other) {}

    template <typename T>
    bool operator()(const T& lhs_val) const
    {
        if (const T* rhs_val = boost::get<T>(&other)) {
            return lhs_val == *rhs_val;
        }
        return false;
    }

    bool operator()(const Binary& lhs) const
    {
        if (const Binary* rhs = boost::get<Binary>(&other)) {
            return lhs.op == rhs->op
                && equals_to(lhs.left, rhs->left)
                && equals_to(lhs.right, rhs->right);
        }
        return false;
    }

    bool operator()(const Unary& lhs) const
    {
        if (const Unary* rhs = boost::get<Unary>(&other)) {
            return lhs.op == rhs->op && equals_to(lhs.expr, rhs->expr);
        }
        return false;
    }

    bool operator()(const FuncCall& lhs) const
    {
        if (const FuncCall* rhs = boost::get<FuncCall>(&other)) {
            if (lhs.name != rhs->name || lhs.args.size() != rhs->args.size())
                return false;
            for (size_t i = 0; i < lhs.args.size(); ++i) {
                if (!equals_to(lhs.args[i], rhs->args[i]))
                    return false;
            }
            return true;
        }
        return false;
    }

    bool operator()(const RegEx& lhs) const
    {
        if (const RegEx* rhs = boost::get<RegEx>(&other)) {
            return lhs == *rhs;
        }
        return false;
    }

    bool operator()(const VarRef& lhs) const
    {
        if (const VarRef* rhs = boost::get<VarRef>(&other)) {
            return lhs.name == rhs->name;
        }
        return false;
    }
};

} // namespace

bool equals_to(const ExprAst& lhs, const ExprAst& rhs)
{
    return boost::apply_visitor(IsEqualVisitor(rhs), lhs);
}

std::ostream& ExprPrinter::operator()(bool val)
{
    os << val;
    return os;
}

std::ostream& ExprPrinter::operator()(double val)
{
    os << val;
    return os;
}

std::ostream& ExprPrinter::operator()(const std::string& val)
{
    os << "\"" << val << "\"";
    return os;
}

std::ostream& ExprPrinter::operator()(const RegEx& val)
{
    os << "/" << val.source() << "/";
    return os;
}

std::ostream& ExprPrinter::operator()(const Binary& val)
{
    std::string op_name;
    switch (val.op) {
    case BinaryOp::Add:
        op_name = "+";
        break;

    case BinaryOp::Subtract:
        op_name = "-";
        break;

    case BinaryOp::Multiply:
        op_name = "*";
        break;

    case BinaryOp::Divide:
        op_name = "/";
        break;

    case BinaryOp::Eq:
        op_name = "==";
        break;

    case BinaryOp::NotEq:
        op_name = "!=";
        break;

    case BinaryOp::RegExMatch:
        op_name = "=~";
        break;

    case BinaryOp::Lt:
        op_name = "<";
        break;

    case BinaryOp::LtEq:
        op_name = "<=";
        break;

    case BinaryOp::Gt:
        op_name = ">";
        break;

    case BinaryOp::GtEq:
        op_name = ">=";
        break;

    case BinaryOp::And:
        op_name = "and";
        break;

    case BinaryOp::Or:
        op_name = "or";
        break;
    }

    os << "(";
    boost::apply_visitor(*this, val.left);
    os << " " << op_name << " ";
    boost::apply_visitor(*this, val.right);
    os << ")";

    return os;
}

std::ostream& ExprPrinter::operator()(const Unary& val)
{
    std::string op_name;
    switch (val.op) {
    case UnaryOp::Not:
        op_name = "not";
        break;

    case UnaryOp::Plus:
        op_name = "+";
        break;

    case UnaryOp::Minus:
        op_name = "-";
        break;
    }

    os << op_name << " ";
    boost::apply_visitor(*this, val.expr);

    return os;
}

std::ostream& ExprPrinter::operator()(const FuncCall& val)
{
    os << val.name << "(";
    bool first = true;
    for (const auto& arg : val.args) {
        if (first)
            first = false;
        else
            os << ", ";
        boost::apply_visitor(*this, arg);
    }

    os << ")";
    return os;
}

std::ostream& ExprPrinter::operator()(const VarRef& val)
{
    os << val.name;
    return os;
}

std::ostream& operator<<(std::ostream& os, const ExprAst& v)
{
    ExprPrinter printer(os);
    boost::apply_visitor(printer, v);
    return os;
}

std::string to_string(const ExprAst& v)
{
    std::ostringstream os;
    os << v;
    return os.str();
}

} // namespace Slic3r::Domain::Expr
