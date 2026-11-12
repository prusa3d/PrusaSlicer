#pragma once

#include "Slic3r/Domain/Preset/SourceLocatedExpr.hpp"

namespace Slic3r::Domain::Preset {

struct ParsedExpr
{
    /**
     * @brief Source located simplified expression
     */
    SourceLocatedExpr expr;
    /**
     * @brief String representation of epxr
     */
    std::string expr_str;
};

} // namespace Slic3r::Domain::Preset
