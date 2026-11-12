#pragma once

#include "Slic3r/Domain/Expr/ExprAst.hpp"

namespace Slic3r::Biz::Expr {
    Domain::Expr::ExprAst simplify(const Domain::Expr::ExprAst& expr);
}
