#pragma once
#include "Slic3r/Domain/Expr/ExprAst.hpp"

namespace Slic3r::Domain::Preset {

struct SourceLocation
{
    std::string file;
    size_t line{0};
    size_t column{0};

    [[nodiscard]] std::string to_string() const
    {
        return file + ": " + std::to_string(line) + ":" + std::to_string(column);
    }
};

template <typename T>
struct SourceLocated
{
    T value;
    SourceLocation source_location;

    const T& operator*() const { return value; }
    T& operator*() { return value; }

};

using SourceLocatedExpr = SourceLocated<Expr::ExprAst>;
}
