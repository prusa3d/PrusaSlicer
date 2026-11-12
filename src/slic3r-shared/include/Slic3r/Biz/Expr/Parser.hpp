#pragma once

#include <string_view>
#include "Slic3r/Domain/Expr/ExprAst.hpp"

namespace Slic3r::Biz::Expr {

class ParseError : public std::runtime_error
{
public:
    explicit ParseError(const std::string& message)
        : std::runtime_error(message)
    {}
};

class Parser
{
public:
    Domain::Expr::ExprAst parse(std::string_view source);

};

}
