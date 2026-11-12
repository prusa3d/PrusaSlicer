#pragma once

#include <functional>
#include <map>
#include <vector>
#include <boost/variant/variant.hpp>
#include <boost/variant/get.hpp>

#include "Slic3r/Assert.hpp"
#include "Slic3r/Domain/Expr/ExprAst.hpp"

namespace Slic3r::Biz::Expr {

using Value     = boost::variant<bool, double, std::string, Domain::Expr::RegEx>;
using ValueList = std::vector<Value>;
using Func      = std::function<Value(const ValueList&)>;
using ValueMap  = std::map<std::string, Value>;
using FuncMap   = std::map<std::string, Func>;

namespace Details {

template <typename T>
void verify_type(const Value& v)
{
    ASSERT(v.type() == typeid(T));
}

template <typename Ret, typename... Args, size_t... I>
Value call(const std::function<Ret(Args...)>& func, const ValueList& args, std::index_sequence<I...>)
{
    (verify_type<Args>(args[I]), ...);
    return func(boost::get<Args>(args[I])...);
}

} // namespace Details

template <typename Ret, typename... Args>
Func make_function(const std::function<Ret(Args...)>& func)
{
    return [func](const ValueList& args) {
        ASSERT(args.size() == sizeof...(Args), "Wrong number of arguments");
        return Details::call(func, args, std::make_index_sequence<sizeof...(Args)>());
    };
}

class EvalError : public std::runtime_error
{
public:
    explicit EvalError(const std::string& msg) : std::runtime_error(msg) {}
};

class Eval
{
public:
    using Expr = Domain::Expr::ExprAst;
    using Logger = std::function<void(std::string_view message)>;

    void set_var(const char* name, Value value)
    {
        m_vars[name] = std::move(value);
    }

    void set_var(const char* name, const char* value)
    {
        m_vars[name] = std::string(value);
    }

    void set_vars(const ValueMap& vars)
    {
        for (const auto& [k, v] : vars)
            m_vars[k] = v;
    }

    template <typename Ref, typename... Args>
    void reg_function(const char* name, const std::function<Ref(Args...)>& func)
    {
        m_functions[name] = make_function(func);
    }

    Value eval(const Expr& expr, const ValueMap& extra_vars = {}) const;

    bool debug_output_enabled() const
    {
        return m_debug_output != nullptr;
    }

    void set_debug_output(Logger logger)
    {
        m_debug_output = std::move(logger);
    }

private:
    FuncMap m_functions;
    ValueMap m_vars;
    Logger m_debug_output{nullptr};
};

std::ostream& operator<<(std::ostream& os, const Value& v);
std::string to_string(const Value& v);

} // namespace Slic3r::Biz::Expr
